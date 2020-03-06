#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define HTTPS_PORT 8080
#define WSS_PORT 8081
#define SERVER_STRING                                                          \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2020-03-06.0"
#define WASM_STRING "WASM2019-02-08.1"

#define PROGRESS_TIMEOUT 250 /*[ms]*/

#include <OpenEXR/IlmThread.h>
#include <OpenEXR/ImfNamespace.h>
#include <OpenEXR/ImfThreading.h>

#include <omp.h>

#include <deque>
#include <filesystem>
#include <iostream>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>

#include <sqlite3.h>

#include "fits.hpp"
//#include "global.h"
#include "json.h"

/** Thread safe cout class
 * Exemple of use:
 *    PrintThread{} << "Hello world!" << std::endl;
 */
class PrintThread : public std::ostringstream {
public:
  PrintThread() = default;

  ~PrintThread() {
    std::lock_guard<std::mutex> guard(_mutexPrint);
    std::cout << this->str();
  }

private:
  static std::mutex _mutexPrint;
};

std::mutex PrintThread::_mutexPrint{};

#ifdef CLUSTER
#include <czmq.h>

inline std::set<std::string> cluster;
inline std::shared_mutex cluster_mtx;

inline bool cluster_contains_node(std::string node) {
  std::shared_lock<std::shared_mutex> lock(cluster_mtx);

  if (cluster.find(node) == cluster.end())
    return false;
  else
    return true;
}

inline void cluster_insert_node(std::string node) {
  std::lock_guard<std::shared_mutex> guard(cluster_mtx);
  cluster.insert(node);
}

inline void cluster_erase_node(std::string node) {
  std::lock_guard<std::shared_mutex> guard(cluster_mtx);
  cluster.erase(node);
}

zactor_t *speaker = NULL;
zactor_t *listener = NULL;
std::thread beacon_thread;
std::atomic<bool> exiting(false);
#endif

sqlite3 *splat_db = NULL;

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

struct TransmitQueue {
  boost::lockfree::spsc_queue<uint8_t> q{10 * CHUNK};
  std::deque<uint8_t> fifo;
  std::mutex mtx;
  std::atomic<bool> eof;

  TransmitQueue() { eof = false; }
};

struct MolecularStream {
  bool first;
  bool compress;
  struct TransmitQueue *queue;
  z_stream z;
  unsigned char out[CHUNK];
  FILE *fp;
};

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::shared_mutex fits_mutex;

std::shared_ptr<FITS> get_dataset(std::string id) {
  std::shared_lock<std::shared_mutex> lock(fits_mutex);

  auto item = DATASETS.find(id);

  if (item == DATASETS.end())
    return nullptr;
  else
    return item->second;
}

