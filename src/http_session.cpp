//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//

#include "http_session.hpp"
#include "websocket_session.hpp"
#include <boost/config.hpp>
#include <iostream>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "fitswebql.hpp"
#include "json.h"
#include <curl/curl.h>
#include <filesystem>

namespace fs = std::filesystem;

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

std::string execute_fits(boost::shared_ptr<shared_state> const& state, std::string dir, std::string ext, std::string db,
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
      auto item = state->get_dataset(data_id);

    if (item == nullptr) {
      // set has_fits to false and load the FITS dataset
      has_fits = false;
      std::shared_ptr<FITS> fits(new FITS(data_id, flux));
      
      state->insert_dataset(data_id, fits);

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
                    va_count, state)
            .detach();
      } else {
        // the last resort
        std::string url = std::string("http://") + JVO_FITS_SERVER +
                          ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB +
                          "&table=cube&data_id=" + data_id + "_00_00_00";

        // download FITS data from a URL in a separate thread
        std::thread(&FITS::from_url, fits, url, flux, va_count, state).detach();
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

  return http_fits_response(datasets, composite, has_fits);
}

#define BOOST_NO_CXX14_GENERIC_LAMBDAS

//------------------------------------------------------------------------------

// Return a reasonable mime type based on the extension of a file.
beast::string_view
mime_type(beast::string_view path)
{
    using beast::iequals;
    auto const ext = [&path]
    {
        auto const pos = path.rfind(".");
        if(pos == beast::string_view::npos)
            return beast::string_view{};
        return path.substr(pos);
    }();
    if(iequals(ext, ".htm"))  return "text/html";
    if(iequals(ext, ".html")) return "text/html";
    if(iequals(ext, ".php"))  return "text/html";
    if(iequals(ext, ".css"))  return "text/css";
    if(iequals(ext, ".txt"))  return "text/plain";
    if(iequals(ext, ".js"))   return "application/javascript";
    if(iequals(ext, ".wasm")) return "application/wasm";
    if(iequals(ext, ".json")) return "application/json";
    if(iequals(ext, ".xml"))  return "application/xml";
    if(iequals(ext, ".swf"))  return "application/x-shockwave-flash";
    if(iequals(ext, ".flv"))  return "video/x-flv";
    if(iequals(ext, ".png"))  return "image/png";
    if(iequals(ext, ".jpe"))  return "image/jpeg";
    if(iequals(ext, ".jpeg")) return "image/jpeg";
    if(iequals(ext, ".jpg"))  return "image/jpeg";
    if(iequals(ext, ".gif"))  return "image/gif";
    if(iequals(ext, ".bmp"))  return "image/bmp";
    if(iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
    if(iequals(ext, ".tiff")) return "image/tiff";
    if(iequals(ext, ".tif"))  return "image/tiff";
    if(iequals(ext, ".svg"))  return "image/svg+xml";
    if(iequals(ext, ".svgz")) return "image/svg+xml";
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
std::string
path_cat(
    beast::string_view base,
    beast::string_view path)
{
    if(base.empty())
        return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for(auto& c : result)
        if(c == '/')
            c = path_separator;
#else
    char constexpr path_separator = '/';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
    class Body, class Allocator,
    class Send>
void
handle_request(    
    boost::shared_ptr<shared_state> const& state,
    http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send)
{   
    beast::string_view doc_root = state->doc_root();
    beast::string_view home_dir = state->home_dir();
    sqlite3 * splat_db = state->splat_db();

    // Returns a bad request response
    auto const bad_request =
    [&req](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found =
    [&req](beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error =
    [&req](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const http_accepted =
    [&req](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::accepted, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

  // Returns a JSON response with caching disabled
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

   // Returns a valid spectrum
  auto const spectrum = [&req, &state, &not_found, &http_accepted, &uncached_json, &server_error](std::string datasetid) {
    auto fits = state->get_dataset(datasetid);

      if (fits == nullptr)   
        return not_found(datasetid);
      else {        
        if (fits->has_error)
          return not_found(datasetid);
        else {
          /*std::unique_lock<std::mutex> data_lck(fits->data_mtx);
          while (!fits->processed_data)
            fits->data_cv.wait(data_lck);*/

          if (!fits->has_data)
            return http_accepted(datasetid);
            //return not_found(datasetid);
          else {            
            std::ostringstream json;

            fits->to_json(json);

            if (json.tellp() > 0) {
              std::string body = json.str();
              return uncached_json(body);
            } else {
              return server_error(datasetid + "/get_spectrum");
            }
          }
        }
      }
  };

    // Make sure we can handle the method
    if( req.method() != http::verb::get &&
        req.method() != http::verb::head)
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
        body = get_directory(std::string(home_dir));
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
      return send(spectrum(datasetid));      
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
            execute_fits(state, dir, ext, db, table, datasets, composite, flux)));
    } else {
      return send(not_found("ERROR: URL parameters not found."));
    }
  }

    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
        req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        return send(bad_request("Illegal request-target"));

    // Build the path to the requested file
    std::string path = path_cat(doc_root, strip_path(req.target()));
    if(req.target().back() == '/')
    {        
#ifdef LOCAL
        path.append("local.html");
#else
        path.append("test.html");
#endif
    }

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if(ec == boost::system::errc::no_such_file_or_directory)
        return send(not_found(req.target()));

    // Handle an unknown error
    if(ec)
        return send(server_error(ec.message()));

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to HEAD request
    if(req.method() == http::verb::head)
    {
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    // Respond to GET request
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}

//------------------------------------------------------------------------------

struct http_session::send_lambda
{
    http_session& self_;

    explicit
    send_lambda(http_session& self)
        : self_(self)
    {
    }

    template<bool isRequest, class Body, class Fields>
    void
    operator()(http::message<isRequest, Body, Fields>&& msg) const
    {
        // The lifetime of the message has to extend
        // for the duration of the async operation so
        // we use a shared_ptr to manage it.
        auto sp = boost::make_shared<
            http::message<isRequest, Body, Fields>>(std::move(msg));

        // Write the response
        auto self = self_.shared_from_this();
        http::async_write(
            self_.stream_,
            *sp,
            [self, sp](beast::error_code ec, std::size_t bytes)
            {
                self->on_write(ec, bytes, sp->need_eof());
            });
    }
};

//------------------------------------------------------------------------------

http_session::
http_session(
    tcp::socket&& socket,
    boost::shared_ptr<shared_state> const& state)
    : stream_(std::move(socket))
    , state_(state)
{
}

void
http_session::
run()
{
    do_read();
}

// Report a failure
void
http_session::
fail(beast::error_code ec, char const* what)
{
    // Don't report on canceled operations
    if(ec == net::error::operation_aborted)
        return;

    std::cerr << what << ": " << ec.message() << "\n";
}

void
http_session::
do_read()
{
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(10000);

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request
    http::async_read(
        stream_,
        buffer_,
        parser_->get(),
        beast::bind_front_handler(
            &http_session::on_read,
            shared_from_this()));
}

void
http_session::
on_read(beast::error_code ec, std::size_t)
{
    // This means they closed the connection
    if(ec == http::error::end_of_stream)
    {
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    // Handle the error, if any
    if(ec)
        return fail(ec, "read");

    // See if it is a WebSocket Upgrade
    if(websocket::is_upgrade(parser_->get()))
    {
        // Create a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        boost::make_shared<websocket_session>(
            stream_.release_socket(),
                state_)->run(parser_->release());
        return;
    }

    // Send the response
#ifndef BOOST_NO_CXX14_GENERIC_LAMBDAS
    //
    // The following code requires generic
    // lambdas, available in C++14 and later.
    //
    handle_request(        
        state_, 
        std::move(req_),
        [this](auto&& response)
        {
            // The lifetime of the message has to extend
            // for the duration of the async operation so
            // we use a shared_ptr to manage it.
            using response_type = typename std::decay<decltype(response)>::type;
            auto sp = boost::make_shared<response_type>(std::forward<decltype(response)>(response));

        #if 0
            // NOTE This causes an ICE in gcc 7.3
            // Write the response
            http::async_write(this->stream_, *sp,
                [self = shared_from_this(), sp](
                    beast::error_code ec, std::size_t bytes)
                {
                    self->on_write(ec, bytes, sp->need_eof()); 
                });
        #else
            // Write the response
            auto self = shared_from_this();
            http::async_write(stream_, *sp,
                [self, sp](
                    beast::error_code ec, std::size_t bytes)
                {
                    self->on_write(ec, bytes, sp->need_eof()); 
                });
        #endif
        });
#else
    //
    // This code uses the function object type send_lambda in
    // place of a generic lambda which is not available in C++11
    //
    handle_request(        
        state_,
        parser_->release(),
        send_lambda(*this));

#endif
}

void
http_session::
on_write(beast::error_code ec, std::size_t, bool close)
{
    // Handle the error, if any
    if(ec)
        return fail(ec, "write");

    if(close)
    {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    // Read another request
    do_read();
}
