#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define SERVER_PORT 8080
#define SERVER_STRING                                                          \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-11-19.0"
#define WASM_STRING "WASM2019-02-08.1"

#include <filesystem>
#include <iostream>
#include <set>
#include <shared_mutex>
#include <unordered_map>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>

#include "fits.hpp"
#include "json.h"

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

bool is_gzip(const char *filename) {
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

void http_not_found(const response *res) {
  res->write_head(404);
  res->end("Not Found");
}

void http_not_implemented(const response *res) {
  res->write_head(501);
  res->end("Not Implemented");
}

#ifdef LOCAL
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
#endif

void get_spectrum(const response *res, std::shared_ptr<FITS> fits) {
  std::ostringstream json;

  fits->to_json(json);

  if (json.tellp() > 0) {
    header_map mime;
    mime.insert(std::pair<std::string, header_value>(
        "Content-Type", {"application/json", false}));
    mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                     {"no-cache", false}));

    res->write_head(200, mime);
    res->end(json.str());
  } else {
    return http_not_implemented(res);
  }
}

void serve_file(const request *req, const response *res, std::string uri) {
  // a safety check against directory traversal attacks
  if (!check_path(uri))
    return http_not_found(res);

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

    // check for compression
    header_map headers = req->header();

    auto it = headers.find("accept-encoding");
    if (it != headers.end()) {
      auto value = it->second.value;
      // std::cout << "Supported compression: " << value << std::endl;

      //prefer brotli due to smaller file sizes
      size_t pos = value.find("br"); // brotli or gzip

      if (pos != std::string::npos) {
        if (std::filesystem::exists(path + ".br")) {
          path += ".br";
          // append the compression mime
          mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                           {"br", false}));
        }
      } else {
        // use gzip as a backup
        size_t pos = value.find("gzip");

        if (pos != std::string::npos) {
          if (std::filesystem::exists(path + ".gz")) {
            path += ".gz";
            // append the compression mime
            mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                             {"gzip", false}));
          }
        }
      }
    }

    res->write_head(200, mime);
    res->end(file_generator(path));
  } else {
    http_not_found(res);
  }
}

void http_fits_response(const response *res, std::vector<std::string> datasets,
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

  header_map mime;
  mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                   {"text/html", false}));
  res->write_head(200, mime);
  res->end(html);
}

void execute_fits(const response *res, std::string dir, std::string ext,
                  std::string db, std::string table,
                  std::vector<std::string> datasets, bool composite,
                  std::string flux) {
  bool has_fits = true;

#ifndef LOCAL
  PGconn *jvo_db = NULL;

  if (db != "")
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

      if (dir != "" && ext != "")
        path = dir + "/" + data_id + "." + ext;

#ifndef LOCAL
      if (jvo_db != NULL && table != "")
        path = get_jvo_path(jvo_db, db, table, data_id);
#endif

      if (path != "") {
        bool is_compressed = is_gzip(path.c_str());
        /*bool is_compressed = false;
          std::string lower_path = boost::algorithm::to_lower_copy(path);
          if (boost::algorithm::ends_with(lower_path, ".gz"))
          is_compressed = is_gzip(path.c_str());*/

        // load FITS data in a separate thread
        std::thread(&FITS::from_path_zfp, fits, path, is_compressed, flux,
                    va_count)
            .detach();
      } else {
        // the last resort
        std::string url = std::string("http://") + JVO_FITS_SERVER +
                          ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB +
                          "&table=cube&data_id=" + data_id + "_00_00_00";

        // download FITS data from a URL in a separate thread
        std::thread(&FITS::from_url, fits, url, flux, va_count).detach();
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

  std::cout << "has_fits: " << has_fits << std::endl;

  return http_fits_response(res, datasets, composite, has_fits);
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
    auto uri = req.uri();
    auto query = percent_decode(uri.raw_query);

    std::string dir;
    std::vector<std::string> params;
    boost::split(params, query, [](char c) { return c == '&'; });

    for (auto const &s : params) {
      // find '='
      size_t pos = s.find("=");

      if (pos != std::string::npos) {
        std::string key = s.substr(0, pos);
        std::string value = s.substr(pos + 1, std::string::npos);

        if (key == "dir") {
          dir = value;
        }
      }
    }

    if (dir != "")
      serve_directory(&res, dir);
    else
      serve_directory(&res, home_dir);
  });