void insert_dataset(std::string id, std::shared_ptr<FITS> fits) {
  std::lock_guard<std::shared_mutex> guard(fits_mutex);

  DATASETS.insert(std::pair(id, fits));
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

inline const char *check_null(const char *str) {
  if (str != NULL)
    return str;
  else
    return ""; //"\"\"" ;
};

#include <nghttp2/asio_http2_server.h>

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

http2 *http2_server;
std::string docs_root = "htdocs2";
std::string home_dir;

void http_not_found(const response *res) {
  res->write_head(404);
  res->end("Not Found");
}

void http_not_implemented(const response *res) {
  res->write_head(501);
  res->end("Not Implemented");
}

void http_accepted(const response *res) {
  res->write_head(202);
  res->end("Accepted");
}

void http_internal_server_error(const response *res) {
  res->write_head(500);
  res->end("Internal Server Error");
}

#ifdef LOCAL
void serve_directory(const response *res, std::string dir) {
  printf("get_directory(%s)\n", dir.c_str());

  struct dirent **namelist = NULL;
  int i, n;

  n = scandir(dir.c_str(), &namelist, 0, alphasort);

  std::ostringstream json;

  char *encoded = json_encode_string(dir.c_str());

  json << "{\"location\" : " << check_null(encoded) << ", \"contents\" : [";

  if (encoded != NULL)
    free(encoded);

  bool has_contents = false;

  if (n < 0) {
    perror("scandir");

    json << "]}";
  } else {
    for (i = 0; i < n; i++) {
      // printf("%s\n", namelist[i]->d_name);

      char pathname[1024];

      sprintf(pathname, "%s/%s", dir.c_str(), check_null(namelist[i]->d_name));

      struct stat64 sbuf;

      int err = stat64(pathname, &sbuf);

      if (err == 0) {
        char last_modified[255];

        struct tm lm;
        localtime_r(&sbuf.st_mtime, &lm);
        strftime(last_modified, sizeof(last_modified) - 1,
                 "%a, %d %b %Y %H:%M:%S %Z", &lm);

        size_t filesize = sbuf.st_size;

        if (S_ISDIR(sbuf.st_mode) && namelist[i]->d_name[0] != '.') {
          char *encoded = json_encode_string(check_null(namelist[i]->d_name));

          json << "{\"type\" : \"dir\", \"name\" : " << check_null(encoded)
               << ", \"last_modified\" : \"" << last_modified << "\"},";
          has_contents = true;

          if (encoded != NULL)
            free(encoded);
        }

        if (S_ISREG(sbuf.st_mode)) {
          const std::string filename = std::string(namelist[i]->d_name);
          const std::string lower_filename =
              boost::algorithm::to_lower_copy(filename);

          // if(!strcasecmp(get_filename_ext(check_null(namelist[i]->d_name)),
          // "fits"))
          if (boost::algorithm::ends_with(lower_filename, ".fits") ||
              boost::algorithm::ends_with(lower_filename, ".fits.gz")) {
            char *encoded = json_encode_string(check_null(namelist[i]->d_name));

            json << "{\"type\" : \"file\", \"name\" : " << check_null(encoded)
                 << ", \"size\" : " << filesize << ", \"last_modified\" : \""
                 << last_modified << "\"},";
            has_contents = true;

            if (encoded != NULL)
              free(encoded);
          }
        }
      } else
        perror("stat64");

      free(namelist[i]);
    };

    // overwrite the the last ',' with a list closing character
    if (has_contents)
      json.seekp(-1, std::ios_base::end);

    json << "]}";
  };

  if (namelist != NULL)
    free(namelist);

  header_map mime;
  mime.insert(std::pair<std::string, header_value>(
      "Content-Type", {"application/json", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  res->write_head(200, mime);
  res->end(json.str());
}
#endif

static int sqlite_callback(void *userp, int argc, char **argv,
                           char **azColName) {
  MolecularStream *stream = (MolecularStream *)userp;

  if (argc == 8) {
    std::string json;

    if (stream->first) {
      stream->first = false;
      json = "{\"molecules\" : [";
    } else
      json = ",";

    // json-encode a spectral line
    char *encoded;

    // species
    encoded = json_encode_string(check_null(argv[0]));
    json += "{\"species\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // name
    encoded = json_encode_string(check_null(argv[1]));
    json += "\"name\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // frequency
    json += "\"frequency\" : " + std::string(check_null(argv[2])) + ",";

    // quantum numbers
    encoded = json_encode_string(check_null(argv[3]));
    json += "\"quantum\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // cdms_intensity
    encoded = json_encode_string(check_null(argv[4]));
    json += "\"cdms\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // lovas_intensity
    encoded = json_encode_string(check_null(argv[5]));
    json += "\"lovas\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // E_L
    encoded = json_encode_string(check_null(argv[6]));
    json += "\"E_L\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // linelist
    encoded = json_encode_string(check_null(argv[7]));
    json += "\"list\" : " + std::string(check_null(encoded)) + "}";
    if (encoded != NULL)
      free(encoded);

    // printf("%s\n", json.c_str());

    if (stream->compress) {
      stream->z.avail_in = json.length();                // size of input
      stream->z.next_in = (unsigned char *)json.c_str(); // input char array

      do {
        stream->z.avail_out = CHUNK;      // size of output
        stream->z.next_out = stream->out; // output char array
        CALL_ZLIB(deflate(&stream->z, Z_NO_FLUSH));
        size_t have = CHUNK - stream->z.avail_out;

        if (have > 0) {
          // printf("ZLIB avail_out: %zu\n", have);
          if (stream->fp != NULL)
            fwrite((const char *)stream->out, sizeof(char), have, stream->fp);

          /*size_t pushed =
              stream->queue->q.push((const uint8_t *)stream->out, have);
          if (pushed != have)
            fprintf(stderr, "error appending to spsc_queue\n");*/

          std::lock_guard<std::mutex> guard(stream->queue->mtx);
          stream->queue->fifo.insert(stream->queue->fifo.end(), stream->out,
                                     stream->out + have);
        }
      } while (stream->z.avail_out == 0);
    } else {
      /*size_t pushed =
          stream->queue->q.push((const uint8_t *)json.c_str(), json.size());
      if (pushed != json.size())
        fprintf(stderr, "error appending to spsc_queue\n");*/

      std::lock_guard<std::mutex> guard(stream->queue->mtx);
      stream->queue->fifo.insert(stream->queue->fifo.end(), json.c_str(),
                                 json.c_str() + json.size());
    }
  }

  return 0;
}

generator_cb progress_generator(std::shared_ptr<FITS> fits) {
  return [fits](uint8_t *buf, size_t len,
                uint32_t *data_flags) -> generator_cb::result_type {
    ssize_t n = 0;
    std::ostringstream data;
    bool eof = false;

    // check if running == total
    {
      std::shared_lock<std::shared_mutex> lock(fits->progress_mtx);

      if (fits->progress.total > 0 &&
          fits->progress.total == fits->progress.running)
        eof = true;
    }

    // if not, sleep for <PROGRESS_TIMEOUT> milliseconds
    if (!eof) {
      struct timespec ts;
      ts.tv_sec = PROGRESS_TIMEOUT / 1000;
      ts.tv_nsec = (PROGRESS_TIMEOUT % 1000) * 1000000;
      nanosleep(&ts, NULL);
    }

    // lock a read-only progress mutex
    {
      std::shared_lock<std::shared_mutex> lock(fits->progress_mtx);

      // make a json response
      if (fits->progress.total > 0) {
        data << "data:";
        data << "{ \"total\" : " << fits->progress.total << ", ";
        data << "\"running\" : " << fits->progress.running << ", ";
        data << "\"elapsed\" : " << fits->progress.elapsed << " }";
        data << "\n\n";
      }
    }

    // printf("sending progress notification: %s\n", data.str().c_str());

    // send it
    size_t size = data.str().size();

    if (size > 0 && size < len) {
      memcpy(buf, data.str().c_str(), size);
      n = size;
    } else if (size > len)
      eof = true;

    if (eof) {
      printf("closing the event stream.\n");
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }

    return n;
  };
}

generator_cb stream_generator(struct TransmitQueue *queue) {
  return [queue](uint8_t *buf, size_t len,
                 uint32_t *data_flags) -> generator_cb::result_type {
    ssize_t n = 0;

    /*if (queue->q.read_available() > 0) {
      printf("queue length: %zu buffer length: %zu bytes.\n",
             queue->q.read_available(), len);

      n = queue->q.pop(buf, len);
    }*/

    std::unique_lock<std::mutex> lock(queue->mtx);

    if (queue->fifo.size() > 0) {
      size_t size = std::min(len, queue->fifo.size());

      // printf("queue length: %zu buffer length: %zu bytes.\n",
      //       queue->fifo.size(), len);

      // pop elements up to <len>
      std::copy(queue->fifo.begin(), queue->fifo.begin() + size, buf);
      queue->fifo.erase(queue->fifo.begin(), queue->fifo.begin() + size);
      n = size;
    }

    // if (queue->eof && queue->q.read_available() == 0) {
    if (queue->eof && queue->fifo.empty()) {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;

      lock.release();
      free(queue);
    }

    return n;
  };
}

void stream_image(const response *res, std::shared_ptr<FITS> fits, int _width,
                  int _height) {
  header_map mime;
  mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                   {"image/png", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  res->write_head(200, mime);

  struct TransmitQueue *queue = new TransmitQueue();

  res->end(stream_generator(queue));

  // launch a separate image thread
  std::thread([queue, fits]() {
    // calculate a new image size

    // downsize + make luminance

    // append image bytes to the queue

    std::lock_guard<std::mutex> guard(queue->mtx);
    printf("[stream_image] number of remaining bytes: %zu\n",
           queue->fifo.size());

    // end of chunked encoding
    queue->eof = true;
  }).detach();
}

void stream_molecules(const response *res, double freq_start, double freq_end,
                      bool compress) {
  if (splat_db == NULL)
    return http_internal_server_error(res);

  header_map mime;
  mime.insert(std::pair<std::string, header_value>(
      "Content-Type", {"application/json", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  // append the compression mime
  if (compress)
    mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                     {"gzip", false}));
  res->write_head(200, mime);

  struct TransmitQueue *queue = new TransmitQueue();

  res->end(stream_generator(queue));

  // launch a separate molecules thread
  std::thread([queue, compress, freq_start, freq_end]() {
    char strSQL[256];
    int rc;
    char *zErrMsg = 0;

    snprintf(strSQL, 256,
             "SELECT * FROM lines WHERE frequency>=%f AND frequency<=%f;",
             freq_start, freq_end);
    printf("%s\n", strSQL);

    struct MolecularStream stream;
    stream.first = true;
    stream.compress = compress;
    stream.queue = queue;
    stream.fp = NULL;

    if (compress) {
      stream.z.zalloc = Z_NULL;
      stream.z.zfree = Z_NULL;
      stream.z.opaque = Z_NULL;
      stream.z.next_in = Z_NULL;
      stream.z.avail_in = 0;

      CALL_ZLIB(deflateInit2(&stream.z, Z_BEST_COMPRESSION, Z_DEFLATED,
                             windowBits | GZIP_ENCODING, 9,
                             Z_DEFAULT_STRATEGY));
    }

    rc = sqlite3_exec(splat_db, strSQL, sqlite_callback, &stream, &zErrMsg);

    if (rc != SQLITE_OK) {
      fprintf(stderr, "SQL error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
    }

    std::string chunk_data;

    if (stream.first)
      chunk_data = "{\"molecules\" : []}";
    else
      chunk_data = "]}";

    if (compress) {
      stream.z.avail_in = chunk_data.length();
      stream.z.next_in = (unsigned char *)chunk_data.c_str();

      do {
        stream.z.avail_out = CHUNK;     // size of output
        stream.z.next_out = stream.out; // output char array
        CALL_ZLIB(deflate(&stream.z, Z_FINISH));
        size_t have = CHUNK - stream.z.avail_out;

        if (have > 0) {
          // printf("Z_FINISH avail_out: %zu\n", have);
          if (stream.fp != NULL)
            fwrite((const char *)stream.out, sizeof(char), have, stream.fp);

          /*size_t pushed =
              stream.queue->q.push((const uint8_t *)stream.out, have);
          if (pushed != have)
            fprintf(stderr, "error appending to spsc_queue\n");*/

          std::lock_guard<std::mutex> guard(stream.queue->mtx);
          stream.queue->fifo.insert(stream.queue->fifo.end(), stream.out,
                                    stream.out + have);
        }
      } while (stream.z.avail_out == 0);

      CALL_ZLIB(deflateEnd(&stream.z));

      if (stream.fp != NULL)
        fclose(stream.fp);
    } else {
      /*size_t pushed = stream.queue->q.push((const uint8_t
      *)chunk_data.c_str(), chunk_data.size()); if (pushed != chunk_data.size())
        fprintf(stderr, "error appending to spsc_queue\n");*/

      std::lock_guard<std::mutex> guard(stream.queue->mtx);
      stream.queue->fifo.insert(stream.queue->fifo.end(), chunk_data.c_str(),
                                chunk_data.c_str() + chunk_data.size());
    }

    /*printf("[stream_molecules] number of remaining bytes: %zu\n",
           stream.queue->q.read_available());*/

    std::lock_guard<std::mutex> guard(stream.queue->mtx);
    printf("[stream_molecules] number of remaining bytes: %zu\n",
           stream.queue->fifo.size());

    // end of chunked encoding
    stream.queue->eof = true;
  }).detach();
}

void get_spectrum(const response *res, std::shared_ptr<FITS> fits) {
  std::ostringstream json;

  fits->to_json(json);

  if (json.tellp() > 0) {
    header_map mime;
    mime.insert(std::pair<std::string, header_value>(
        "Content-Type", {"application/json", false}));
    mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                     {"no-cache", false}));

    res->write_head(200, mime);
    res->end(json.str());
  } else {
    return http_not_implemented(res);
  }
}

void serve_file(const request *req, const response *res, std::string uri) {
  // a safety check against directory traversal attacks
  if (!check_path(uri))
    return http_not_found(res);

  // check if a resource exists
  std::string path = docs_root + uri;

  if (std::filesystem::exists(path)) {
    // detect mime-types
    header_map mime;

    size_t pos = uri.find_last_of(".");

    if (pos != std::string::npos) {
      std::string ext = uri.substr(pos + 1, std::string::npos);

      if (ext == "htm" || ext == "html")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"text/html", false}));

      if (ext == "txt")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"text/plain", false}));

      if (ext == "js")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"application/javascript", false}));

      if (ext == "ico")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/x-icon", false}));

      if (ext == "png")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"image/png", false}));

      if (ext == "gif")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"image/gif", false}));

      if (ext == "webp")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/webp", false}));

      if (ext == "jpg" || ext == "jpeg")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/jpeg", false}));

      if (ext == "mp4")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"video/mp4", false}));

      if (ext == "css")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"text/css", false}));

      if (ext == "pdf")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"application/pdf", false}));

      if (ext == "svg")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/svg+xml", false}));

      if (ext == "wasm")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"application/wasm", false}));
    }

    // check for compression
    header_map headers = req->header();

    auto it = headers.find("accept-encoding");
    if (it != headers.end()) {
      auto value = it->second.value;
      // std::cout << "Supported compression: " << value << std::endl;

      // prefer brotli due to smaller file sizes
      size_t pos = value.find("br"); // brotli or gzip

      if (pos != std::string::npos) {
        if (std::filesystem::exists(path + ".br")) {
          path += ".br";
          // append the compression mime
          mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                           {"br", false}));
        }
      } else {
        // fallback to gzip
        size_t pos = value.find("gzip");

        if (pos != std::string::npos) {
          if (std::filesystem::exists(path + ".gz")) {
            path += ".gz";
            // append the compression mime
            mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                             {"gzip", false}));
          }
        }
      }
    }

    res->write_head(200, mime);
    res->end(file_generator(path));
  } else {
    http_not_found(res);
  }
}

