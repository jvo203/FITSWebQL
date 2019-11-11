#include "fitswebql.hpp"
#include "listener.hpp"
#include "shared_state.hpp"

#include <boost/asio/signal_set.hpp>
#include <boost/smart_ptr.hpp>
#include <iostream>
#include <memory>
#include <vector>

#include <pwd.h>
#include <sys/types.h>

#include <curl/curl.h>
#include <ipp.h>

#ifdef CLUSTER
zactor_t *beacon_speaker = NULL;
zactor_t *beacon_listener = NULL;
std::thread beacon_thread;
std::atomic<bool> exiting(false);
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

int
main(int argc, char* argv[])
{
    #ifdef CLUSTER
    setenv("ZSYS_SIGHANDLER","false",1);
  //LAN cluster node auto-discovery
  beacon_thread = std::thread([]() {
				beacon_speaker = zactor_new (zbeacon, NULL);
				if(beacon_speaker == NULL)
				  return;

				zstr_send (beacon_speaker, "VERBOSE");
				zsock_send (beacon_speaker, "si", "CONFIGURE", BEACON_PORT);
				char *my_hostname = zstr_recv (beacon_speaker);
				if(my_hostname != NULL)
				  {
				    const char* message = "JVO :> FITSWEBQL::ENTER";
				    const int interval = 1000;//[ms]
				    zsock_send (beacon_speaker, "sbi", "PUBLISH", message, strlen(message), interval);
				  }

				beacon_listener = zactor_new (zbeacon, NULL);
				if(beacon_listener == NULL)
				  return;

				zstr_send (beacon_listener, "VERBOSE");
				zsock_send (beacon_listener, "si", "CONFIGURE", BEACON_PORT);
				char *hostname = zstr_recv (beacon_listener);
				if(hostname != NULL)
				  free(hostname);
				else
				  return;

				zsock_send (beacon_listener, "sb", "SUBSCRIBE", "", 0);
				zsock_set_rcvtimeo (beacon_listener, 500);
  
				while(!exiting) {
				  char *ipaddress = zstr_recv (beacon_listener);
				  if (ipaddress != NULL) {				    
				    zframe_t *content = zframe_recv (beacon_listener);
				    std::string_view message = std::string_view((const char*)zframe_data (content), zframe_size (content));

				    //ENTER
				    if(message.find("ENTER") != std::string::npos)
				      {
					if(strcmp(my_hostname, ipaddress) != 0)
					  {
					    std::string node = std::string(ipaddress);
					    
					    if(!cluster_contains_node(node))
					      {
						PrintThread{} << message << " :> found a new peer @" << ipaddress << std::endl;
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
						PrintThread{} << message << " :> " << ipaddress << " is leaving" << std::endl;
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

	ipp_init();
  	curl_global_init(CURL_GLOBAL_ALL);  	

  	struct passwd *passwdEnt = getpwuid(getuid());
  	auto home_dir = passwdEnt->pw_dir;

    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(SERVER_PORT);    
    auto doc_root = "htdocs_beast";
    auto const threads = std::max<int>(std::thread::hardware_concurrency() / 2, 1);

    // The io_context is required for all I/O
    net::io_context ioc;

    // Create and launch a listening port
    boost::make_shared<listener>(
        ioc,
        tcp::endpoint{address, port},
        boost::make_shared<shared_state>(doc_root, home_dir))->run();

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&ioc](boost::system::error_code const&, int signal)
        {
            PrintThread{} << "Interrupt signal (" << signal << ") received.\n";
            // Stop the io_context. This will cause run()
            // to return immediately, eventually destroying the
            // io_context and any remaining handlers in it.
            ioc.stop();
        });

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back(
        [&ioc]
        {
            ioc.run();
        });
    ioc.run();

    // (If we get here, it means we got a SIGINT or SIGTERM)

#ifdef CLUSTER
            exiting = true;

            if(beacon_speaker != NULL)
            {
                zstr_sendx (beacon_speaker, "SILENCE", NULL);

                const char* message = "JVO :> FITSWEBQL::LEAVE";
                const int interval = 1000;//[ms]
                zsock_send (beacon_speaker, "sbi", "PUBLISH", message, strlen(message), interval);
      
                zstr_sendx (beacon_speaker, "SILENCE", NULL);      
                zactor_destroy (&beacon_speaker);
            }

            if(beacon_listener != NULL)
            {
                zstr_sendx (beacon_listener, "UNSUBSCRIBE", NULL);
                beacon_thread.join();
                zactor_destroy (&beacon_listener);
            }
#endif

    // Block until all the threads exit
    for(auto& t : v)
        t.join();

	curl_global_cleanup();

    return EXIT_SUCCESS;
}
