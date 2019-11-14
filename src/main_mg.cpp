#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define SERVER_PORT 8080
#define SERVER_STRING                                                          \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-11-14.0"
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

#include <cstdint>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if !defined(__APPLE__) || !defined(__MACH__)
#include <bsd/string.h>
#endif

static bool is_gzip(const char *filename) {
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

#include "fits.hpp"
#include "json.h"
#include "mongoose.h"

#include <atomic>
#include <iostream>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <thread>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <curl/curl.h>
#include <sqlite3.h>

#ifndef LOCAL
#include <pgsql/libpq-fe.h>

#define FITSHOME "/home"
#define JVO_HOST "localhost"
#define JVO_USER "jvo"

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

sqlite3 *splat_db = NULL;
std::string home_dir;

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

#include <ipp.h>

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
  /*status = ippGetCpuFeatures(&mask, 0);
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
  }*/
}

inline const char *check_null(const char *str) {
  if (str != NULL)
    return str;
  else
    return ""; //"\"\"" ;
};

static volatile sig_atomic_t s_received_signal = 0;
static const char *s_http_port = "8080";
static const int s_num_worker_threads = 4;
static unsigned long s_next_id = 0;
struct mg_mgr mgr;

static void signal_handler(int sig_num) {
  printf("Interrupt signal [%d] received.\n", sig_num);

  signal(sig_num, signal_handler);
  s_received_signal = sig_num;

#ifdef CLUSTER
  exiting = true;
#endif
}
static struct mg_serve_http_opts s_http_server_opts;
static sock_t sock[2];

static int is_websocket(const struct mg_connection *nc) {
  return nc->flags & MG_F_IS_WEBSOCKET;
}

void mg_http_send_error_keep_alive(struct mg_connection *nc, int code,
                                   const char *reason) {
  if (reason == NULL)
    reason = "N/A";

  mg_send_head(nc, code, strlen(reason), "Content-Type: text/plain");
  mg_send(nc, reason, strlen(reason));
}

struct molecules_request {
  double freq_start;
  double freq_end;
  bool compress;
};

// This info is passed to the worker thread
struct work_request {
  unsigned long
      conn_id; // needed to identify the connection where to send the reply
  // optionally, more data that could be required by worker
  char *dataId;
  struct molecules_request *req;
};

// This info is passed by the worker thread to mg_broadcast
struct work_result_err {
  unsigned long conn_id;
  int code;
};

// This info is passed by the worker thread to mg_broadcast
struct work_result_header {
  unsigned long conn_id;
  bool compress;
};

// This info is passed by the worker thread to mg_broadcast
struct work_result_chunk {
  unsigned long conn_id;
  char *chunk;
  size_t len;
};

static void on_error_complete(struct mg_connection *nc, int ev, void *ev_data) {
  (void)nc;
  (void)ev_data;

  if (ev == MG_EV_POLL) {
    struct work_result_err *res = (struct work_result_err *)ev_data;

    if (res != NULL) {
      printf("conn. id %zu, sending an error code %d; cross-check: %zu\n",
             res->conn_id, res->code, (unsigned long)nc->user_data);

      if (res->conn_id == (unsigned long)nc->user_data)
        mg_http_send_error_keep_alive(nc, res->code, NULL);
    }
  }
}

static void on_header_complete(struct mg_connection *nc, int ev,
                               void *ev_data) {
  (void)nc;
  (void)ev_data;

  if (ev == MG_EV_POLL) {
    struct work_result_header *res = (struct work_result_header *)ev_data;

    if (res != NULL) {
      printf("conn. id %zu, sending a chunked header, compression: %d; "
             "cross-check: %zu\n",
             res->conn_id, res->compress, (unsigned long)nc->user_data);

      if (res->conn_id == (unsigned long)nc->user_data) {
        if (res->compress)
          mg_send_head(nc, 200, -1,
                       "Content-Type: application/json\r\nCache-Control: "
                       "no-cache\r\nContent-Encoding: gzip");
        else
          mg_send_head(
              nc, 200, -1,
              "Content-Type: application/json\r\nCache-Control: no-cache");
      }
    }
  }
}

static void on_chunk_complete(struct mg_connection *nc, int ev, void *ev_data) {
  (void)nc;
  (void)ev_data;

  struct work_result_chunk *res = (struct work_result_chunk *)ev_data;

  if (ev == MG_EV_POLL) {
    if (res != NULL) {
      printf("conn. id %zu, sending a chunk of length %zu; cross-check: %zu\n",
             res->conn_id, res->len, (unsigned long)nc->user_data);

      if (res->conn_id == (unsigned long)nc->user_data)
        if (res->chunk != NULL) {
          mg_send_http_chunk(nc, res->chunk, res->len);
          free(res->chunk);
          res->chunk = NULL;
        } else
          mg_send_http_chunk(nc, "", 0);
    }
  }
}