void http_fits_response(const response *res, std::vector<std::string> datasets,
                        bool composite, bool has_fits) {
  std::string html =
      "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
  html.append(
      "<link href=\"https://fonts.googleapis.com/css?family=Inconsolata\" "
      "rel=\"stylesheet\"/>\n");
  html.append(
      "<link href=\"https://fonts.googleapis.com/css?family=Material+Icons\" "
      "rel=\"stylesheet\"/>\n");
  html.append("<script src=\"https://d3js.org/d3.v5.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/reconnecting-websocket.js\"></script>\n");
  html.append("<script "
              "src=\"//cdnjs.cloudflare.com/ajax/libs/numeral.js/2.0.6/"
              "numeral.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/ra_dec_conversion.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/sylvester.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/shortcut.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/colourmaps.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/lz4.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/marchingsquares-isocontours.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/marchingsquares-isobands.min.js\"></script>\n");

  // hevc wasm decoder
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/hevc_" WASM_STRING ".js\"></script>\n");
  html.append(R"(<script>
        Module.onRuntimeInitialized = async _ => {
            api = {                
                hevc_init: Module.cwrap('hevc_init', '', []), 
                hevc_destroy: Module.cwrap('hevc_destroy', '', []),                
                hevc_decode_nal_unit: Module.cwrap('hevc_decode_nal_unit', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'string']),               
            };                   
        };
    </script>)");

  // bootstrap
  html.append("<meta name=\"viewport\" content=\"width=device-width, "
              "initial-scale=1, "
              "user-scalable=no, minimum-scale=1, maximum-scale=1\">\n");
  html.append("<link rel=\"stylesheet\" "
              "href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/"
              "bootstrap.min.css\">\n");
  html.append("<script "
              "src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/"
              "jquery.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/"
              "bootstrap.min.js\"></script>\n");

  // FITSWebQL main JavaScript + CSS
  html.append("<script src=\"fitswebql.js?" VERSION_STRING "\"></script>\n");
  html.append("<link rel=\"stylesheet\" href=\"fitswebql.css?" VERSION_STRING
              "\"/>\n");

  // HTML content
  html.append("<title>FITSWebQL</title></head><body>\n");
  html.append("<div id='votable' style='width: 0; height: 0;' data-va_count='" +
              std::to_string(datasets.size()) + "' ");

  if (datasets.size() == 1)
    html.append("data-datasetId='" + datasets[0] + "' ");
  else {
    for (int i = 0; i < datasets.size(); i++)
      html.append("data-datasetId" + std::to_string(i + 1) + "='" +
                  datasets[i] + "' ");

    if (composite && datasets.size() <= 3)
      html.append("data-composite='1' ");
  }

  html.append("data-root-path='/" + std::string("fitswebql") +
              "/' data-server-version='" + VERSION_STRING +
              "' data-server-string='" + SERVER_STRING +
              "' data-server-mode='" + "SERVER" + "' data-has-fits='" +
              std::to_string(has_fits) + "'></div>\n");

  html.append("<script>var WS_SOCKET = 'wss://'; WS_PORT = " +
              std::to_string(WSS_PORT) + ";</script>");

  // the page entry point
  html.append(R"(<script>
        const golden_ratio = 1.6180339887;
        var ALMAWS = null ;
        var wsVideo = null ;
        var wsConn = null ;
        var firstTime = true ;
        var has_image = false ;         
        var PROGRESS_VARIABLE = 0.0 ;
        var PROGRESS_INFO = '' ;      
        var RESTFRQ = 0.0 ;
        var USER_SELFRQ = 0.0 ;
        var USER_DELTAV = 0.0 ;
        var ROOT_PATH = '/fitswebql/' ;
        var idleResize = -1;
        window.onresize = resizeMe;
        window.onbeforeunload = function() {            
            if(wsConn != null)
            {
                for(let i=0;i<va_count;i++)
                    wsConn[i].close();
            }

            if(wsVideo != null)
                wsVideo.close();
        };
        mainRenderer();
    </script>)");

  html.append("</body></html>");

  header_map mime;
  mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                   {"text/html", false}));
  res->write_head(200, mime);
  res->end(html);
}

