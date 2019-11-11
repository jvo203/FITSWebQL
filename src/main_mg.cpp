#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define SERVER_PORT 8080
#define SERVER_STRING							\
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-11-11.0"
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
#include <string.h>

#if !defined(__APPLE__) || !defined(__MACH__)
#include <bsd/string.h>
#endif

#include "mongoose.h"
#include "json.h"
#include "fits.hpp"

#include <atomic>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <curl/curl.h>
#include <sqlite3.h>

sqlite3 *splat_db = NULL;
std::string home_dir;

#ifdef CLUSTER
#include <czmq.h>

inline std::set<std::string> cluster;
inline std::shared_mutex cluster_mtx;

inline bool cluster_contains_node(std::string node)
{
  std::shared_lock<std::shared_mutex> lock(cluster_mtx);

  if(cluster.find(node) == cluster.end())
    return false;
  else
    return true;
}

inline void cluster_insert_node(std::string node)
{
  std::lock_guard<std::shared_mutex> guard(cluster_mtx);
  cluster.insert(node);
}

inline void cluster_erase_node(std::string node)
{
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

#ifdef LOCAL
static void get_directory(struct mg_connection *nc, const char* dir)
{
 printf("get_directory(%s)\n", check_null(dir)) ;
  
    struct dirent **namelist = NULL ;
    int i,n ;

    n = scandir(dir, &namelist, 0, alphasort);
  
    std::ostringstream json;
  
    char* encoded = json_encode_string(check_null(dir)) ;
    
    json << "{\"location\" : " << check_null(encoded) << ", \"contents\" : [" ;

    if(encoded != NULL)
      free(encoded) ;
  
    bool has_contents = false ;
  
    if (n < 0)
      {
	perror("scandir");      
      
	json << "]}" ;
      }
    else
      {            
	for (i = 0; i < n; i++)
	  {
	    //printf("%s\n", namelist[i]->d_name);

	    char pathname[1024] ;

	    sprintf(pathname, "%s/%s", dir, check_null(namelist[i]->d_name)) ;	      
	      
	    struct stat64 sbuf;

	    int err = stat64(pathname, &sbuf) ;

	    if(err == 0)
	      {
		char last_modified[255] ;

		struct tm lm ;
		localtime_r(&sbuf.st_mtime, &lm);
		strftime(last_modified, sizeof(last_modified)-1, "%a, %d %b %Y %H:%M:%S %Z", &lm) ;
		  
		size_t filesize = sbuf.st_size;	  

		if( S_ISDIR(sbuf.st_mode) && namelist[i]->d_name[0] != '.')
		  {
		    char* encoded = json_encode_string(check_null(namelist[i]->d_name)) ;
		  		  
		    json << "{\"type\" : \"dir\", \"name\" : " << check_null(encoded) << ", \"last_modified\" : \"" << last_modified << "\"}," ;
		    has_contents = true ;
		  
		    if(encoded != NULL)
		      free(encoded) ;
		  }

		if( S_ISREG(sbuf.st_mode) ) {
        const std::string filename = std::string(namelist[i]->d_name);
        const std::string lower_filename = boost::algorithm::to_lower_copy(filename);

        //if(!strcasecmp(get_filename_ext(check_null(namelist[i]->d_name)), "fits"))
        if (boost::algorithm::ends_with(lower_filename, ".fits") ||
            boost::algorithm::ends_with(lower_filename, ".fits.gz"))
		    {
		      char* encoded = json_encode_string(check_null(namelist[i]->d_name)) ;
		    		    
		      json << "{\"type\" : \"file\", \"name\" : " << check_null(encoded) << ", \"size\" : " << filesize << ", \"last_modified\" : \"" << last_modified << "\"}," ;
		      has_contents = true ;

		      if(encoded != NULL)
			free(encoded) ;
		    }
    }
	      }
	    else
	      perror("stat64") ;
	  
	    free(namelist[i]);
	  } ;

	//overwrite the the last ',' with a list closing character
	if(has_contents)
	  json.seekp(-1, std::ios_base::end);

	json << "]}" ;
      } ;

    if(namelist != NULL)
      free(namelist);

  //std::cout << json.str() << std::endl;
  mg_send_head(nc, 200, json.tellp(), "Content-Type: application/json\r\nCache-Control: no-cache");
	mg_printf(nc, json.str().c_str());
}
#endif

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
      
  #ifdef LOCAL
      //get_directory
      if(strnstr(hm->uri.p, "/get_directory", hm->uri.len) != NULL)
	    {	  
        char dir[1024] = "";	
	      struct mg_str query = hm->query_string;

	      if(query.len > 0)
	      {
	        printf("%.*s\n", (int) query.len, query.p);	              

	        if(mg_get_http_var(&query, "dir", dir, sizeof(dir)-1) > 0)
            printf("dir: ""%s""\n", dir);          
        }

        //return a json with a directory listing
        if(strcmp(dir, "") == 0)
          return get_directory(nc, home_dir.c_str());
        else
          return get_directory(nc, dir);
      }
  #endif

      if(strnstr(hm->uri.p, "FITSWebQL.html", hm->uri.len) != NULL)
	    {	          
	      struct mg_str query = hm->query_string;

	      if(query.len > 0)
	      {
	        printf("%.*s\n", (int) query.len, query.p);
        }
      }

      mg_serve_http(nc, hm, s_http_server_opts);

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
#ifdef CLUSTER
  setenv("ZSYS_SIGHANDLER","false",1);
  //LAN cluster node auto-discovery
  beacon_thread = std::thread([]() {
				speaker = zactor_new (zbeacon, NULL);
				if(speaker == NULL)
				  return;

				zstr_send (speaker, "VERBOSE");
				zsock_send (speaker, "si", "CONFIGURE", BEACON_PORT);
				char *my_hostname = zstr_recv (speaker);
				if(my_hostname != NULL)
				  {
				    const char* message = "JVO:>FITSWEBQL::ENTER";
				    const int interval = 1000;//[ms]
				    zsock_send (speaker, "sbi", "PUBLISH", message, strlen(message), interval);
				  }

				listener = zactor_new (zbeacon, NULL);
				if(listener == NULL)
				  return;

				zstr_send (listener, "VERBOSE");
				zsock_send (listener, "si", "CONFIGURE", BEACON_PORT);
				char *hostname = zstr_recv (listener);
				if(hostname != NULL)
				  free(hostname);
				else
				  return;

				zsock_send (listener, "sb", "SUBSCRIBE", "", 0);
				zsock_set_rcvtimeo (listener, 500);
  
				while(!exiting) {
				  char *ipaddress = zstr_recv (listener);
				  if (ipaddress != NULL) {				    
				    zframe_t *content = zframe_recv (listener);
				    std::string_view message = std::string_view((const char*)zframe_data (content), zframe_size (content));

				    //ENTER
				    if(message.find("ENTER") != std::string::npos)
				      {
					if(strcmp(my_hostname, ipaddress) != 0)
					  {
					    std::string node = std::string(ipaddress);
					    
					    if(!cluster_contains_node(node))
					      {
						PrintThread{} << "found a new peer @ " << ipaddress << ": " << message << std::endl;
						cluster_insert_node(node);
					      }
					  }
				      }

				    //LEAVE
				    if(message.find("LEAVE") != std::string::npos)
				      {
					if(strcmp(my_hostname, ipaddress) != 0)
					  {
					    std::string node = std::string(ipaddress);
					    
					    if(cluster_contains_node(node))
					      {
						PrintThread{} << ipaddress << " is leaving: " << message << std::endl;
						cluster_erase_node(node);
					      }
					  }
				      }

				    zframe_destroy (&content);
				    zstr_free (&ipaddress);
				  }
				}

				if(my_hostname != NULL)
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
  curl_global_init(CURL_GLOBAL_ALL) ;
  
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
  s_http_server_opts.document_root = "htdocs_mg";  // Serve current directory
  s_http_server_opts.enable_directory_listing = "no";
#ifdef LOCAL
  s_http_server_opts.index_files = "local.html";              
#else
  s_http_server_opts.index_files = "test.html";	
#endif
  s_http_server_opts.custom_mime_types = ".txt=text/plain,.html=text/html,.js=application/javascript,.ico=image/x-icon,.png=image/png,.gif=image/gif,.webp=image/webp,.jpg=image/jpeg,.jpeg=image/jpeg,.bpg=image/bpg,.mp4=video/mp4,.hevc=video/hevc,.css=text/css,.pdf=application/pdf,.svg=image/svg+xml,.wasm=application/wasm";

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
  
  if (splat_db != NULL)
    sqlite3_close(splat_db);

#ifdef CLUSTER
  if(speaker != NULL)
    {
      zstr_sendx (speaker, "SILENCE", NULL);

      const char* message = "JVO:>FITSWEBQL::LEAVE";
      const int interval = 1000;//[ms]
      zsock_send (speaker, "sbi", "PUBLISH", message, strlen(message), interval);
      
      zstr_sendx (speaker, "SILENCE", NULL);      
      zactor_destroy (&speaker);
    }

  if(listener != NULL)
    {
      zstr_sendx (listener, "UNSUBSCRIBE", NULL);
      beacon_thread.join();
      zactor_destroy (&listener);
    }
#endif

  return 0;
}
