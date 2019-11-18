#include <filesystem>
#include <iostream>

#include <nghttp2/asio_http2_server.h>

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

std::string docs_root = "htdocs2";

void not_found(const response *res) {
  res->write_head(404);
  res->end("Not Found");
}

void serve_file(const response *res, std::string uri) {
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

    res->write_head(200, mime);
    res->end(file_generator(path));
  } else {
    res->write_head(404);
    res->end("Not Found");
  }
}

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

  server.handle("/get_directory", [](const request &req, const response &res) {
    res.write_head(200);
    res.end("FITSWebQL v5\n");
  });

  server.handle("/", [](const request &req, const response &res) {
    boost::system::error_code ec;

    auto uri = req.uri().path;

    if (uri == "/") {
      auto push = res.push(ec, "GET", "/favicon.ico");
      serve_file(push, "/favicon.ico");

#ifdef LOCAL
      push = res.push(ec, "GET", "/local.css");
      serve_file(push, "/local.css");

      push = res.push(ec, "GET", "/local.js");
      serve_file(push, "/local.js");

      push = res.push(ec, "GET", "/logo_naoj_all_s.png");
      serve_file(push, "/logo_naoj_all_s.png");

      serve_file(&res, "/local.html");
#else
      push = res.push(ec, "GET", "/test.css");      
      serve_file(push, "/test.css");

      push = res.push(ec, "GET", "/test.js");      
      serve_file(push, "/test.js");
      
      serve_file(&res, "/test.html");
#endif
    } else {
      std::cout << uri << std::endl;
      serve_file(&res, uri);
    }
  });

  if (server.listen_and_serve(ec, tls, "0.0.0.0", "8080")) {
    std::cerr << "error: " << ec.message() << std::endl;
  }
}