void execute_fits(const response *res, std::string dir, std::string ext,
                  std::string db, std::string table,
                  std::vector<std::string> datasets, bool composite,
                  std::string flux) {
  bool has_fits = true;

#ifndef LOCAL
  PGconn *jvo_db = NULL;

  if (db != "")
    jvo_db = jvo_db_connect(db);
#endif

  int va_count = datasets.size();

  for (auto const &data_id : datasets) {
    auto item = get_dataset(data_id);

    if (item == nullptr) {
      // set has_fits to false and load the FITS dataset
      has_fits = false;
      std::shared_ptr<FITS> fits(new FITS(data_id, flux));

      insert_dataset(data_id, fits);

      std::string path;

      if (dir != "" && ext != "")
        path = dir + "/" + data_id + "." + ext;

#ifndef LOCAL
      if (jvo_db != NULL && table != "")
        path = get_jvo_path(jvo_db, db, table, data_id);
#endif

      if (path != "") {
        bool is_compressed = is_gzip(path.c_str());
        /*bool is_compressed = false;
          std::string lower_path = boost::algorithm::to_lower_copy(path);
          if (boost::algorithm::ends_with(lower_path, ".gz"))
          is_compressed = is_gzip(path.c_str());*/

        // load FITS data in a separate thread
        std::thread(&FITS::from_path_mmap, fits, path, is_compressed, flux,
                    va_count)
            .detach();
      } else {
        // the last resort
        std::string url = std::string("http://") + JVO_FITS_SERVER +
                          ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB +
                          "&table=cube&data_id=" + data_id + "_00_00_00";

        // download FITS data from a URL in a separate thread
        std::thread(&FITS::from_url, fits, url, flux, va_count).detach();
      }
    } else {
      has_fits = has_fits && item->has_data;
      item->update_timestamp();
    }
  }

#ifndef LOCAL
  if (jvo_db != NULL)
    PQfinish(jvo_db);
#endif

  std::cout << "has_fits: " << has_fits << std::endl;

  return http_fits_response(res, datasets, composite, has_fits);
}