#endif

  server.handle("/", [](const request &req, const response &res) {
    boost::system::error_code ec;

    auto uri = req.uri().path;

    if (uri == "/") {
      auto push = res.push(ec, "GET", "/favicon.ico");
      serve_file(&req, push, "/favicon.ico");

#ifdef LOCAL
      push = res.push(ec, "GET", "/local.css");
      serve_file(&req, push, "/local.css");

      push = res.push(ec, "GET", "/local.js");
      serve_file(&req, push, "/local.js");

      push = res.push(ec, "GET", "/logo_naoj_all_s.png");
      serve_file(&req, push, "/logo_naoj_all_s.png");

      serve_file(&req, &res, "/local.html");
#else
      push = res.push(ec, "GET", "/test.css");      
      serve_file(&req, push, "/test.css");

      push = res.push(ec, "GET", "/test.js");      
      serve_file(&req, push, "/test.js");
      
      serve_file(&req, &res, "/test.html");
#endif
    } else {
      std::cout << uri << std::endl;

      if (uri.find("/get_spectrum") != std::string::npos) {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string datasetid;

        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key.find("dataset") != std::string::npos) {
              datasetid = value;
            }
          }
        }

        // process the response
        std::cout << "get_spectrum(" << datasetid << ")" << std::endl;

        auto fits = get_dataset(datasetid);

        if (fits == nullptr)
          return http_not_found(&res);
        else {
          if (fits->has_error)
            return http_not_found(&res);
          else {
            std::unique_lock<std::mutex> data_lock(fits->data_mtx);
            while (!fits->processed_data)
              fits->data_cv.wait(data_lock);

            if (!fits->has_data)
              return http_not_found(&res);
            // return http_accepted(res);
            else
              return get_spectrum(&res, fits);
          }
        }
      }

      // FITSWebQL entry
      if (uri.find("FITSWebQL.html") != std::string::npos) {
        auto push = res.push(ec, "GET", "/favicon.ico");
        serve_file(&req, push, "/favicon.ico");

        push = res.push(ec, "GET", "/fitswebql/fitswebql.css?" VERSION_STRING);
        serve_file(&req, push, "/fitswebql/fitswebql.css");

        push = res.push(ec, "GET", "/fitswebql/fitswebql.js?" VERSION_STRING);
        serve_file(&req, push, "/fitswebql/fitswebql.js");

        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::vector<std::string> datasets;
        std::string dir, ext, db, table, flux;
        bool composite = false;

        std::vector<std::string> params;
        boost::split(params, query, [](char c) { return c == '&'; });

        for (auto const &s : params) {
          // find '='
          size_t pos = s.find("=");

          if (pos != std::string::npos) {
            std::string key = s.substr(0, pos);
            std::string value = s.substr(pos + 1, std::string::npos);

            if (key.find("dataset") != std::string::npos) {
              datasets.push_back(value);
            }

            if (key.find("filename") != std::string::npos) {
              datasets.push_back(value);
            }

            if (key == "dir") {
              dir = value;
            }

            if (key == "ext") {
              ext = value;
            }

            if (key == "db")
              db = value;

            if (key == "table")
              table = value;

            if (key == "flux") {
              // validate the flux value
              std::set<std::string> valid_values;
              valid_values.insert("linear");
              valid_values.insert("logistic");
              valid_values.insert("ratio");
              valid_values.insert("square");
              valid_values.insert("legacy");

              if (valid_values.find(value) != valid_values.end())
                flux = value;
            }

            if (key == "view") {
              if (value.find("composite") != std::string::npos)
                composite = true;
            }
          }
        }

        // sane defaults
        {
          if (db.find("hsc") != std::string::npos) {
            flux = "ratio";
          }

          if (table.find("fugin") != std::string::npos)
            flux = "logistic";
        }

        std::cout << "dir:" << dir << ", ext:" << ext << ", db:" << db
                  << ", table:" << table << ", composite:" << composite
                  << ", flux:" << flux << ", ";
        for (auto const &dataset : datasets)
          std::cout << dataset << " ";
        std::cout << std::endl;

        if (datasets.size() == 0) {
          return http_not_found(&res);
        } else
          return execute_fits(&res, dir, ext, db, table, datasets, composite,
                              flux);
      }

      serve_file(&req, &res, uri);
    }
  });

  if (server.listen_and_serve(ec, tls, "0.0.0.0",
                              std::to_string(SERVER_PORT))) {
    std::cerr << "error: " << ec.message() << std::endl;
  }
}
