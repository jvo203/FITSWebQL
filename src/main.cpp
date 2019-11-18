#include <filesystem>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>

#include "json.h"

inline const char *check_null(const char *str) {
  if (str != NULL)
    return str;
  else
    return ""; //"\"\"" ;
};

#include <nghttp2/asio_http2_server.h>

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

std::string docs_root = "htdocs2";
std::string home_dir;

void not_found(const response *res) {
  res->write_head(404);
  res->end("Not Found");
}

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
  struct passwd *passwdEnt = getpwuid(getuid());
  home_dir = passwdEnt->pw_dir;

  boost::system::error_code ec;
  boost::asio::ssl::context tls(boost::asio::ssl::context::sslv23);

  tls.use_private_key_file("ssl/server.key", boost::asio::ssl::context::pem);
  tls.use_certificate_chain_file("ssl/server.crt");

  if (configure_tls_context_easy(ec, tls)) {
    std::cerr << "error: " << ec.message() << std::endl;
  }

  http2 server;
  server.num_threads(4);

#ifdef LOCAL
  server.handle("/get_directory", [](const request &req, const response &res) {
    serve_directory(&res, home_dir);
  });
#endif

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