void signalHandler(int signum) {
  printf("Interrupt signal (%d) received. Please wait...\n", signum);

#ifdef CLUSTER
  exiting = true;
  if (speaker != NULL) {
    zstr_sendx(speaker, "SILENCE", NULL);

    const char *message = "JVO:>FITSWEBQL::LEAVE";
    const int interval = 1000; //[ms]
    zsock_send(speaker, "sbi", "PUBLISH", message, strlen(message), interval);

    zstr_sendx(speaker, "SILENCE", NULL);
    zactor_destroy(&speaker);
  }

  if (listener != NULL) {
    zstr_sendx(listener, "UNSUBSCRIBE", NULL);
    beacon_thread.join();
    zactor_destroy(&listener);
  }
#endif

  http2_server->stop();

  /*signal(SIGINT, prevSIGINT);
  signal(SIGTERM, prevSIGTERM);
  raise(signum);*/
}

int main(int argc, char *argv[]) {
#ifdef CLUSTER
  setenv("ZSYS_SIGHANDLER", "false", 1);
  // LAN cluster node auto-discovery
  beacon_thread = std::thread([]() {
    speaker = zactor_new(zbeacon, NULL);
    if (speaker == NULL)
      return;

    zstr_send(speaker, "VERBOSE");
    zsock_send(speaker, "si", "CONFIGURE", BEACON_PORT);
    char *my_hostname = zstr_recv(speaker);
    if (my_hostname != NULL) {
      const char *message = "JVO:>FITSWEBQL::ENTER";
      const int interval = 1000; //[ms]
      zsock_send(speaker, "sbi", "PUBLISH", message, strlen(message), interval);
    }

    listener = zactor_new(zbeacon, NULL);
    if (listener == NULL)
      return;

    zstr_send(listener, "VERBOSE");
    zsock_send(listener, "si", "CONFIGURE", BEACON_PORT);
    char *hostname = zstr_recv(listener);
    if (hostname != NULL)
      free(hostname);
    else
      return;

    zsock_send(listener, "sb", "SUBSCRIBE", "", 0);
    zsock_set_rcvtimeo(listener, 500);

    while (!exiting) {
      char *ipaddress = zstr_recv(listener);
      if (ipaddress != NULL) {
        zframe_t *content = zframe_recv(listener);
        std::string_view message = std::string_view(
            (const char *)zframe_data(content), zframe_size(content));

        // ENTER
        if (message.find("ENTER") != std::string::npos) {
          if (strcmp(my_hostname, ipaddress) != 0) {
            std::string node = std::string(ipaddress);

            if (!cluster_contains_node(node)) {
              PrintThread{} << "found a new peer @ " << ipaddress << ": "
                            << message << std::endl;
              cluster_insert_node(node);
            }
          }
        }

        // LEAVE
        if (message.find("LEAVE") != std::string::npos) {
          if (strcmp(my_hostname, ipaddress) != 0) {
            std::string node = std::string(ipaddress);

            if (cluster_contains_node(node)) {
              PrintThread{} << ipaddress << " is leaving: " << message
                            << std::endl;
              cluster_erase_node(node);
            }
          }
        }

        zframe_destroy(&content);
        zstr_free(&ipaddress);
      }
    }

    if (my_hostname != NULL)
      free(my_hostname);
  });
#endif

  if (ILMTHREAD_NAMESPACE::supportsThreads()) {
    int omp_threads = omp_get_max_threads();
    OPENEXR_IMF_NAMESPACE::setGlobalThreadCount(omp_threads);
    std::cout << "[OpenEXR] number of threads: "
              << OPENEXR_IMF_NAMESPACE::globalThreadCount() << std::endl;
  }

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

  boost::system::error_code ec;
  boost::asio::ssl::context tls(boost::asio::ssl::context::sslv23);

  tls.use_private_key_file("ssl/server.key", boost::asio::ssl::context::pem);
  tls.use_certificate_chain_file("ssl/server.crt");

  if (configure_tls_context_easy(ec, tls)) {
    std::cerr << "error: " << ec.message() << std::endl;
  }

  int no_threads = MAX(std::thread::hardware_concurrency() / 2, 1);

  http2_server = new http2();
  http2_server->num_threads(no_threads);

#ifdef LOCAL
  http2_server->handle(
      "/get_directory", [](const request &req, const response &res) {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string dir;
        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key == "dir") {
              dir = value;
            }
          }
        }

        if (dir != "")
          serve_directory(&res, dir);
        else
          serve_directory(&res, home_dir);
      });
