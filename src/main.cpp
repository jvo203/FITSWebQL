#include <filesystem>
#include <iostream>

#include <nghttp2/asio_http2_server.h>

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

std::string docs_root = "htdocs2";

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
    auto uri = req.uri().path;
    std::cout << uri << std::endl;

    boost::system::error_code ec;

    if (uri == "/") {
      auto push = res.push(ec, "GET", "/favicon.ico");
      push->write_head(200);
      push->end(file_generator(docs_root + "/favicon.ico"));

#ifdef LOCAL
      push = res.push(ec, "GET", "/local.css");
      push->write_head(200);
      push->end(file_generator(docs_root + "/local.css"));

      push = res.push(ec, "GET", "/local.js");
      push->write_head(200);
      push->end(file_generator(docs_root + "/local.js"));

      push = res.push(ec, "GET", "/logo_naoj_all_s.png");
      push->write_head(200);
      push->end(file_generator(docs_root + "/logo_naoj_all_s.png"));

      res.write_head(200);
      res.end(file_generator(docs_root + "/local.html"));
#else
      push = res.push(ec, "GET", "/test.css");
      push->write_head(200);
      push->end(file_generator(docs_root + "/test.css"));

      push = res.push(ec, "GET", "/test.js");
      push->write_head(200);
      push->end(file_generator(docs_root + "/test.js"));

      res.write_head(200);
      res.end(file_generator(docs_root + "/test.html"));
#endif
    } else {
      // check if a resource exists
      std::string path = docs_root + uri;

      if (std::filesystem::exists(path)) {
        res.write_head(200);
        res.end(file_generator(path));
      } else {
        res.write_head(404);
        res.end("Not Found");
      }
    }
  });

  if (server.listen_and_serve(ec, tls, "0.0.0.0", "8080")) {
    std::cerr << "error: " << ec.message() << std::endl;
  }
}
