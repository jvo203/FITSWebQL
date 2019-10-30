#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_PORT 8080
#define BEACON_PORT 50000
#define SERVER_STRING                                                          \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-10-30.0"
#define WASM_STRING "WASM2019-02-08.1"

#include <algorithm>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <pwd.h>
#include <sys/types.h>

namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace http = beast::http;     // from <boost/beast/http.hpp>
namespace net = boost::asio;      // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

#include <curl/curl.h>
#include <ipp.h>
#include <sqlite3.h>

#include "fits.hpp"
#include "json.h"

#include <experimental/filesystem>
#include <shared_mutex>

namespace fs = std::experimental::filesystem;

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::shared_mutex fits_mutex;
std::string home_dir;
sqlite3 *splat_db = NULL;

inline const char *check_null(const char *str) {
  if (str != nullptr)
    return str;
  else
    return "\"\"";
};

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

#include "global.h"

#ifdef CLUSTER
zactor_t *beacon_speaker = NULL;
zactor_t *beacon_listener = NULL;
std::thread beacon_thread;
std::atomic<bool> exiting(false);
#endif

void signalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received.\n";

#ifdef CLUSTER
  exiting = true;
#endif

  // cleanup and close up stuff here
  // terminate program

  curl_global_cleanup();

  if (splat_db != NULL)
    sqlite3_close(splat_db);

#ifdef CLUSTER
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
  
  //zsys_shutdown();
#endif

  std::cout << "FITSWebQL shutdown completed." << std::endl;

  exit(signum);
}

#ifdef LOCAL
uintmax_t ComputeFileSize(const fs::path &pathToCheck) {
  if (fs::exists(pathToCheck) && fs::is_regular_file(pathToCheck)) {
    auto err = std::error_code{};
    auto filesize = fs::file_size(pathToCheck, err);
    if (filesize != static_cast<uintmax_t>(-1))
      return filesize;
  }

  return static_cast<uintmax_t>(-1);
}

std::string get_directory(std::string dir) {
  std::cout << "scanning directory " << dir << std::endl;

  fs::path pathToShow(dir);

  std::map<std::string, std::string> entries;

  if (fs::exists(pathToShow) && fs::is_directory(pathToShow)) {
    for (const auto &entry : fs::directory_iterator(pathToShow)) {
      if (!fs::exists(entry))
        continue;

      auto filename = entry.path().filename();
      auto timestamp = fs::last_write_time(entry);
      time_t cftime = std::chrono::system_clock::to_time_t(timestamp);
      std::string last_modified = std::asctime(std::localtime(&cftime));
      last_modified.pop_back();

      if (fs::is_directory(entry.status())) {
        if (!boost::algorithm::starts_with(filename.string(), ".")) {
          char *encoded = json_encode_string(filename.c_str());

          std::string json =
              "{\"type\" : \"dir\", \"name\" : " + std::string(encoded) +
              ", \"last_modified\" : \"" + last_modified + "\"}";

          if (encoded != NULL)
            free(encoded);

          std::cout << json << std::endl;

          entries.insert(std::pair(filename, json));
        }
      } else if (fs::is_regular_file(entry.status())) {
        // check the extensions .fits or .fits.gz
        const std::string lower_filename =
            boost::algorithm::to_lower_copy(filename.string());

        if (boost::algorithm::ends_with(lower_filename, ".fits") ||
            boost::algorithm::ends_with(lower_filename, ".fits.gz")) {

          char *encoded = json_encode_string(filename.c_str());

          uintmax_t filesize = ComputeFileSize(entry);

          std::string json =
              "{\"type\" : \"file\", \"name\" : " + std::string(encoded) +
              ", \"size\" : " + std::to_string(filesize) +
              ", \"last_modified\" : \"" + last_modified + "\"}";

          if (encoded != NULL)
            free(encoded);

          std::cout << json << std::endl;

          entries.insert(std::pair(filename, json));
        }
      }
    }
  }

  std::ostringstream json;

  char *encoded = json_encode_string(check_null(dir.c_str()));

  json << "{\"location\" : " << check_null(encoded) << ", \"contents\" : [";

  if (encoded != NULL)
    free(encoded);

  if (entries.size() > 0) {
    for (auto const &entry : entries) {
      json << entry.second << ",";
    }

    // overwrite the the last ',' with a list closing character
    json.seekp(-1, std::ios_base::end);
  }

  json << "]}";

  return json.str();
}
#endif

