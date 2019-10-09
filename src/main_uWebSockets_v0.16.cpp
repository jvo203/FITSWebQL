#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_PORT 8080
#define SERVER_STRING                                                          \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-10-09.0"
#define WASM_STRING "WASM2019-02-08.1"

#include <zlib.h>

/* CHUNK is the size of the memory chunk used by the zlib routines. */

#define CHUNK 0x4000
#define windowBits 15
#define GZIP_ENCODING 16

/* The following macro calls a zlib routine and checks the return
   value. If the return value ("status") is not OK, it prints an error
   message and exits the program. Zlib's error statuses are all less
   than zero. */

#define CALL_ZLIB(x)                                                           \
  {                                                                            \
    int status;                                                                \
    status = x;                                                                \
    if (status < 0) {                                                          \
      fprintf(stderr, "%s:%d: %s returned a bad status of %d.\n", __FILE__,    \
              __LINE__, #x, status);                                           \
      /*exit(EXIT_FAILURE);*/                                                  \
    }                                                                          \
  }

#include <pwd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <ipp.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "App.h"
#include <curl/curl.h>
#include <sqlite3.h>

#ifndef LOCAL
#include <libpq-fe.h>

#define FITSHOME "/home"
#define JVO_HOST "localhost"
#define JVO_USER "jvo"
#endif

#include "fits.hpp"
#include "json.h"

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::shared_mutex fits_mutex;
std::string home_dir;
int server_port = SERVER_PORT;
sqlite3 *splat_db = NULL;

inline const char *check_null(const char *str) {
  if (str != nullptr)
    return str;
  else
    return "\"\"";
};

void signalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received.\n";

  // cleanup and close up stuff here
  // terminate program

  curl_global_cleanup();

  if (splat_db != NULL)
    sqlite3_close(splat_db);

  std::cout << "FITSWebQL shutdown completed." << std::endl;

  exit(signum);
}

bool is_gzip(const char *filename) {
  int fd = open(filename, O_RDONLY);

  if (fd == -1)
    return false;

  bool ok = true;
  uint8_t header[10];

  // try to read the first 10 bytes
  ssize_t bytes_read = read(fd, header, 10);

  // test for magick numbers and the deflate compression type
  if (bytes_read == 10) {
    if (header[0] != 0x1f || header[1] != 0x8b || header[2] != 0x08)
      ok = false;
  } else
    ok = false;

  close(fd);

  return ok;
}

// resource not found
void http_not_found(auto *res) {
  const std::string not_found =
      "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
  res->Super::write(not_found.data(), not_found.length());
  // res->write(not_found.c_str());
}

// server error
void http_internal_server_error(auto *res) {
  const std::string server_error =
      "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
  res->Super::write(server_error.data(), server_error.length());
}

// request accepted but not ready yet
void http_accepted(auto *res) {
  const std::string accepted =
      "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n";
  res->Super::write(accepted.data(), accepted.length());
}

// functionality not implemented/not available
void http_not_implemented(auto *res) {
  const std::string not_implemented =
      "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n";
  res->Super::write(not_implemented.data(), not_implemented.length());
}

void write_status(auto *res, int code, std::string message) {
  std::string status =
      "HTTP/1.1 " + std::to_string(code) + " " + message + "\r\n";
  res->Super::write(status.data(), status.length());
}

void write_content_length(auto *res, size_t length) {
  std::string content_length =
      "Content-Length: " + std::to_string(length) + "\r\n";
  res->Super::write(content_length.data(), content_length.length());
}

void write_content_type(auto *res, std::string mime) {
  std::string content_type = "Content-Type: " + mime + "\r\n";
  res->Super::write(content_type.data(), content_type.length());
}

void write_key_value(auto *res, std::string key, std::string value) {
  std::string content_type = key + ": " + value + "\r\n";
  res->Super::write(content_type.data(), content_type.length());
}

void get_spectrum(auto *res, std::shared_ptr<FITS> fits) {
  std::ostringstream json;

  fits->to_json(json);

  if (json.tellp() > 0) {
    write_status(res, 200, "OK");
    write_content_length(res, json.tellp());
    write_content_type(res, "application/json");
    write_key_value(res, "Cache-Control", "no-cache");
    write_key_value(res, "Cache-Control", "no-store");
    write_key_value(res, "Pragma", "no-cache");
    res->Super::write("\r\n", 2);
    res->Super::write(json.str().c_str(), json.tellp());
    res->Super::write("\r\n\r\n", 4);
  } else {
    return http_not_implemented(res);
  }
}

