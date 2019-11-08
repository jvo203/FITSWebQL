#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define SERVER_PORT 8080
#define SERVER_STRING							\
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-11-08.0"
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

#define CALL_ZLIB(x)							\
  {									\
    int status;								\
    status = x;								\
    if (status < 0) {							\
      fprintf(stderr, "%s:%d: %s returned a bad status of %d.\n", __FILE__, \
              __LINE__, #x, status);					\
      /*exit(EXIT_FAILURE);*/						\
    }									\
  }

#include <pwd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <bsd/string.h>

#include "mongoose.h"

#include <curl/curl.h>
#include <sqlite3.h>

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

inline const char* check_null(const char* str)
{
  if(str != NULL)
    return str ;
  else
    return "";//"\"\"" ;
} ;

static volatile sig_atomic_t s_received_signal = 0;
static const char *s_http_port = "8080";
static const int s_num_worker_threads = 4;
static unsigned long s_next_id = 0;
struct mg_mgr mgr;

static void signal_handler(int sig_num) {
  signal(sig_num, signal_handler);
  s_received_signal = sig_num;
}
static struct mg_serve_http_opts s_http_server_opts;
static sock_t sock[2];

static int is_websocket(const struct mg_connection *nc) {
  return nc->flags & MG_F_IS_WEBSOCKET;
}

// This info is passed to the worker thread
struct work_request {
  unsigned long conn_id;  // needed to identify the connection where to send the reply
  // optionally, more data that could be required by worker
  char* dataId;
};

void *worker_thread_proc(void *param)
{
  struct mg_mgr *mgr = (struct mg_mgr *) param;
  struct work_request req = {0, NULL};
  
  while (s_received_signal == 0) {
    if (read(sock[1], &req, sizeof(req)) < 0)
      {
	//if(s_received_signal == 0)
	perror("Reading worker sock");
      }
    else
      {	
       printf("handling %s\n", req.dataId);
      }
  }
  
  printf("worker_thread_proc terminated.\n");
  return NULL;
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
  (void) nc;
  (void) ev_data;

  switch (ev) {
    case MG_EV_ACCEPT:      
      break;
    case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST: {
      struct http_message *hm = (struct http_message *) ev_data;
      printf("WEBSOCKET URI:\t%.*s\n", (int) hm->uri.len, hm->uri.p);
      break;
    }
    case MG_EV_WEBSOCKET_FRAME: {
      struct websocket_message *wm = (struct websocket_message *) ev_data;

      if(wm->data != NULL && wm->size > 0)
	{
	  printf("[WS]:\t%.*s\n", (int) wm->size, wm->data);
       }

       break;
    }
    case MG_EV_HTTP_REQUEST: {
      struct http_message *hm = (struct http_message *) ev_data;
      printf("URI:\t%.*s\n", (int) hm->uri.len, hm->uri.p);

      //serve the root index file
      if(strnstr(hm->uri.p, "/", hm->uri.len) != NULL)
      {
#ifdef LOCAL
       mg_http_serve_file(c, hm, "htdocs_mg/local.html", mg_mk_str("text/html"), mg_mk_str(""));
#else
	mg_http_serve_file(c, hm, "htdocs_mg/test.html", mg_mk_str("text/html"), mg_mk_str(""));
#endif
      }
      else mg_serve_http(nc, hm, s_http_server_opts);

      break;
    }
    case MG_EV_CLOSE: {
       if(is_websocket(nc) && nc->user_data != NULL)
	  {
	    printf("closing a websocket connection identified by %s\n", (char*) nc->user_data) ;
         }
      }
    default:
       break;
  }
}

int main(void) {  
  struct mg_connection *nc;
  int i;

  if (mg_socketpair(sock, SOCK_STREAM) == 0) {
    perror("Opening socket pair");
    exit(1);
  }

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  
  ipp_init();
  curl_global_init(CURL_GLOBAL_ALL) ;
  
  mg_mgr_init(&mgr, NULL);

  nc = mg_bind(&mgr, s_http_port, ev_handler);
  if (nc == NULL) {
    printf("Failed to create listener\n");
    return 1;
  }

  mg_set_protocol_http_websocket(nc);
  s_http_server_opts.document_root = "./htdocs_mg";  // Serve current directory
  s_http_server_opts.enable_directory_listing = "no";

  for (i = 0; i < s_num_worker_threads; i++) {
    mg_start_thread(worker_thread_proc, &mgr);
  }

  printf("FITSWebQL: started on port %s\n", s_http_port);
  while (s_received_signal == 0) {
    mg_mgr_poll(&mgr, 200);
  }

  mg_mgr_free(&mgr);

  //no need to call shutdown() as close will do it for us    
  close(sock[0]);
  close(sock[1]);


  printf("FITSWebQL: clean shutdown completed\n");
  
  curl_global_cleanup() ;
  
  return 0;
}