#ifndef LOCAL
PGconn *jvo_db_connect(std::string db) {
  PGconn *jvo_db = NULL;

  std::string conn_str =
      "dbname=" + db + " host=" + JVO_HOST + " user=" + JVO_USER;

  jvo_db = PQconnectdb(conn_str.c_str());

  if (PQstatus(jvo_db) != CONNECTION_OK) {
    fprintf(stderr, "PostgreSQL connection failed: %s\n",
            PQerrorMessage(jvo_db));
    PQfinish(jvo_db);
    jvo_db = NULL;
  } else
    printf("PostgreSQL connection successful.\n");

  return jvo_db;
}

std::string get_jvo_path(PGconn *jvo_db, std::string db, std::string table,
                         std::string data_id) {
  std::string path;

  std::string sql_str =
      "SELECT path FROM " + table + " WHERE data_id = '" + data_id + "';";

  PGresult *res = PQexec(jvo_db, sql_str.c_str());
  int status = PQresultStatus(res);

  if (PQresultStatus(res) == PGRES_TUPLES_OK) {
    path = std::string(FITSHOME) + "/" + db + "/";

    size_t pos = table.find(".");

    if (pos == std::string::npos)
      path += std::string((const char *)PQgetvalue(res, 0, 0));
    else
      path += boost::algorithm::to_upper_copy(table.substr(0, pos)) + "/" +
              std::string((const char *)PQgetvalue(res, 0, 0));
  }

  PQclear(res);

  return path;
}
#endif

std::string http_fits_response(std::vector<std::string> datasets,
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

  return html;
}

