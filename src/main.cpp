#define VERSION_MAJOR 4
#define VERSION_MINOR 1
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_PORT 8080
#define SERVER_STRING "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-03-10.0"
#define WASM_STRING "WASM2019-02-08.1"

inline const char *check_null(const char *str)
{
    if (str != nullptr)
        return str;
    else
        return "\"\"";
};

#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <memory>
#include <thread>
#include <shared_mutex>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <csignal>
#include <string_view>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <chrono>

#include <ipp.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/file.hpp>

#include <uWS/uWS.h>
#include <sqlite3.h>
#include <curl/curl.h>

#ifndef LOCAL
#include <libpq-fe.h>

#define FITSHOME "/home"
#define JVO_HOST "localhost"
#define JVO_USER "jvo"
#endif

#include "fits.hpp"
#include "json.h"

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::shared_mutex fits_mutex;
std::string home_dir;
int server_port = SERVER_PORT;
sqlite3 *splat_db = NULL;

void signalHandler(int signum)
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";

    // cleanup and close up stuff here
    // terminate program

    curl_global_cleanup();

    if (splat_db != NULL)
        sqlite3_close(splat_db);

    std::cout << "FITSWebQL shutdown completed." << std::endl;

    exit(signum);
}

bool is_gzip(const char *filename)
{
    int fd = open(filename, O_RDONLY);

    if (fd == -1)
        return false;

    bool ok = true;
    uint8_t header[10];

    //try to read the first 10 bytes
    ssize_t bytes_read = read(fd, header, 10);

    //test for magick numbers and the deflate compression type
    if (bytes_read == 10)
    {
        if (header[0] != 0x1f || header[1] != 0x8b || header[2] != 0x08)
            ok = false;
    }
    else
        ok = false;

    close(fd);

    return ok;
}

//resource not found
void http_not_found(uWS::HttpResponse *res)
{
    const std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    res->write(not_found.data(), not_found.length());
}

void write_status(uWS::HttpResponse *res, int code, std::string message)
{
    std::string status = "HTTP/1.1 " + std::to_string(code) + " " + message + "\r\n";
    res->write(status.data(), status.length());
}

void write_content_length(uWS::HttpResponse *res, size_t length)
{
    std::string content_length = "Content-Length: " + std::to_string(length) + "\r\n";
    res->write(content_length.data(), content_length.length());
}

void write_content_type(uWS::HttpResponse *res, std::string mime)
{
    std::string content_type = "Content-Type: " + mime + "\r\n";
    res->write(content_type.data(), content_type.length());
}

void write_key_value(uWS::HttpResponse *res, std::string key, std::string value)
{
    std::string content_type = key + ": " + value + "\r\n";
    res->write(content_type.data(), content_type.length());
}

uintmax_t ComputeFileSize(const fs::path &pathToCheck)
{
    if (fs::exists(pathToCheck) &&
        fs::is_regular_file(pathToCheck))
    {
        auto err = std::error_code{};
        auto filesize = fs::file_size(pathToCheck, err);
        if (filesize != static_cast<uintmax_t>(-1))
            return filesize;
    }

    return static_cast<uintmax_t>(-1);
}