/*struct MolecularStream
{
    bool first;
    bool compress;
    auto *res;
    z_stream z;
    unsigned char out[CHUNK];
    FILE *fp;
};*/

uintmax_t ComputeFileSize(const fs::path &pathToCheck) {
  if (fs::exists(pathToCheck) && fs::is_regular_file(pathToCheck)) {
    auto err = std::error_code{};
    auto filesize = fs::file_size(pathToCheck, err);
    if (filesize != static_cast<uintmax_t>(-1))
      return filesize;
  }

  return static_cast<uintmax_t>(-1);
}

void get_directory(auto *res, std::string dir) {
  std::cout << "scanning directory " << dir << std::endl;

  fs::path pathToShow(dir);

  std::map<std::string, std::string> entries;

  if (fs::exists(pathToShow) && fs::is_directory(pathToShow)) {
    for (const auto &entry : fs::directory_iterator(pathToShow)) {
      if (!fs::exists(entry))
        continue;

      auto filename = entry.path().filename();
      auto timestamp = fs::last_write_time(entry);
      time_t cftime = std::chrono::system_clock::to_time_t(timestamp);
      std::string last_modified = std::asctime(std::localtime(&cftime));
      last_modified.pop_back();

      if (fs::is_directory(entry.status())) {
        if (!boost::algorithm::starts_with(filename.string(), ".")) {
          char *encoded = json_encode_string(filename.c_str());

          std::string json =
              "{\"type\" : \"dir\", \"name\" : " + std::string(encoded) +
              ", \"last_modified\" : \"" + last_modified + "\"}";

          if (encoded != NULL)
            free(encoded);

          std::cout << json << std::endl;

          entries.insert(std::pair(filename, json));
        }
      } else if (fs::is_regular_file(entry.status())) {
        // check the extensions .fits or .fits.gz
        const std::string lower_filename =
            boost::algorithm::to_lower_copy(filename.string());

        if (boost::algorithm::ends_with(lower_filename, ".fits") ||
            boost::algorithm::ends_with(lower_filename, ".fits.gz")) {

          char *encoded = json_encode_string(filename.c_str());

          uintmax_t filesize = ComputeFileSize(entry);

          std::string json =
              "{\"type\" : \"file\", \"name\" : " + std::string(encoded) +
              ", \"size\" : " + std::to_string(filesize) +
              ", \"last_modified\" : \"" + last_modified + "\"}";

          if (encoded != NULL)
            free(encoded);

          std::cout << json << std::endl;

          entries.insert(std::pair(filename, json));
        }
      }
    }
  }

  std::ostringstream json;

  char *encoded = json_encode_string(check_null(dir.c_str()));

  json << "{\"location\" : " << check_null(encoded) << ", \"contents\" : [";

  if (encoded != NULL)
    free(encoded);

  if (entries.size() > 0) {
    for (auto const &entry : entries) {
      json << entry.second << ",";
    }

    // overwrite the the last ',' with a list closing character
    json.seekp(-1, std::ios_base::end);
  }

  json << "]}";

  write_status(res, 200, "OK");
  write_content_length(res, json.tellp());
  write_content_type(res, "application/json");
  write_key_value(res, "Cache-Control", "no-cache");
  write_key_value(res, "Cache-Control", "no-store");
  write_key_value(res, "Pragma", "no-cache");
  res->Super::write("\r\n", 2);
  res->Super::write(json.str().c_str(), json.tellp());
  res->Super::write("\r\n\r\n", 4);
}

void get_home_directory(auto *res) {
  if (home_dir != "")
    return get_directory(res, home_dir);
  else
    return http_not_found(res);
}