void broadcast_error(unsigned long conn_id, int code) {
  struct work_result_err res = {conn_id, code};
  mg_broadcast(&mgr, on_error_complete, (void *)&res, sizeof(res));
}

void broadcast_header(unsigned long conn_id, bool compress) {
  struct work_result_header res = {conn_id, compress};
  mg_broadcast(&mgr, on_header_complete, (void *)&res, sizeof(res));
}

void broadcast_chunk(unsigned long conn_id, const char *data, size_t len) {
  char *chunk = NULL;

  if (data != NULL)
    chunk = (char *)malloc(len);

  struct work_result_chunk res = {conn_id, chunk, len};
  mg_broadcast(&mgr, on_chunk_complete, (void *)&res, sizeof(res));
}

struct MolecularStream {
  bool first;
  bool compress;
  unsigned long conn_id;
  z_stream z;
  unsigned char out[CHUNK];
  FILE *fp;
};

static int sqlite_callback(void *userp, int argc, char **argv,
                           char **azColName) {
  MolecularStream *stream = (MolecularStream *)userp;
  // static long counter = 0;
  // printf("sqlite_callback: %ld, argc: %d\n", counter++, argc);

  if (argc == 8) {
    /*printf("sqlite_callback::molecule:\t");
      for (int i = 0; i < argc; i++)
      printf("%s:%s\t", azColName[i], argv[i]);
      printf("\n");*/

    std::string json;

    if (stream->first) {
      stream->first = false;
      broadcast_header(stream->conn_id, stream->compress);

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

          broadcast_chunk(stream->conn_id, (char *)stream->out, have);
        }
      } while (stream->z.avail_out == 0);
    } else
      broadcast_chunk(stream->conn_id, json.c_str(), json.length());
  }

  return 0;
}

void stream_molecules(unsigned long conn_id, double freq_start, double freq_end,
                      bool compress) {
  if (splat_db == NULL)
    return broadcast_error(conn_id, 500);

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
  stream.conn_id = conn_id;
  stream.fp = NULL; // fopen("molecules.txt.gz", "w");

  if (compress) {
    stream.z.zalloc = Z_NULL;
    stream.z.zfree = Z_NULL;
    stream.z.opaque = Z_NULL;
    stream.z.next_in = Z_NULL;
    stream.z.avail_in = 0;

    CALL_ZLIB(deflateInit2(&stream.z, Z_BEST_COMPRESSION, Z_DEFLATED,
                           windowBits | GZIP_ENCODING, 9, Z_DEFAULT_STRATEGY));
  }

  rc = sqlite3_exec(splat_db, strSQL, sqlite_callback, &stream, &zErrMsg);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
    return broadcast_error(conn_id, 500);
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

        broadcast_chunk(conn_id, (char *)stream.out, have);
      }
    } while (stream.z.avail_out == 0);

    CALL_ZLIB(deflateEnd(&stream.z));

    if (stream.fp != NULL)
      fclose(stream.fp);
  } else
    broadcast_chunk(conn_id, chunk_data.c_str(), chunk_data.length());

  // end of chunked encoding
  broadcast_chunk(conn_id, NULL, 0);
}

void *worker_thread_proc(void *param) {
  struct mg_mgr *mgr = (struct mg_mgr *)param;
  struct work_request req = {0, NULL, NULL};

  while (s_received_signal == 0) {
    if (read(sock[1], &req, sizeof(req)) < 0) {
      // if(s_received_signal == 0)
      perror("Reading worker sock");
    } else {
      if (req.dataId != NULL) {
        printf("handling %s\n", req.dataId);

        // stream molecules
        if (req.req != NULL) {
          double freq_start = req.req->freq_start;
          double freq_end = req.req->freq_end;

          if (FPzero(freq_start) || FPzero(freq_end)) {
            // get the frequency range from the FITS header
            auto fits = get_dataset(req.dataId);

            if (fits == nullptr)
              broadcast_error(req.conn_id, 404);
            else {
              if (fits->has_error)
                broadcast_error(req.conn_id, 404);
              else {
                std::unique_lock<std::mutex> header_lck(fits->header_mtx);
                while (!fits->processed_header)
                  fits->header_cv.wait(header_lck);

                if (!fits->has_header)
                  broadcast_error(req.conn_id, 404);
                else {
                  if (fits->depth <= 1 || !fits->has_frequency)
                    broadcast_error(req.conn_id, 501);
                  else
                    // extract the freq. range
                    fits->get_frequency_range(freq_start, freq_end);
                }
              }
            }
          }

          // process the response
          PrintThread{} << "get_molecules(" << req.dataId << ", " << freq_start
                        << " GHz," << freq_end << " GHz)" << std::endl;

          if (!FPzero(freq_start) && !FPzero(freq_end))
            stream_molecules(req.conn_id, freq_start, freq_end, compress);
          else
            broadcast_error(req.conn_id, 501);
        }

        free(req.dataId);
        req.dataId = NULL;
      }

      if (req.req != NULL) {
        free(req.req);
        req.req = NULL;
      }
    }
  }

  printf("worker_thread_proc terminated.\n");
  return NULL;
}