void get_directory(uWS::HttpResponse *res, std::string dir)
{
    std::cout << "scanning directory " << dir << std::endl;

    fs::path pathToShow(dir);

    std::map<std::string, std::string> entries;

    if (fs::exists(pathToShow) && fs::is_directory(pathToShow))
    {
        for (const auto &entry : fs::directory_iterator(pathToShow))
        {
            if (!fs::exists(entry))
                continue;

            auto filename = entry.path().filename();
            auto timestamp = fs::last_write_time(entry);
            time_t cftime = std::chrono::system_clock::to_time_t(timestamp);
            std::string last_modified = std::asctime(std::localtime(&cftime));
            last_modified.pop_back();

            if (fs::is_directory(entry.status()))
            {
                if (!boost::algorithm::starts_with(filename.string(), "."))
                {
                    char *encoded = json_encode_string(filename.c_str());

                    std::string json = "{\"type\" : \"dir\", \"name\" : " + std::string(encoded) + ", \"last_modified\" : \"" + last_modified + "\"}";

                    if (encoded != NULL)
                        free(encoded);

                    std::cout << json << std::endl;

                    entries.insert(std::pair(filename, json));
                }
            }
            else if (fs::is_regular_file(entry.status()))
            {
                //check the extensions .fits or .fits.gz
                const std::string lower_filename = boost::algorithm::to_lower_copy(filename.string());

                if (boost::algorithm::ends_with(lower_filename, ".fits") || boost::algorithm::ends_with(lower_filename, ".fits.gz"))
                {

                    char *encoded = json_encode_string(filename.c_str());

                    uintmax_t filesize = ComputeFileSize(entry);

                    std::string json = "{\"type\" : \"file\", \"name\" : " + std::string(encoded) + ", \"size\" : " + std::to_string(filesize) + ", \"last_modified\" : \"" + last_modified + "\"}";

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

    if (entries.size() > 0)
    {
        for (auto const &entry : entries)
        {
            json << entry.second << ",";
        }

        //overwrite the the last ',' with a list closing character
        json.seekp(-1, std::ios_base::end);
    }

    json << "]}";

    write_status(res, 200, "OK");
    write_content_length(res, json.tellp());
    write_content_type(res, "application/json");
    write_key_value(res, "Cache-Control", "no-cache");
    write_key_value(res, "Cache-Control", "no-store");
    write_key_value(res, "Pragma", "no-cache");
    res->write("\r\n", 2);
    res->write(json.str().c_str(), json.tellp());
    res->write("\r\n\r\n", 4);
}

void get_home_directory(uWS::HttpResponse *res)
{
    if (home_dir != "")
        return get_directory(res, home_dir);
    else
        return http_not_found(res);
}

void serve_file(uWS::HttpResponse *res, std::string uri)
{
    std::string resource = "htdocs" + uri;

    //strip '?' from the requested file name
    size_t pos = resource.find("?");

    if (pos != std::string::npos)
        resource = resource.substr(0, pos);

    std::cout << "serving " << resource << std::endl;

    //mmap a disk resource
    int fd = -1;
    void *buffer = NULL;

    struct stat64 st;
    stat64(resource.c_str(), &st);
    long size = st.st_size;

    fd = open(resource.c_str(), O_RDONLY);

    if (fd != -1)
    {
        buffer = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

        if (buffer != NULL)
        {
            //const std::string header = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(size) + "\r\n\r\n";
            //res->write(header.data(), header.length());

            write_status(res, 200, "OK");
            write_content_length(res, size);

            //detect mime-types
            size_t pos = resource.find_last_of(".");

            if (pos != std::string::npos)
            {
                std::string ext = resource.substr(pos + 1, std::string::npos);

                if (ext == "htm" || ext == "html")
                    write_content_type(res, "text/html");

                if (ext == "txt")
                    write_content_type(res, "text/plain");

                if (ext == "js")
                    write_content_type(res, "application/javascript");

                if (ext == "ico")
                    write_content_type(res, "image/x-icon");

                if (ext == "png")
                    write_content_type(res, "image/png");

                if (ext == "gif")
                    write_content_type(res, "image/gif");

                if (ext == "webp")
                    write_content_type(res, "image/webp");

                if (ext == "jpg" || ext == "jpeg")
                    write_content_type(res, "image/jpeg");

                if (ext == "bpg")
                    write_content_type(res, "image/bpg");

                if (ext == "mp4")
                    write_content_type(res, "video/mp4");

                if (ext == "hevc")
                    write_content_type(res, "video/hevc");

                if (ext == "css")
                    write_content_type(res, "text/css");

                if (ext == "pdf")
                    write_content_type(res, "application/pdf");

                if (ext == "svg")
                    write_content_type(res, "image/svg+xml");

                if (ext == "wasm")
                    write_content_type(res, "application/wasm");
            }

            res->write("\r\n", 2);
            res->write((const char *)buffer, size);
            res->write("\r\n\r\n", 4);
        }
        else
        {
            perror("error mapping a file");
            http_not_found(res);
        }

        if (munmap(buffer, size) == -1)
            perror("un-mapping error");

        close(fd);
    }
    else
        http_not_found(res);
}

void http_fits_response(uWS::HttpResponse *res, std::vector<std::string> datasets, bool composite, bool is_optical, bool has_fits)
{
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
    html.append("<link href=\"https://fonts.googleapis.com/css?family=Inconsolata\" rel=\"stylesheet\"/>\n");
    html.append("<script src=\"https://d3js.org/d3.v5.min.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/reconnecting-websocket.js\"></script>\n");
    html.append("<script src=\"//cdnjs.cloudflare.com/ajax/libs/numeral.js/2.0.6/numeral.min.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/ra_dec_conversion.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/sylvester.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/shortcut.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/colourmaps.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/lz4.min.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/marchingsquares-isocontours.min.js\"></script>\n");
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/marchingsquares-isobands.min.js\"></script>\n");

    //hevc wasm decoder
    html.append("<script src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/hevc_" WASM_STRING ".js\"></script>\n");
    html.append(R"(<script>
        Module.onRuntimeInitialized = async _ => {
            api = {                
                hevc_init: Module.cwrap('hevc_init', '', []), 
                hevc_destroy: Module.cwrap('hevc_destroy', '', []),                
                hevc_decode_nal_unit: Module.cwrap('hevc_decode_nal_unit', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'string']),               
            };                   
        };
    </script>)");

    //bootstrap
    html.append("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no, minimum-scale=1, maximum-scale=1\">\n");
    html.append("<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css\">\n");
    html.append("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/jquery.min.js\"></script>\n");
    html.append("<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js\"></script>\n");

    //FITSWebQL main JavaScript + CSS
    html.append("<script src=\"fitswebql.js?" VERSION_STRING "\"></script>\n");
    html.append("<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/fitswebql/fitswebql.css\"/>\n");

    //HTML content
    html.append("<title>FITSWebQL</title></head><body>\n");
    html.append("<div id='votable' style='width: 0; height: 0;' data-va_count='" + std::to_string(datasets.size()) + "' ");

    if (datasets.size() == 1)
        html.append("data-datasetId='" + datasets[0] + "' ");
    else
    {
        for (int i = 0; i < datasets.size(); i++)
            html.append("data-datasetId" + std::to_string(i + 1) + "='" + datasets[i] + "' ");

        if (composite && datasets.size() <= 3)
            html.append("data-composite='1' ");
    }

    html.append("data-root-path='/" +
                std::string("fitswebql") +
                "/' data-server-version='" + VERSION_STRING + "' data-server-string='" + SERVER_STRING + "' data-server-mode='" + "SERVER" +
                "' data-has-fits='" + std::to_string(has_fits) + "' data-is-optical='" + std::to_string(is_optical) + "'></div>\n");

#ifdef PRODUCTION
    html.append(R"(<script>
        var WS_SOCKET = 'wss://';
        </script>)");
#else
    html.append(R"(<script>
        var WS_SOCKET = 'ws://';
        </script>)");
#endif

    //the page entry point
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

    write_status(res, 200, "OK");
    write_content_length(res, html.length());
    res->write("\r\n", 2);
    res->write(html.data(), html.length());
    res->write("\r\n\r\n", 4);
}

#ifndef LOCAL
PGconn *jvo_db_connect(std::string db)
{
    PGconn *jvo_db = NULL;

    std::string conn_str = "dbname=" + db + " host=" + JVO_HOST + " user=" + JVO_USER;

    jvo_db = PQconnectdb(conn_str.c_str());

    if (PQstatus(jvo_db) != CONNECTION_OK)
    {
        fprintf(stderr, "PostgreSQL connection failed: %s\n", PQerrorMessage(jvo_db));
        PQfinish(jvo_db);
        jvo_db = NULL;
    }
    else
        printf("PostgreSQL connection successful.\n");

    return jvo_db;
}

std::string get_jvo_path(PGconn *jvo_db, std::string db, std::string table, std::string data_id)
{
    std::string path;

    std::string sql_str = "SELECT path FROM " + table + " WHERE data_id = '" + data_id + "';";

    PGresult *res = PQexec(jvo_db, sql_str.c_str());
    int status = PQresultStatus(res);

    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        path = std::string(FITSHOME) + "/" + db + "/";

        size_t pos = table.find(".");

        if (pos == std::string::npos)
            path += std::string((const char *)PQgetvalue(res, 0, 0));
        else
            path += boost::algorithm::to_upper_copy(table.substr(0, pos)) + "/" + std::string((const char *)PQgetvalue(res, 0, 0));

        PQclear(res);
    }

    return path;
}
#endif

void execute_fits(uWS::HttpResponse *res, std::string dir, std::string ext, std::string db, std::string table, std::vector<std::string> datasets, bool composite, bool is_optical, std::string flux)
{
    bool has_fits = true;

#ifndef LOCAL
    PGconn *jvo_db = NULL;

    if (db != "")
        jvo_db = jvo_db_connect(db);
#endif

    int va_count = datasets.size();

    for (auto const &data_id : datasets)
    {
        std::shared_lock<std::shared_mutex> lock(fits_mutex);
        auto item = DATASETS.find(data_id);
        lock.unlock();

        if (item == DATASETS.end())
        {
            //set has_fits to false and load the FITS dataset
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

            if (path != "")
            {
                bool is_compressed = false;
                std::string lower_path = boost::algorithm::to_lower_copy(path);
                if (boost::algorithm::ends_with(lower_path, ".gz"))
                    is_compressed = is_gzip(path.c_str());

                //load FITS data in a separate thread
                std::thread(&FITS::from_path, fits, path, is_compressed, flux, is_optical, va_count).detach();
            }
            else
            {
                //the last resort
                std::string url = std::string("http://") + JVO_FITS_SERVER + ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB + "&table=cube&data_id=" + data_id + "_00_00_00";

                //download FITS data from a URL in a separate thread
                std::thread(&FITS::from_url, fits, url, flux, is_optical, va_count).detach();
            }
        }
        else
        {
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

    return http_fits_response(res, datasets, composite, is_optical, has_fits);
}

void ipp_init()
{
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
    if (ippStsNoErr == status)
    {
        emask = ippGetEnabledCpuFeatures();
        printf("Features supported by CPU\tby IPP\n");
        printf("-----------------------------------------\n");
        printf("  ippCPUID_MMX        = ");
        printf("%c\t%c\t", (mask & ippCPUID_MMX) ? 'Y' : 'N', (emask & ippCPUID_MMX) ? 'Y' : 'N');
        printf("Intel(R) Architecture MMX technology supported\n");
        printf("  ippCPUID_SSE        = ");
        printf("%c\t%c\t", (mask & ippCPUID_SSE) ? 'Y' : 'N', (emask & ippCPUID_SSE) ? 'Y' : 'N');
        printf("Intel(R) Streaming SIMD Extensions\n");
        printf("  ippCPUID_SSE2       = ");
        printf("%c\t%c\t", (mask & ippCPUID_SSE2) ? 'Y' : 'N', (emask & ippCPUID_SSE2) ? 'Y' : 'N');
        printf("Intel(R) Streaming SIMD Extensions 2\n");
        printf("  ippCPUID_SSE3       = ");
        printf("%c\t%c\t", (mask & ippCPUID_SSE3) ? 'Y' : 'N', (emask & ippCPUID_SSE3) ? 'Y' : 'N');
        printf("Intel(R) Streaming SIMD Extensions 3\n");
        printf("  ippCPUID_SSSE3      = ");
        printf("%c\t%c\t", (mask & ippCPUID_SSSE3) ? 'Y' : 'N', (emask & ippCPUID_SSSE3) ? 'Y' : 'N');
        printf("Intel(R) Supplemental Streaming SIMD Extensions 3\n");
        printf("  ippCPUID_MOVBE      = ");
        printf("%c\t%c\t", (mask & ippCPUID_MOVBE) ? 'Y' : 'N', (emask & ippCPUID_MOVBE) ? 'Y' : 'N');
        printf("The processor supports MOVBE instruction\n");
        printf("  ippCPUID_SSE41      = ");
        printf("%c\t%c\t", (mask & ippCPUID_SSE41) ? 'Y' : 'N', (emask & ippCPUID_SSE41) ? 'Y' : 'N');
        printf("Intel(R) Streaming SIMD Extensions 4.1\n");
        printf("  ippCPUID_SSE42      = ");
        printf("%c\t%c\t", (mask & ippCPUID_SSE42) ? 'Y' : 'N', (emask & ippCPUID_SSE42) ? 'Y' : 'N');
        printf("Intel(R) Streaming SIMD Extensions 4.2\n");
        printf("  ippCPUID_AVX        = ");
        printf("%c\t%c\t", (mask & ippCPUID_AVX) ? 'Y' : 'N', (emask & ippCPUID_AVX) ? 'Y' : 'N');
        printf("Intel(R) Advanced Vector Extensions instruction set\n");
        printf("  ippAVX_ENABLEDBYOS  = ");
        printf("%c\t%c\t", (mask & ippAVX_ENABLEDBYOS) ? 'Y' : 'N', (emask & ippAVX_ENABLEDBYOS) ? 'Y' : 'N');
        printf("The operating system supports Intel(R) AVX\n");
        printf("  ippCPUID_AES        = ");
        printf("%c\t%c\t", (mask & ippCPUID_AES) ? 'Y' : 'N', (emask & ippCPUID_AES) ? 'Y' : 'N');
        printf("Intel(R) AES instruction\n");
        printf("  ippCPUID_SHA        = ");
        printf("%c\t%c\t", (mask & ippCPUID_SHA) ? 'Y' : 'N', (emask & ippCPUID_SHA) ? 'Y' : 'N');
        printf("Intel(R) SHA new instructions\n");
        printf("  ippCPUID_CLMUL      = ");
        printf("%c\t%c\t", (mask & ippCPUID_CLMUL) ? 'Y' : 'N', (emask & ippCPUID_CLMUL) ? 'Y' : 'N');
        printf("PCLMULQDQ instruction\n");
        printf("  ippCPUID_RDRAND     = ");
        printf("%c\t%c\t", (mask & ippCPUID_RDRAND) ? 'Y' : 'N', (emask & ippCPUID_RDRAND) ? 'Y' : 'N');
        printf("Read Random Number instructions\n");
        printf("  ippCPUID_F16C       = ");
        printf("%c\t%c\t", (mask & ippCPUID_F16C) ? 'Y' : 'N', (emask & ippCPUID_F16C) ? 'Y' : 'N');
        printf("Float16 instructions\n");
        printf("  ippCPUID_AVX2       = ");
        printf("%c\t%c\t", (mask & ippCPUID_AVX2) ? 'Y' : 'N', (emask & ippCPUID_AVX2) ? 'Y' : 'N');
        printf("Intel(R) Advanced Vector Extensions 2 instruction set\n");
        printf("  ippCPUID_AVX512F    = ");
        printf("%c\t%c\t", (mask & ippCPUID_AVX512F) ? 'Y' : 'N', (emask & ippCPUID_AVX512F) ? 'Y' : 'N');
        printf("Intel(R) Advanced Vector Extensions 3.1 instruction set\n");
        printf("  ippCPUID_AVX512CD   = ");
        printf("%c\t%c\t", (mask & ippCPUID_AVX512CD) ? 'Y' : 'N', (emask & ippCPUID_AVX512CD) ? 'Y' : 'N');
        printf("Intel(R) Advanced Vector Extensions CD (Conflict Detection) instruction set\n");
        printf("  ippCPUID_AVX512ER   = ");
        printf("%c\t%c\t", (mask & ippCPUID_AVX512ER) ? 'Y' : 'N', (emask & ippCPUID_AVX512ER) ? 'Y' : 'N');
        printf("Intel(R) Advanced Vector Extensions ER instruction set\n");
        printf("  ippCPUID_ADCOX      = ");
        printf("%c\t%c\t", (mask & ippCPUID_ADCOX) ? 'Y' : 'N', (emask & ippCPUID_ADCOX) ? 'Y' : 'N');
        printf("ADCX and ADOX instructions\n");
        printf("  ippCPUID_RDSEED     = ");
        printf("%c\t%c\t", (mask & ippCPUID_RDSEED) ? 'Y' : 'N', (emask & ippCPUID_RDSEED) ? 'Y' : 'N');
        printf("The RDSEED instruction\n");
        printf("  ippCPUID_PREFETCHW  = ");
        printf("%c\t%c\t", (mask & ippCPUID_PREFETCHW) ? 'Y' : 'N', (emask & ippCPUID_PREFETCHW) ? 'Y' : 'N');
        printf("The PREFETCHW instruction\n");
        printf("  ippCPUID_KNC        = ");
        printf("%c\t%c\t", (mask & ippCPUID_KNC) ? 'Y' : 'N', (emask & ippCPUID_KNC) ? 'Y' : 'N');
        printf("Intel(R) Xeon Phi(TM) Coprocessor instruction set\n");
    }
}

int main(int argc, char *argv[])
{
    ipp_init();
    curl_global_init(CURL_GLOBAL_ALL);

    std::cout << SERVER_STRING << " (" << VERSION_STRING << ")" << std::endl;
    std::cout << "Browser URL: http://localhost:" << server_port << std::endl;

    int rc = sqlite3_open_v2("splatalogue_v3.db", &splat_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

    if (rc)
    {
        fprintf(stderr, "Can't open local splatalogue database: %s\n", sqlite3_errmsg(splat_db));
        sqlite3_close(splat_db);
        splat_db = NULL;
    }
    struct passwd *passwdEnt = getpwuid(getuid());
    home_dir = passwdEnt->pw_dir;

    // register signal SIGINT and signal handler
    signal(SIGINT, signalHandler);

    //parse local command-line options
    if (argc > 2)
    {
        for (int i = 1; i < argc - 1; i++)
        {
            const char *key = argv[i];
            const char *value = argv[i + 1];

            if (!strcmp(key, "--port"))
                server_port = atoi(value);

            if (!strcmp(key, "--home"))
                home_dir = std::string(value);
        }
    }

    int no_threads = MIN(MAX(std::thread::hardware_concurrency() / 2, 1), 4);
    std::vector<std::thread *> threads(no_threads);
    std::transform(threads.begin(), threads.end(), threads.begin(), [](std::thread *t) {
        return new std::thread([]() {
            uWS::Hub h;

            h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t, size_t) {
                std::string uri = req.getUrl().toString();

                std::cout << "HTTP request for " << uri << std::endl;

                //root
                if (uri == "/")
#ifdef LOCAL
                    return serve_file(res, "/local.html");
#else
                                                                                                     return serve_file(res, "/test.html");
#endif

                if (uri.find("/get_directory") != std::string::npos)
                {
                    std::string dir;

                    //get a position of '?'
                    size_t pos = uri.find("?");

                    if (pos != std::string::npos)
                    {
                        std::string query = uri.substr(pos + 1, std::string::npos);
                        std::cout << "query: (" << query << ")" << std::endl;

                        std::vector<std::string> params;
                        boost::split(params, query, [](char c) { return c == '&'; });

                        for (auto const &s : params)
                        {
                            //find '='
                            size_t pos = s.find("=");

                            if (pos != std::string::npos)
                            {
                                std::string key = s.substr(0, pos);
                                std::string value = s.substr(pos + 1, std::string::npos);

                                if (key == "dir")
                                {
                                    CURL *curl = curl_easy_init();

                                    char *str = curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
                                    dir = std::string(str);
                                    curl_free(str);

                                    curl_easy_cleanup(curl);
                                }
                            }
                        }
                    }

                    if (dir != "")
                        return get_directory(res, dir);
                    else
                        return get_home_directory(res);
                }

                //FITSWebQL entry
                if (uri.find("FITSWebQL.html") != std::string::npos)
                {
                    //get a position of '?'
                    size_t pos = uri.find("?");

                    if (pos != std::string::npos)
                    {
                        std::vector<std::string> datasets;
                        std::string dir, ext, db, table, flux;
                        bool composite = false;
                        bool optical = false;

                        //using std::string for now as std::string_view is broken
                        //in the Intel C++ compiler v19 Update 1
                        //(works OK with v19 Update 2, which cannot be installed on CentOS 6)
                        //LLVM CLANG works OK with std::string_view

                        std::string query = uri.substr(pos + 1, std::string::npos);
                        std::cout << "query: (" << query << ")" << std::endl;

                        std::vector<std::string> params;
                        boost::split(params, query, [](char c) { return c == '&'; });

                        CURL *curl = curl_easy_init();

                        for (auto const &s : params)
                        {
                            //find '='
                            size_t pos = s.find("=");

                            if (pos != std::string::npos)
                            {
                                std::string key = s.substr(0, pos);
                                std::string value = s.substr(pos + 1, std::string::npos);

                                if (key.find("dataset") != std::string::npos)
                                {
                                    char *str = curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
                                    datasets.push_back(std::string(str));
                                    curl_free(str);
                                }

                                if (key.find("filename") != std::string::npos)
                                {
                                    char *str = curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
                                    datasets.push_back(std::string(str));
                                    curl_free(str);
                                }

                                if (key == "dir")
                                {
                                    char *str = curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
                                    dir = std::string(str);
                                    curl_free(str);
                                }

                                if (key == "ext")
                                {
                                    char *str = curl_easy_unescape(curl, value.c_str(), value.length(), NULL);
                                    ext = std::string(str);
                                    curl_free(str);
                                }

                                if (key == "db")
                                    db = value;

                                if (key == "table")
                                    table = value;

                                if (key == "flux")
                                {
                                    //validate the flux value
                                    std::set<std::string> valid_values;
                                    valid_values.insert("linear");
                                    valid_values.insert("logistic");
                                    valid_values.insert("ratio");
                                    valid_values.insert("square");
                                    valid_values.insert("legacy");

                                    if (valid_values.find(value) != valid_values.end())
                                        flux = value;
                                }

                                if (key == "view")
                                {
                                    if (value.find("composite") != std::string::npos)
                                        composite = true;

                                    if (value.find("optical") != std::string::npos)
                                        optical = true;
                                }
                            }
                        }

                        curl_easy_cleanup(curl);

                        //sane defaults
                        {
                            if (db.find("hsc") != std::string::npos)
                            {
                                optical = true;
                                flux = "ratio";
                            }

                            if (table.find("fugin") != std::string::npos)
                                flux = "logistic";
                        }

                        std::cout << "dir:" << dir << ", ext:" << ext << ", db:" << db << ", table:" << table << ", composite:" << composite << ", optical:" << optical << ", flux:" << flux << ", ";
                        for (auto const &dataset : datasets)
                            std::cout << dataset << " ";
                        std::cout << std::endl;

                        if (datasets.size() == 0)
                        {
                            const std::string not_found = "ERROR: please specify at least one dataset in the URL parameters list.";
                            res->end(not_found.data(), not_found.length());
                            return;
                        }
                        else
                            return execute_fits(res, dir, ext, db, table, datasets, composite, optical, flux);
                    }
                    else
                    {
                        const std::string not_found = "ERROR: URL parameters not found.";
                        res->end(not_found.data(), not_found.length());
                        return;
                    }
                }

                return serve_file(res, uri);
            });

            h.onConnection([](uWS::WebSocket<uWS::SERVER> *ws, uWS::HttpRequest req) {
                std::string url = req.getUrl().toString();
                std::cout << "[µWS] onConnection URL(" << url << ")" << std::endl;
            });

            h.onDisconnection([](uWS::WebSocket<uWS::SERVER> *ws, int code, char *message, size_t length) {
                std::string msg = std::string(message, length);
                std::cout << "[µWS] onDisconnection " << msg << std::endl;
            });

            h.onMessage([](uWS::WebSocket<uWS::SERVER> *ws, char *message, size_t length, uWS::OpCode opCode) {
                //ws->send(message, length, opCode);

                std::string msg = std::string(message, length);

                std::cout << "[µWS] " << msg << std::endl;
            });

            // This makes use of the SO_REUSEPORT of the Linux kernel
            // Other solutions include listening to one port per thread
            // with or without some kind of proxy inbetween
            if (!h.listen(server_port, nullptr, uS::ListenOptions::REUSE_PORT))
            {
                std::cout << "Failed to listen\n";
            }

            std::cout << "Launching a uWS::HTTP/WS thread\n";

            h.run();
        });
    });

    std::for_each(threads.begin(), threads.end(), [](std::thread *t) {
        t->join();
    });

    if (splat_db != NULL)
        sqlite3_close(splat_db);
}
