#include <iostream>
#include <nghttp2/asio_http2_server.h>

// sudo openssl req -x509 -nodes -days 3650 -newkey rsa:2048 -keyout
// /etc/ssl/private/http2-selfsigned.key -out
// /etc/ssl/certs/http2-selfsigned.crt openssl req -x509 -nodes -days 3650
// -newkey rsa:2048 -keyout ssl/server.key -out ssl/server.crt

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

int main(int argc, char *argv[]) {
  boost::system::error_code ec;
  boost::asio::ssl::context tls(boost::asio::ssl::context::sslv23);

  tls.use_private_key_file("ssl/server.key", boost::asio::ssl::context::pem);
  tls.use_certificate_chain_file("ssl/server.crt");

  if (configure_tls_context_easy(ec, tls)) {
    std::cerr << "error: " << ec.message() << std::endl;
  }

  http2 server;
  server.num_threads(4);

  server.handle("/", [](const request &req, const response &res) {
    std::cout << req.uri().path << std::endl;

    boost::system::error_code ec;
    auto push = res.push(ec, "GET", "/favicon.ico");
    push->write_head(200);
    push->end(file_generator("htdocs2/favicon.ico"));

    res.write_head(200);
    res.end("FITSWebQL v5\n");
  });

  if (server.listen_and_serve(ec, tls, "localhost", "8080")) {
    std::cerr << "error: " << ec.message() << std::endl;
  }
}
