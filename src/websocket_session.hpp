#pragma once

#include "net.hpp"
#include "beast.hpp"
#include "shared_state.hpp"
#include "fitswebql.hpp"

#include <iostream>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

using namespace std::chrono;

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <curl/curl.h>

// Forward declaration
class shared_state;

/** Represents an active WebSocket connection to the server
*/
class websocket_session : public boost::enable_shared_from_this<websocket_session>
{
    beast::flat_buffer buffer_;
    websocket::stream<beast::tcp_stream> ws_;
    boost::shared_ptr<shared_state> state_;
    std::vector<boost::shared_ptr<std::string const>> queue_;

    //FITSWebQL session management
    boost::uuids::uuid session_id;
    system_clock::time_point ts;
    std::shared_mutex ts_mtx;

    //the main fields
    std::string primary_id;
    std::vector<std::string> ids;
    //end-of-FITSWebQL

    void fail(beast::error_code ec, char const* what);
    void on_accept(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);

public:
    websocket_session(
        tcp::socket&& socket,
        boost::shared_ptr<shared_state> const& state);

    ~websocket_session();

    template<class Body, class Allocator>
    void
    run(http::request<Body, http::basic_fields<Allocator>> req);

    // Send a message
    void
    send(boost::shared_ptr<std::string const> const& ss);

private:
    void
    on_send(boost::shared_ptr<std::string const> const& ss);
};

template<class Body, class Allocator>
void
websocket_session::
run(http::request<Body, http::basic_fields<Allocator>> req)
{
    boost::beast::string_view uri = req.target();
    PrintThread{} << "[ws] " << uri << std::endl;    

    // Set suggested timeout settings for the websocket
    ws_.set_option(
        websocket::stream_base::timeout::suggested(
            beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " FITSWEBQL");
        }));

    size_t pos = uri.find_last_of("/");
	if (pos == std::string::npos)
        return;

    boost::beast::string_view tmp = uri.substr(pos+1);
	CURL *curl = curl_easy_init();
	char *str = curl_easy_unescape(curl, tmp.data(), tmp.length(), NULL);
	std::string plain = std::string(str);
	curl_free(str);
	curl_easy_cleanup(curl);

	std::vector<std::string> datasetid;
	boost::split(datasetid, plain, [](char c) { return c == ';'; });
                  
	for (auto const &s : datasetid) {
	    PrintThread{} << "datasetid: " << s << std::endl;
	}

    primary_id = datasetid[0];                          
	ids = datasetid;

    // Accept the websocket handshake
    ws_.async_accept(
        req,
        beast::bind_front_handler(
            &websocket_session::on_accept,
            shared_from_this()));
}
