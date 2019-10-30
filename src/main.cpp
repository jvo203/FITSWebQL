#include "fitswebql.hpp"
#include "listener.hpp"
#include "shared_state.hpp"

#include <boost/asio/signal_set.hpp>
#include <boost/smart_ptr.hpp>
#include <iostream>
#include <memory>
#include <vector>

#ifdef CLUSTER
zactor_t *beacon_speaker = NULL;
zactor_t *beacon_listener = NULL;
std::thread beacon_thread;
std::atomic<bool> exiting(false);
#endif

int
main(int argc, char* argv[])
{
    #ifdef CLUSTER
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
				    const char* message = "JVO:>FITSWEBQL::ENTER";
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
        boost::make_shared<shared_state>(doc_root))->run();

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

                const char* message = "JVO:>FITSWEBQL::LEAVE";
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

    std::cout << "FITSWebQL shutdown completed." << std::endl;

    return EXIT_SUCCESS;
}