std::string execute_fits(std::string dir, std::string ext, std::string db,
                         std::string table, std::vector<std::string> datasets,
                         bool composite, std::string flux) {
  bool has_fits = true;

#ifndef LOCAL
  PGconn *jvo_db = NULL;

  if (db != "")
    jvo_db = jvo_db_connect(db);
#endif

  int va_count = datasets.size();

  for (auto const &data_id : datasets) {
    std::shared_lock<std::shared_mutex> lock(fits_mutex);
    auto item = DATASETS.find(data_id);
    lock.unlock();

    if (item == DATASETS.end()) {
      // set has_fits to false and load the FITS dataset
      has_fits = false;
      std::shared_ptr<FITS> fits(new FITS(data_id, flux));

      std::unique_lock<std::shared_mutex> lock(fits_mutex);
      DATASETS.insert(std::pair(data_id, fits));
      lock.unlock();

      std::string path;

      if (dir != "" && ext != "")
        path = dir + "/" + data_id + "." + ext;

#ifndef LOCAL
      if (jvo_db != NULL && table != "")
        path = get_jvo_path(jvo_db, db, table, data_id);
#endif

      if (path != "") {
        bool is_compressed = is_gzip(path.c_str());

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
      auto fits = item->second;

      has_fits = has_fits && fits->has_data;
      fits->update_timestamp();
    }
  }

#ifndef LOCAL
  if (jvo_db != NULL)
    PQfinish(jvo_db);
#endif

  std::cout << "has_fits: " << has_fits << std::endl;

  return http_fits_response(datasets, composite, has_fits);
}

// Return a reasonable mime type based on the extension of a file.
beast::string_view mime_type(beast::string_view path) {
  using beast::iequals;
  auto const ext = [&path] {
    auto const pos = path.rfind(".");
    if (pos == beast::string_view::npos)
      return beast::string_view{};
    return path.substr(pos);
  }();
  if (iequals(ext, ".htm"))
    return "text/html";
  if (iequals(ext, ".html"))
    return "text/html";
  if (iequals(ext, ".php"))
    return "text/html";
  if (iequals(ext, ".css"))
    return "text/css";
  if (iequals(ext, ".txt"))
    return "text/plain";
  if (iequals(ext, ".js"))
    return "application/javascript";
  if (iequals(ext, ".wasm"))
    return "application/wasm";
  if (iequals(ext, ".json"))
    return "application/json";
  if (iequals(ext, ".xml"))
    return "application/xml";
  if (iequals(ext, ".swf"))
    return "application/x-shockwave-flash";
  if (iequals(ext, ".flv"))
    return "video/x-flv";
  if (iequals(ext, ".png"))
    return "image/png";
  if (iequals(ext, ".jpe"))
    return "image/jpeg";
  if (iequals(ext, ".jpeg"))
    return "image/jpeg";
  if (iequals(ext, ".jpg"))
    return "image/jpeg";
  if (iequals(ext, ".gif"))
    return "image/gif";
  if (iequals(ext, ".bmp"))
    return "image/bmp";
  if (iequals(ext, ".ico"))
    return "image/vnd.microsoft.icon";
  if (iequals(ext, ".tiff"))
    return "image/tiff";
  if (iequals(ext, ".tif"))
    return "image/tiff";
  if (iequals(ext, ".svg"))
    return "image/svg+xml";
  if (iequals(ext, ".svgz"))
    return "image/svg+xml";
  return "application/text";
}

// strip '?' from file paths
beast::string_view strip_path(beast::string_view path) {
  // get a position of '?'
  size_t pos = path.find("?");

  if (pos != std::string::npos) {
    return path.substr(0, pos);
  } else
    return path;
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string path_cat(beast::string_view base, beast::string_view path) {
  if (base.empty())
    return std::string(path);
  std::string result(base);
#ifdef BOOST_MSVC
  char constexpr path_separator = '\\';
  if (result.back() == path_separator)
    result.resize(result.size() - 1);
  result.append(path.data(), path.size());
  for (auto &c : result)
    if (c == '/')
      c = path_separator;
#else
  char constexpr path_separator = '/';
  if (result.back() == path_separator)
    result.resize(result.size() - 1);
  result.append(path.data(), path.size());
#endif
  return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template <class Body, class Allocator, class Send>
void handle_request(beast::string_view doc_root,
                    http::request<Body, http::basic_fields<Allocator>> &&req,
                    Send &&send) {
  // Returns a bad request response
  auto const bad_request = [&req](beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();
    return res;
  };

  // Returns a not found response
  auto const not_found = [&req](beast::string_view target) {
    http::response<http::string_body> res{http::status::not_found,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + std::string(target) + "' was not found.";
    res.prepare_payload();
    return res;
  };

  // Returns a server error response
  auto const server_error = [&req](beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::string(what) + "'";
    res.prepare_payload();
    return res;
  };

#ifdef LOCAL
  // Returns a directory listing
  auto const uncached_json = [&req](std::string body) {
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.set(http::field::cache_control, "no-cache");
    res.set(http::field::cache_control, "no-store");
    res.set(http::field::pragma, "no-cache");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
  };
#endif

  // Returns a valid OK response
  auto const ok_response = [&req](std::string body) {
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    return res;
  };

  // Make sure we can handle the method
  if (req.method() != http::verb::get && req.method() != http::verb::head)
    return send(bad_request("Unknown HTTP-method"));

  // FITSWebQL custom HTTP requests

  boost::beast::string_view uri = req.target();

#ifdef LOCAL
  // handle the local file system requests
  if (uri.find("/get_directory") != std::string::npos) {
    std::string dir;

    // get a position of '?'
    size_t pos = uri.find("?");

    if (pos != std::string::npos) {
      boost::beast::string_view query = uri.substr(pos + 1, std::string::npos);
      std::cout << "query: (" << query << ")" << std::endl;

      std::vector<std::string> params;
      boost::split(params, query, [](char c) { return c == '&'; });

      for (auto const &s : params) {
        // find '='
        size_t pos = s.find("=");

        if (pos != std::string::npos) {
          std::string key = s.substr(0, pos);
          std::string value = s.substr(pos + 1, std::string::npos);

          if (key == "dir") {
            CURL *curl = curl_easy_init();

            char *str =
                curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
            dir = std::string(str);
            curl_free(str);

            curl_easy_cleanup(curl);
          }
        }
      }
    }

    std::string body = "";

    if (dir != "")
      body = get_directory(dir);
    else {
      if (home_dir != "")
        body = get_directory(home_dir);
      else
        return send(not_found(req.target()));
    }

    return send(uncached_json(body));
  }
#endif

  if (uri.find("/get_spectrum") != std::string::npos) {
    // get a position of '?'
    size_t pos = uri.find("?");

    if (pos != std::string::npos) {
      std::string datasetid;

      beast::string_view query = uri.substr(pos + 1, std::string::npos);
      // std::cout << "query: (" << query << ")" << std::endl;

      std::vector<std::string> params;
      boost::split(params, query, [](char c) { return c == '&'; });

      CURL *curl = curl_easy_init();

      for (auto const &s : params) {
        // find '='
        size_t pos = s.find("=");

        if (pos != std::string::npos) {
          std::string key = s.substr(0, pos);
          std::string value = s.substr(pos + 1, std::string::npos);

          if (key.find("dataset") != std::string::npos) {
            char *str =
                curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
            datasetid = std::string(str);
            curl_free(str);
          }
        }
      }

      curl_easy_cleanup(curl);

      // process the response
      std::cout << "get_spectrum(" << datasetid << ")" << std::endl;

      std::shared_lock<std::shared_mutex> lock(fits_mutex);
      auto item = DATASETS.find(datasetid);
      lock.unlock();

      if (item == DATASETS.end())
        return send(not_found(datasetid));
      else {
        auto fits = item->second;

        if (fits->has_error)
          return send(not_found(datasetid));
        else {
          std::unique_lock<std::mutex> data_lck(fits->data_mtx);
          while (!fits->processed_data)
            fits->data_cv.wait(data_lck);

          if (!fits->has_data)
            // return http_accepted(res);
            return send(not_found(datasetid));
          else {
            // return get_spectrum(res, fits);
            std::ostringstream json;

            fits->to_json(json);

            if (json.tellp() > 0) {
              std::string body = json.str();
              return send(uncached_json(body));
            } else {
              return send(
                  server_error(datasetid + "/get_spectrum"));
            }
          }
        }
      }
    }
  }

  // FITSWebQL entry
  if (uri.find("FITSWebQL.html") != std::string::npos) {
    // get a position of '?'
    size_t pos = uri.find("?");

    if (pos != std::string::npos) {
      std::vector<std::string> datasets;
      std::string dir, ext, db, table, flux;
      bool composite = false;

      boost::beast::string_view query = uri.substr(pos + 1, std::string::npos);
      std::cout << "query: (" << query << ")" << std::endl;

      std::vector<std::string> params;
      boost::split(params, query, [](char c) { return c == '&'; });

      CURL *curl = curl_easy_init();

      for (auto const &s : params) {
        // find '='
        size_t pos = s.find("=");

        if (pos != std::string::npos) {
          std::string key = s.substr(0, pos);
          std::string value = s.substr(pos + 1, std::string::npos);

          if (key.find("dataset") != std::string::npos) {
            char *str =
                curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
            datasets.push_back(std::string(str));
            curl_free(str);
          }

          if (key.find("filename") != std::string::npos) {
            char *str =
                curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
            datasets.push_back(std::string(str));
            curl_free(str);
          }

          if (key == "dir") {
            char *str =
                curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
            dir = std::string(str);
            curl_free(str);
          }

          if (key == "ext") {
            char *str =
                curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
            ext = std::string(str);
            curl_free(str);
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

      curl_easy_cleanup(curl);

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
        return send(bad_request("ERROR: please specify at least one "
                                "dataset in the URL parameters list."));
      } else
        return send(ok_response(
            execute_fits(dir, ext, db, table, datasets, composite, flux)));
    } else {
      return send(not_found("ERROR: URL parameters not found."));
    }
  }

  // Request path must be absolute and not contain "..".
  if (req.target().empty() || req.target()[0] != '/' ||
      req.target().find("..") != beast::string_view::npos)
    return send(bad_request("Illegal request-target"));

  // Build the path to the requested file
  std::string path = path_cat(doc_root, strip_path(req.target()));
  if (req.target().back() == '/')
#ifdef LOCAL
    path.append("local.html");
#else
    path.append("test.html");
#endif

  // Attempt to open the file
  beast::error_code ec;
  http::file_body::value_type body;
  body.open(path.c_str(), beast::file_mode::scan, ec);

  // Handle the case where the file doesn't exist
  if (ec == beast::errc::no_such_file_or_directory)
    return send(not_found(req.target()));

  // Handle an unknown error
  if (ec)
    return send(server_error(ec.message()));

  // Cache the size since we need it after the move
  auto const size = body.size();

  // Respond to HEAD request
  if (req.method() == http::verb::head) {
    http::response<http::empty_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
  }

  // Respond to GET request
  http::response<http::file_body> res{
      std::piecewise_construct, std::make_tuple(std::move(body)),
      std::make_tuple(http::status::ok, req.version())};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, mime_type(path));
  res.content_length(size);
  res.keep_alive(req.keep_alive());
  return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void fail(beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

// Handles an HTTP server connection
class session : public std::enable_shared_from_this<session> {
  // This is the C++11 equivalent of a generic lambda.
  // The function object is used to send an HTTP message.
  struct send_lambda {
    session &self_;

    explicit send_lambda(session &self) : self_(self) {}

    template <bool isRequest, class Body, class Fields>
    void operator()(http::message<isRequest, Body, Fields> &&msg) const {
      // The lifetime of the message has to extend
      // for the duration of the async operation so
      // we use a shared_ptr to manage it.
      auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(
          std::move(msg));

      // Store a type-erased version of the shared
      // pointer in the class to keep it alive.
      self_.res_ = sp;

      // Write the response
      http::async_write(self_.stream_, *sp,
                        beast::bind_front_handler(&session::on_write,
                                                  self_.shared_from_this(),
                                                  sp->need_eof()));
    }
  };

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::shared_ptr<std::string const> doc_root_;
  http::request<http::string_body> req_;
  std::shared_ptr<void> res_;
  send_lambda lambda_;

public:
  // Take ownership of the stream
  session(tcp::socket &&socket,
          std::shared_ptr<std::string const> const &doc_root)
      : stream_(std::move(socket)), doc_root_(doc_root), lambda_(*this) {}

  // Start the asynchronous operation
  void run() { do_read(); }

  void do_read() {
    // Make the request empty before reading,
    // otherwise the operation behavior is undefined.
    req_ = {};

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request
    http::async_read(
        stream_, buffer_, req_,
        beast::bind_front_handler(&session::on_read, shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
      return do_close();

    if (ec)
      return fail(ec, "read");

    // Send the response
    handle_request(*doc_root_, std::move(req_), lambda_);
  }

  void on_write(bool close, beast::error_code ec,
                std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec)
      return fail(ec, "write");

    if (close) {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      return do_close();
    }

    // We're done with the response so delete it
    res_ = nullptr;

    // Read another request
    do_read();
  }

  void do_close() {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
  net::io_context &ioc_;
  tcp::acceptor acceptor_;
  std::shared_ptr<std::string const> doc_root_;

public:
  listener(net::io_context &ioc, tcp::endpoint endpoint,
           std::shared_ptr<std::string const> const &doc_root)
      : ioc_(ioc), acceptor_(net::make_strand(ioc)), doc_root_(doc_root) {
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
      fail(ec, "open");
      return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      fail(ec, "set_option");
      return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
      fail(ec, "bind");
      return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      fail(ec, "listen");
      return;
    }
  }

  // Start accepting incoming connections
  void run() { do_accept(); }

private:
  void do_accept() {
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&listener::on_accept, shared_from_this()));
  }

  void on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
      fail(ec, "accept");
    } else {
      // Create the session and run it
      std::make_shared<session>(std::move(socket), doc_root_)->run();
    }

    // Accept another connection
    do_accept();
  }
};

//------------------------------------------------------------------------------

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

int main(int argc, char *argv[]) {
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

  ipp_init();
  curl_global_init(CURL_GLOBAL_ALL);

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

  // register signal SIGINT and signal handler
  signal(SIGINT, signalHandler);

  // Check command line arguments.
  /*if (argc != 5)
  {
      std::cerr <<
          "Usage: http-server-async <address> <port> <doc_root> <threads>\n" <<
          "Example:\n" <<
          "    http-server-async 0.0.0.0 8080 . 1\n";
      return EXIT_FAILURE;
  }*/
  // auto const address = net::ip::make_address(argv[1]);
  auto const address = net::ip::make_address("0.0.0.0");
  // auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
  auto const port = static_cast<unsigned short>(SERVER_PORT);
  // auto const doc_root = std::make_shared<std::string>(argv[3]);
  auto const doc_root = std::make_shared<std::string>("htdocs_beast");
  // auto const threads = std::max<int>(1, std::atoi(argv[4]));
  int threads = std::max<int>(std::thread::hardware_concurrency() / 2, 1);

  // The io_context is required for all I/O
  net::io_context ioc{threads};

  // Create and launch a listening port
  std::make_shared<listener>(ioc, tcp::endpoint{address, port}, doc_root)
      ->run();

  // Run the I/O service on the requested number of threads
  std::vector<std::thread> v;
  v.reserve(threads - 1);
  for (auto i = threads - 1; i > 0; --i)
    v.emplace_back([&ioc] { ioc.run(); });
  ioc.run();

  curl_global_cleanup();

  if (splat_db != NULL)
    sqlite3_close(splat_db);

  std::cout << "FITSWebQL shutdown completed." << std::endl;

  return EXIT_SUCCESS;
}