#ifdef LOCAL
static void get_directory(struct mg_connection *nc, const char *dir) {
  printf("get_directory(%s)\n", check_null(dir));

  struct dirent **namelist = NULL;
  int i, n;

  n = scandir(dir, &namelist, 0, alphasort);

  std::ostringstream json;

  char *encoded = json_encode_string(check_null(dir));

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

      sprintf(pathname, "%s/%s", dir, check_null(namelist[i]->d_name));

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

  mg_send_head(nc, 200, json.tellp(),
               "Content-Type: application/json\r\nCache-Control: no-cache");
  mg_send(nc, json.str().c_str(), json.tellp());
}
#endif

static void http_fits_response(struct mg_connection *nc,
                               std::vector<std::string> datasets,
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
  html.append(
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, "
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

#ifdef PRODUCTION
  html.append(R"(<script>
        var WS_SOCKET = 'wss://';
        </script>)");
#else
  html.append(R"(<script>
        var WS_SOCKET = 'ws://';
        </script>)");
#endif

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

  mg_send_head(nc, 200, html.size(),
               "Content-Type: text/html\r\nCache-Control: no-cache");
  mg_send(nc, html.c_str(), html.size());
}

static void execute_fits(struct mg_connection *nc, const char *dir,
                         const char *ext, const char *db, const char *table,
                         std::vector<std::string> datasets, bool composite,
                         const char *flux) {
  bool has_fits = true;

#ifndef LOCAL
  PGconn *jvo_db = NULL;

  if (strcmp(db, "") != 0)
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

      if (strcmp(dir, "") != 0 && strcmp(ext, "") != 0)
        path = std::string(dir) + "/" + data_id + "." + std::string(ext);

#ifndef LOCAL
      if (jvo_db != NULL && strcmp(table, "") != 0)
        path = get_jvo_path(jvo_db, db, table, data_id);
#endif

      if (path != "") {
        bool is_compressed = is_gzip(path.c_str());

        // load FITS data in a separate thread
        std::thread(&FITS::from_path_zfp, fits, path, is_compressed,
                    std::string(flux), va_count)
            .detach();
      } else {
        // the last resort
        std::string url = std::string("http://") + JVO_FITS_SERVER +
                          ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB +
                          "&table=cube&data_id=" + data_id + "_00_00_00";

        // download FITS data from a URL in a separate thread
        std::thread(&FITS::from_url, fits, url, std::string(flux), va_count)
            .detach();
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

  PrintThread{} << "has_fits: " << has_fits << std::endl;

  return http_fits_response(nc, datasets, composite, has_fits);
}

static void get_spectrum(struct mg_connection *nc, std::shared_ptr<FITS> fits) {
  std::ostringstream json;

  fits->to_json(json);

  if (json.tellp() > 0) {
    mg_send_head(nc, 200, json.tellp(),
                 "Content-Type: application/json\r\nCache-Control: no-cache");
    mg_send(nc, json.str().c_str(), json.tellp());
  } else
    mg_http_send_error_keep_alive(nc, 501, "Not Implemented");
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
  (void)nc;
  (void)ev_data;

  switch (ev) {
  case MG_EV_ACCEPT:
    break;
  case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST: {
    struct http_message *hm = (struct http_message *)ev_data;
    printf("WEBSOCKET URI:\t%.*s\n", (int)hm->uri.len, hm->uri.p);
    break;
  }
  case MG_EV_WEBSOCKET_FRAME: {
    struct websocket_message *wm = (struct websocket_message *)ev_data;

    if (wm->data != NULL && wm->size > 0) {
      printf("[WS]:\t%.*s\n", (int)wm->size, wm->data);
    }

    break;
  }
  case MG_EV_HTTP_REQUEST: {
    struct http_message *hm = (struct http_message *)ev_data;
    printf("URI:\t%.*s\n", (int)hm->uri.len, hm->uri.p);

#ifdef LOCAL
    // get_directory
    if (strnstr(hm->uri.p, "/get_directory", hm->uri.len) != NULL) {
      char dir[1024] = "";
      struct mg_str query = hm->query_string;

      if (query.len > 0) {
        printf("%.*s\n", (int)query.len, query.p);

        if (mg_get_http_var(&query, "dir", dir, sizeof(dir) - 1) > 0)
          printf("dir: "
                 "%s"
                 "\n",
                 dir);
      }

      // return a json with a directory listing
      if (strcmp(dir, "") == 0)
        return get_directory(nc, home_dir.c_str());
      else
        return get_directory(nc, dir);
    }
#endif

    if (strnstr(hm->uri.p, "/get_molecules", hm->uri.len) != NULL) {
      struct mg_str query = hm->query_string;

      if (query.len > 0) {
        printf("%.*s\n", (int)query.len, query.p);

        char datasetid[256] = "";
        char freq_start_str[256] = "";
        char freq_end_str[256] = "";
        double freq_start = 0.0;
        double freq_end = 0.0;
        bool compress = false;

        struct mg_str *hdr = mg_get_http_header(hm, "accept-encoding");

        if (hdr != NULL) {
          if (strnstr(hdr->p, "gzip", hdr->len) != NULL)
            compress = true;
        }

        PrintThread{} << "Accept-Encoding:"
                      << std::string_view(hdr->p, hdr->len)
                      << "; compression support " << (compress ? "" : "not ")
                      << "found." << std::endl;

        mg_get_http_var(&query, "datasetId", datasetid, sizeof(datasetid));

        if (mg_get_http_var(&query, "freq_start", freq_start_str,
                            sizeof(freq_start_str) - 1) > 0)
          freq_start = atof(freq_start_str) / 1.0E9; //[Hz -> GHz]

        if (mg_get_http_var(&query, "freq_end", freq_end_str,
                            sizeof(freq_end_str) - 1) > 0)
          freq_end = atof(freq_end_str) / 1.0E9; //[Hz -> GHz]

        nc->user_data = (void *)++s_next_id;
        printf("MG_EV_HTTP_REQUEST conn_id = %zu\n", s_next_id);

        struct molecules_request *m_req = (struct molecules_request *)malloc(
            sizeof(struct molecules_request));

        if (m_req != NULL) {
          m_req->freq_start = freq_start;
          m_req->freq_end = freq_end;
          m_req->compress = compress;

          struct work_request req = {(unsigned long)nc->user_data,
                                     strdup(datasetid), m_req};

          if (write(sock[0], &req, sizeof(req)) < 0)
            perror("Writing worker sock");
          else
            break;
        }
      }
    }

    if (strnstr(hm->uri.p, "/get_spectrum", hm->uri.len) != NULL) {
      struct mg_str query = hm->query_string;

      if (query.len > 0) {
        printf("%.*s\n", (int)query.len, query.p);

        char datasetid[256] = "";

        if (mg_get_http_var(&query, "datasetId", datasetid,
                            sizeof(datasetid) - 1) > 0) {
          auto fits = get_dataset(datasetid);

          if (fits == nullptr) {
            mg_http_send_error_keep_alive(nc, 404, "Not Found");
            break;
          } else {
            if (fits->has_error) {
              mg_http_send_error_keep_alive(nc, 404, "Not Found");
              break;
            } else {
              /*std::unique_lock<std::mutex> data_lock(
                                                                                                       fits->data_mtx);
                                                                while
                 (!fits->processed_data) fits->data_cv.wait(data_lock);*/

              if (!fits->has_data) {
                // mg_http_send_error_keep_alive(nc, 404, "Not Found");
                mg_http_send_error_keep_alive(nc, 202, "Accepted");
                break;
              } else
                return get_spectrum(nc, fits);
            }
          }
        }
      }
    }

    if (strnstr(hm->uri.p, "FITSWebQL.html", hm->uri.len) != NULL) {
      struct mg_str query = hm->query_string;

      if (query.len > 0) {
        printf("%.*s\n", (int)query.len, query.p);

        std::vector<std::string> datasets;
        char dir[1024] = "";
        char ext[256] = "";
        char db[256] = "";
        char table[256] = "";
        char flux[256] = "";
        char tmp[256] = "";
        bool composite = false;

#ifdef LOCAL
        char pattern[] = "filename";
#else
        char pattern[] = "datasetId";
#endif

        // first try to find the main pattern
        if (mg_get_http_var(&query, pattern, tmp, sizeof(tmp) - 1) > 0)
          datasets.push_back(std::string(tmp));
        else {
          // iterate through multiple patterns, starting with '1'
          int count = 0;
          strcat(pattern, std::to_string(++count).c_str());

          while (mg_get_http_var(&query, pattern, tmp, sizeof(tmp) - 1) > 0) {
            datasets.push_back(std::string(tmp));
            size_t len = strlen(pattern);
            pattern[len - 1] = '\0';
            strcat(pattern, std::to_string(++count).c_str());
          }
        }

        mg_get_http_var(&query, "dir", dir, sizeof(dir) - 1);
        mg_get_http_var(&query, "ext", ext, sizeof(ext) - 1);
        mg_get_http_var(&query, "db", db, sizeof(db) - 1);
        mg_get_http_var(&query, "table", table, sizeof(table) - 1);
        mg_get_http_var(&query, "flux", tmp, sizeof(tmp) - 1);

        // validate the flux value
        if (strcmp(tmp, "linear") == 0 || strcmp(tmp, "logistic") == 0 ||
            strcmp(tmp, "ratio") == 0 || strcmp(tmp, "square") == 0 ||
            strcmp(tmp, "legacy") == 0)
          strcpy(flux, tmp);

        mg_get_http_var(&query, "view", tmp, sizeof(tmp) - 1);
        if (strcmp(tmp, "composite") == 0)
          composite = true;

        // sane defaults
        if (strstr(db, "hsc") != NULL)
          strcpy(flux, "ratio");

        if (strstr(table, "fugin") != NULL)
          strcpy(flux, "logistic");

        /*PrintThread{} << "dir:" << dir << ", ext:" << ext
                                                                                                                << ", db:" << db << ", table:" << table
                                                                                                                << ", composite:" << composite
                                                                                                                << ", flux:" << flux << ", ";
                                      for (auto const &dataset : datasets)
                                        std::cout << dataset << " ";
                                      std::cout << std::endl;*/

        if (datasets.size() > 0)
          return execute_fits(nc, dir, ext, db, table, datasets, composite,
                              flux);
      }

      mg_http_send_error_keep_alive(nc, 404, "Not Found");
      break;
    }

    mg_serve_http(nc, hm, s_http_server_opts);
    break;
  }
  case MG_EV_CLOSE: {
    if (is_websocket(nc) && nc->user_data != NULL) {
      printf("closing a websocket connection for %s\n", (char *)nc->user_data);
    }
  }
  default:
    break;
  }
}

int main(void) {
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

  struct mg_connection *nc;
  int i;

  if (mg_socketpair(sock, SOCK_STREAM) == 0) {
    perror("Opening socket pair");
    exit(1);
  }

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

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

  mg_mgr_init(&mgr, NULL);

  nc = mg_bind(&mgr, s_http_port, ev_handler);
  if (nc == NULL) {
    printf("Failed to create listener\n");
    return 1;
  }

  mg_set_protocol_http_websocket(nc);
  s_http_server_opts.document_root = "htdocs_mg"; // Serve current directory
  s_http_server_opts.enable_directory_listing = "no";
#ifdef LOCAL
  s_http_server_opts.index_files = "local.html";
#else
  s_http_server_opts.index_files = "test.html";
#endif
  s_http_server_opts.custom_mime_types =
      ".txt=text/plain,.html=text/html,.js=application/javascript,.ico=image/"
      "x-icon,.png=image/png,.gif=image/gif,.webp=image/webp,.jpg=image/"
      "jpeg,.jpeg=image/jpeg,.bpg=image/bpg,.mp4=video/mp4,.hevc=video/"
      "hevc,.css=text/css,.pdf=application/pdf,.svg=image/"
      "svg+xml,.wasm=application/wasm";

  for (i = 0; i < s_num_worker_threads; i++) {
    mg_start_thread(worker_thread_proc, &mgr);
  }

  printf("FITSWebQL: started on port %s\n", s_http_port);
  while (s_received_signal == 0) {
    mg_mgr_poll(&mgr, 200);
  }

  mg_mgr_free(&mgr);

  // no need to call shutdown() as close will do it for us
  close(sock[0]);
  close(sock[1]);

  printf("FITSWebQL: clean shutdown completed\n");

  curl_global_cleanup();

  if (splat_db != NULL)
    sqlite3_close(splat_db);

#ifdef CLUSTER
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

  return 0;
}