#endif

  http2_server->handle("/", [](const request &req, const response &res) {
    boost::system::error_code ec;

    auto uri = req.uri().path;

    if (uri == "/") {
      auto push = res.push(ec, "GET", "/favicon.ico");
      serve_file(&req, push, "/favicon.ico");

#ifdef LOCAL
      push = res.push(ec, "GET", "/local.css");
      serve_file(&req, push, "/local.css");

      push = res.push(ec, "GET", "/local.js");
      serve_file(&req, push, "/local.js");

      push = res.push(ec, "GET", "/logo_naoj_all_s.png");
      serve_file(&req, push, "/logo_naoj_all_s.png");

      serve_file(&req, &res, "/local.html");
#else
      push = res.push(ec, "GET", "/test.css");
      serve_file(&req, push, "/test.css");

      push = res.push(ec, "GET", "/test.js");
      serve_file(&req, push, "/test.js");

      serve_file(&req, &res, "/test.html");
#endif
    } else {
      std::cout << uri << std::endl;

      if (uri.find("get_progress/") != std::string::npos) {
        size_t pos = uri.find_last_of("/");

        if (pos != std::string::npos) {
          std::string datasetid = uri.substr(pos + 1, std::string::npos);

          // process the response
          std::cout << "progress(" << datasetid << ")" << std::endl;

          auto fits = get_dataset(datasetid);

          if (fits == nullptr)
            return http_not_found(&res);
          else {
            if (fits->has_error)
              return http_not_found(&res);
            else {
              // make json
              std::ostringstream json;
              bool valid = false;
              {
                std::shared_lock<std::shared_mutex> lock(fits->progress_mtx);

                json << "{\"total\" : " << fits->progress.total << ",";
                json << "\"running\" : " << fits->progress.running << ",";
                json << "\"elapsed\" : " << fits->progress.elapsed << "}";

                if (fits->progress.total > 0)
                  valid = true;
              }

              if (valid) {
                header_map mime;
                mime.insert(std::pair<std::string, header_value>(
                    "Content-Type", {"application/json", false}));
                res.write_head(200, mime);
                res.end(json.str());
              } else
                http_accepted(&res);

              return;
            }
          }
        } else
          return http_not_found(&res);
      }

      if (uri.find("progress/") != std::string::npos) {
        size_t pos = uri.find_last_of("/");

        if (pos != std::string::npos) {
          std::string datasetid = uri.substr(pos + 1, std::string::npos);

          // process the response
          std::cout << "progress(" << datasetid << ")" << std::endl;

          auto fits = get_dataset(datasetid);

          if (fits == nullptr)
            return http_not_found(&res);
          else {
            if (fits->has_error)
              return http_not_found(&res);
            else {
              header_map mime;

              mime.insert(std::pair<std::string, header_value>(
                  "Content-Type", {"text/event-stream", false}));
              mime.insert(std::pair<std::string, header_value>(
                  "Cache-Control", {"no-cache", false}));
              res.write_head(200, mime);
              res.end(progress_generator(fits));

              return;
            }
          }
        } else
          return http_not_found(&res);
      }

      if (uri.find("/get_molecules") != std::string::npos) {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        bool compress = false;

        // check for compression
        header_map headers = req.header();

        auto it = headers.find("accept-encoding");
        if (it != headers.end()) {
          auto value = it->second.value;
          // std::cout << "Supported compression: " << value << std::endl;

          size_t pos = value.find("gzip");

          if (pos != std::string::npos) {
            compress = true;
          }
        }

        std::string datasetid;
        double freq_start = 0.0;
        double freq_end = 0.0;

        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key.find("dataset") != std::string::npos) {
              datasetid = value;
            }

            if (key.find("freq_start") != std::string::npos)
              freq_start = std::stod(value) / 1.0E9; //[Hz -> GHz]

            if (key.find("freq_end") != std::string::npos)
              freq_end = std::stod(value) / 1.0E9; //[Hz -> GHz]
          }
        }

        if (FPzero(freq_start) || FPzero(freq_end)) {
          // get the frequency range from the FITS header
          auto fits = get_dataset(datasetid);

          if (fits == nullptr)
            return http_not_found(&res);
          else {
            if (fits->has_error)
              return http_not_found(&res);

            /*std::unique_lock<std::mutex> header_lck(fits->header_mtx);
            while (!fits->processed_header)
              fits->header_cv.wait(header_lck);*/

            if (!fits->has_header)
              return http_accepted(&res);
            // return http_not_found(&res);

            if (fits->depth <= 1 || !fits->has_frequency)
              return http_not_implemented(&res);

            // extract the freq. range
            fits->get_frequency_range(freq_start, freq_end);
          }
        }

        // process the response
        std::cout << "get_molecules(" << datasetid << "," << freq_start
                  << "GHz," << freq_end << "GHz)" << std::endl;

        if (!FPzero(freq_start) && !FPzero(freq_end))
          return stream_molecules(&res, freq_start, freq_end, compress);
        else
          return http_not_implemented(&res);
      }

      if (uri.find("/heartbeat") != std::string::npos) {
        res.write_head(200);
        res.end("N/A");
        return;
      }

      if (uri.find("/get_image") != std::string::npos) {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string datasetid;
        int width = 0;
        int height = 0;

        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key.find("dataset") != std::string::npos) {
              datasetid = value;
            }

            if (key.find("width") != std::string::npos) {
              width = std::stoi(value);
            }

            if (key.find("height") != std::string::npos) {
              height = std::stoi(value);
            }
          }
        }

        // process the response
        std::cout << "get_image(" << datasetid << "::" << width
                  << "::" << height << ")" << std::endl;

        auto fits = get_dataset(datasetid);

        if (fits == nullptr)
          return http_not_found(&res);
        else {
          if (fits->has_error)
            return http_not_found(&res);
          else {
            /*std::unique_lock<std::mutex> data_lock(fits->data_mtx);
            while (!fits->processed_data)
              fits->data_cv.wait(data_lock);*/

            if (!fits->has_data)
              // return http_not_found(&res);
              return http_accepted(&res);
            else
              // return http_not_implemented(&res);
              return stream_image(&res, fits, width, height);
          }
        }
      }

      if (uri.find("/get_spectrum") != std::string::npos) {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string datasetid;

        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key.find("dataset") != std::string::npos) {
              datasetid = value;
            }
          }
        }

        // process the response
        std::cout << "get_spectrum(" << datasetid << ")" << std::endl;

        auto fits = get_dataset(datasetid);

        if (fits == nullptr)
          return http_not_found(&res);
        else {
          if (fits->has_error)
            return http_not_found(&res);
          else {
            /*std::unique_lock<std::mutex> data_lock(fits->data_mtx);
            while (!fits->processed_data)
              fits->data_cv.wait(data_lock);*/

            if (!fits->has_data)
              // return http_not_found(&res);
              return http_accepted(&res);
            else
              return get_spectrum(&res, fits);
          }
        }
      }

      // FITSWebQL entry
      if (uri.find("FITSWebQL.html") != std::string::npos) {
        auto push = res.push(ec, "GET", "/favicon.ico");
        serve_file(&req, push, "/favicon.ico");

        push = res.push(ec, "GET", "/fitswebql/fitswebql.css?" VERSION_STRING);
        serve_file(&req, push, "/fitswebql/fitswebql.css");

        push = res.push(ec, "GET", "/fitswebql/fitswebql.js?" VERSION_STRING);
        serve_file(&req, push, "/fitswebql/fitswebql.js");

        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::vector<std::string> datasets;
        std::string dir, ext, db, table, flux;
        bool composite = false;

        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key.find("dataset") != std::string::npos) {
              datasets.push_back(value);
            }

            if (key.find("filename") != std::string::npos) {
              datasets.push_back(value);
            }

            if (key == "dir") {
              dir = value;
            }

            if (key == "ext") {
              ext = value;
            }

            if (key == "db")
              db = value;

            if (key == "table")
              table = value;

            if (key == "flux") {
              // validate the flux value
              std::set<std::string> valid_values;
              valid_values.insert("linear");
              valid_values.insert("logistic");
              valid_values.insert("ratio");
              valid_values.insert("square");
              valid_values.insert("legacy");

              if (valid_values.find(value) != valid_values.end())
                flux = value;
            }

            if (key == "view") {
              if (value.find("composite") != std::string::npos)
                composite = true;
            }
          }
        }

        // sane defaults
        {
          if (db.find("hsc") != std::string::npos) {
            flux = "ratio";
          }

          if (table.find("fugin") != std::string::npos)
            flux = "logistic";
        }

        std::cout << "dir:" << dir << ", ext:" << ext << ", db:" << db
                  << ", table:" << table << ", composite:" << composite
                  << ", flux:" << flux << ", ";
        for (auto const &dataset : datasets)
          std::cout << dataset << " ";
        std::cout << std::endl;

        if (datasets.size() == 0) {
          return http_not_found(&res);
        } else
          return execute_fits(&res, dir, ext, db, table, datasets, composite,
                              flux);
      }

      serve_file(&req, &res, uri);
    }
  });

  signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  if (http2_server->listen_and_serve(ec, tls, "0.0.0.0",
                                     std::to_string(HTTPS_PORT), true)) {
    std::cerr << "error: " << ec.message() << std::endl;
  }

  http2_server->join();
  delete http2_server;

  // cleanup and close up stuff here
  // terminate program

  if (splat_db != NULL)
    sqlite3_close(splat_db);

  std::cout << "Terminating FITSWebQL..." << std::endl;
}