void serve_file(auto *res, std::string uri) {
  std::string resource = "htdocs" + uri;

  // strip '?' from the requested file name
  size_t pos = resource.find("?");

  if (pos != std::string::npos)
    resource = resource.substr(0, pos);

  std::cout << "serving " << resource << std::endl;

  // mmap a disk resource
  int fd = -1;
  void *buffer = NULL;

  struct stat64 st;
  stat64(resource.c_str(), &st);
  long size = st.st_size;

  fd = open(resource.c_str(), O_RDONLY);

  if (fd != -1) {
    buffer = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (buffer != NULL) {
      write_status(res, 200, "OK");
      write_content_length(res, size);

      // detect mime-types
      size_t pos = resource.find_last_of(".");

      if (pos != std::string::npos) {
        std::string ext = resource.substr(pos + 1, std::string::npos);

        if (ext == "htm" || ext == "html")
          write_content_type(res, "text/html");

        if (ext == "txt")
          write_content_type(res, "text/plain");

        if (ext == "js")
          write_content_type(res, "application/javascript");

        if (ext == "ico")
          write_content_type(res, "image/x-icon");

        if (ext == "png")
          write_content_type(res, "image/png");

        if (ext == "gif")
          write_content_type(res, "image/gif");

        if (ext == "webp")
          write_content_type(res, "image/webp");

        if (ext == "jpg" || ext == "jpeg")
          write_content_type(res, "image/jpeg");

        if (ext == "bpg")
          write_content_type(res, "image/bpg");

        if (ext == "mp4")
          write_content_type(res, "video/mp4");

        if (ext == "hevc")
          write_content_type(res, "video/hevc");

        if (ext == "css")
          write_content_type(res, "text/css");

        if (ext == "pdf")
          write_content_type(res, "application/pdf");

        if (ext == "svg")
          write_content_type(res, "image/svg+xml");

        if (ext == "wasm")
          write_content_type(res, "application/wasm");
      }

      res->Super::write("\r\n", 2);
      res->Super::write((const char *)buffer, size);
      res->Super::write("\r\n\r\n", 4);
    } else {
      perror("error mapping a file");
      http_not_found(res);
    }

    if (munmap(buffer, size) == -1)
      perror("un-mapping error");

    close(fd);
  } else
    http_not_found(res);
}

#ifndef LOCAL
PGconn *jvo_db_connect(std::string db) {
  PGconn *jvo_db = NULL;

  std::string conn_str =
      "dbname=" + db + " host=" + JVO_HOST + " user=" + JVO_USER;

  jvo_db = PQconnectdb(conn_str.c_str());

  if (PQstatus(jvo_db) != CONNECTION_OK) {
    fprintf(stderr, "PostgreSQL connection failed: %s\n",
            PQerrorMessage(jvo_db));
    PQfinish(jvo_db);
    jvo_db = NULL;
  } else
    printf("PostgreSQL connection successful.\n");

  return jvo_db;
}

std::string get_jvo_path(PGconn *jvo_db, std::string db, std::string table,
                         std::string data_id) {
  std::string path;

  std::string sql_str =
      "SELECT path FROM " + table + " WHERE data_id = '" + data_id + "';";

  PGresult *res = PQexec(jvo_db, sql_str.c_str());
  int status = PQresultStatus(res);

  if (PQresultStatus(res) == PGRES_TUPLES_OK) {
    path = std::string(FITSHOME) + "/" + db + "/";

    size_t pos = table.find(".");

    if (pos == std::string::npos)
      path += std::string((const char *)PQgetvalue(res, 0, 0));
    else
      path += boost::algorithm::to_upper_copy(table.substr(0, pos)) + "/" +
              std::string((const char *)PQgetvalue(res, 0, 0));
  }

  PQclear(res);

  return path;
}
#endif

void ipp_init() {
  const IppLibraryVersion *lib;
  IppStatus status;
  Ipp64u mask, emask;

  /* Init IPP library */
  ippInit();
  /* Get IPP library version info */
  lib = ippGetLibVersion();
  printf("%s %s\n", lib->Name, lib->Version);

  /* Get CPU features and features enabled with selected library level */
  status = ippGetCpuFeatures(&mask, 0);
  if (ippStsNoErr == status) {
    emask = ippGetEnabledCpuFeatures();
    printf("Features supported by CPU\tby IPP\n");
    printf("-----------------------------------------\n");
    printf("  ippCPUID_MMX        = ");
    printf("%c\t%c\t", (mask & ippCPUID_MMX) ? 'Y' : 'N',
           (emask & ippCPUID_MMX) ? 'Y' : 'N');
    printf("Intel(R) Architecture MMX technology supported\n");
    printf("  ippCPUID_SSE        = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE) ? 'Y' : 'N',
           (emask & ippCPUID_SSE) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions\n");
    printf("  ippCPUID_SSE2       = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE2) ? 'Y' : 'N',
           (emask & ippCPUID_SSE2) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 2\n");
    printf("  ippCPUID_SSE3       = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE3) ? 'Y' : 'N',
           (emask & ippCPUID_SSE3) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 3\n");
    printf("  ippCPUID_SSSE3      = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSSE3) ? 'Y' : 'N',
           (emask & ippCPUID_SSSE3) ? 'Y' : 'N');
    printf("Intel(R) Supplemental Streaming SIMD Extensions 3\n");
    printf("  ippCPUID_MOVBE      = ");
    printf("%c\t%c\t", (mask & ippCPUID_MOVBE) ? 'Y' : 'N',
           (emask & ippCPUID_MOVBE) ? 'Y' : 'N');
    printf("The processor supports MOVBE instruction\n");
    printf("  ippCPUID_SSE41      = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE41) ? 'Y' : 'N',
           (emask & ippCPUID_SSE41) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 4.1\n");
    printf("  ippCPUID_SSE42      = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE42) ? 'Y' : 'N',
           (emask & ippCPUID_SSE42) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 4.2\n");
    printf("  ippCPUID_AVX        = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX) ? 'Y' : 'N',
           (emask & ippCPUID_AVX) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions instruction set\n");
    printf("  ippAVX_ENABLEDBYOS  = ");
    printf("%c\t%c\t", (mask & ippAVX_ENABLEDBYOS) ? 'Y' : 'N',
           (emask & ippAVX_ENABLEDBYOS) ? 'Y' : 'N');
    printf("The operating system supports Intel(R) AVX\n");
    printf("  ippCPUID_AES        = ");
    printf("%c\t%c\t", (mask & ippCPUID_AES) ? 'Y' : 'N',
           (emask & ippCPUID_AES) ? 'Y' : 'N');
    printf("Intel(R) AES instruction\n");
    printf("  ippCPUID_SHA        = ");
    printf("%c\t%c\t", (mask & ippCPUID_SHA) ? 'Y' : 'N',
           (emask & ippCPUID_SHA) ? 'Y' : 'N');
    printf("Intel(R) SHA new instructions\n");
    printf("  ippCPUID_CLMUL      = ");
    printf("%c\t%c\t", (mask & ippCPUID_CLMUL) ? 'Y' : 'N',
           (emask & ippCPUID_CLMUL) ? 'Y' : 'N');
    printf("PCLMULQDQ instruction\n");
    printf("  ippCPUID_RDRAND     = ");
    printf("%c\t%c\t", (mask & ippCPUID_RDRAND) ? 'Y' : 'N',
           (emask & ippCPUID_RDRAND) ? 'Y' : 'N');
    printf("Read Random Number instructions\n");
    printf("  ippCPUID_F16C       = ");
    printf("%c\t%c\t", (mask & ippCPUID_F16C) ? 'Y' : 'N',
           (emask & ippCPUID_F16C) ? 'Y' : 'N');
    printf("Float16 instructions\n");
    printf("  ippCPUID_AVX2       = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX2) ? 'Y' : 'N',
           (emask & ippCPUID_AVX2) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions 2 instruction set\n");
    printf("  ippCPUID_AVX512F    = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX512F) ? 'Y' : 'N',
           (emask & ippCPUID_AVX512F) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions 3.1 instruction set\n");
    printf("  ippCPUID_AVX512CD   = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX512CD) ? 'Y' : 'N',
           (emask & ippCPUID_AVX512CD) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions CD (Conflict Detection) "
           "instruction set\n");
    printf("  ippCPUID_AVX512ER   = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX512ER) ? 'Y' : 'N',
           (emask & ippCPUID_AVX512ER) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions ER instruction set\n");
    printf("  ippCPUID_ADCOX      = ");
    printf("%c\t%c\t", (mask & ippCPUID_ADCOX) ? 'Y' : 'N',
           (emask & ippCPUID_ADCOX) ? 'Y' : 'N');
    printf("ADCX and ADOX instructions\n");
    printf("  ippCPUID_RDSEED     = ");
    printf("%c\t%c\t", (mask & ippCPUID_RDSEED) ? 'Y' : 'N',
           (emask & ippCPUID_RDSEED) ? 'Y' : 'N');
    printf("The RDSEED instruction\n");
    printf("  ippCPUID_PREFETCHW  = ");
    printf("%c\t%c\t", (mask & ippCPUID_PREFETCHW) ? 'Y' : 'N',
           (emask & ippCPUID_PREFETCHW) ? 'Y' : 'N');
    printf("The PREFETCHW instruction\n");
    printf("  ippCPUID_KNC        = ");
    printf("%c\t%c\t", (mask & ippCPUID_KNC) ? 'Y' : 'N',
           (emask & ippCPUID_KNC) ? 'Y' : 'N');
    printf("Intel(R) Xeon Phi(TM) Coprocessor instruction set\n");
  }
}

struct UserData {
  std::string session_id;
};

int main(int argc, char *argv[]) {
  ipp_init();
  curl_global_init(CURL_GLOBAL_ALL);

  int rc = sqlite3_open_v2("splatalogue_v3.db", &splat_db,
                           SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

  if (rc) {
    fprintf(stderr, "Can't open local splatalogue database: %s\n",
            sqlite3_errmsg(splat_db));
    sqlite3_close(splat_db);
    splat_db = NULL;
  }

  struct passwd *passwdEnt = getpwuid(getuid());
  home_dir = passwdEnt->pw_dir;

  // register signal SIGINT and signal handler
  signal(SIGINT, signalHandler);

  // parse local command-line options
  if (argc > 2) {
    for (int i = 1; i < argc - 1; i++) {
      const char *key = argv[i];
      const char *value = argv[i + 1];

      if (!strcmp(key, "--port"))
        server_port = atoi(value);

      if (!strcmp(key, "--home"))
        home_dir = std::string(value);
    }
  }

  std::cout << SERVER_STRING << " (" << VERSION_STRING << ")" << std::endl;
  std::cout << "Browser URL: http://localhost:" << server_port << std::endl;

  int no_threads = MIN(MAX(std::thread::hardware_concurrency() / 2, 1), 4);
  std::vector<std::thread *> threads(no_threads);

  std::transform(
      threads.begin(), threads.end(), threads.begin(), [](std::thread *t) {
        return new std::thread([]() {
          uWS::App()
              .get("/*",
                   [](auto *res, auto *req) {
                     // res->write("FITSWebQL v5 Supercomputer Edition");
                     std::string_view uri = req->getUrl();
                     std::cout << "HTTP request for " << uri << std::endl;

                     // root
                     if (uri == "/")
#ifdef LOCAL
                       return serve_file(res, "/local.html");
#else
                        return serve_file(res, "/test.html");
#endif

                     if (uri.find("/get_directory") != std::string::npos) {
                       std::string dir;

                       // get a position of '?'
                       size_t pos = uri.find("?");

                       if (pos != std::string::npos) {
                         std::string_view query =
                             uri.substr(pos + 1, std::string::npos);
                         std::cout << "query: (" << query << ")" << std::endl;

                         std::vector<std::string> params;
                         boost::split(params, query,
                                      [](char c) { return c == '&'; });

                         for (auto const &s : params) {
                           // find '='
                           size_t pos = s.find("=");

                           if (pos != std::string::npos) {
                             std::string key = s.substr(0, pos);
                             std::string value =
                                 s.substr(pos + 1, std::string::npos);

                             if (key == "dir") {
                               CURL *curl = curl_easy_init();

                               char *str = curl_easy_unescape(
                                   curl, value.c_str(), value.length(), NULL);
                               dir = std::string(str);
                               curl_free(str);

                               curl_easy_cleanup(curl);
                             }
                           }
                         }
                       }

                       if (dir != "")
                         return get_directory(res, dir);
                       else
                         return get_home_directory(res);
                     };
                   })
              .listen(server_port,
                      [](auto *token) {
                        if (token) {
                          std::cout << "Thread " << std::this_thread::get_id()
                                    << " listening on port " << server_port
                                    << std::endl;
                        } else {
                          std::cout << "Thread " << std::this_thread::get_id()
                                    << " failed to listen on port "
                                    << server_port << std::endl;
                        }
                      })
              .run();
        });
      });

  std::for_each(threads.begin(), threads.end(),
                [](std::thread *t) { t->join(); });

  if (splat_db != NULL)
    sqlite3_close(splat_db);
}
