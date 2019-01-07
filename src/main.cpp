#define VERSION_MAJOR 4
#define VERSION_MINOR 1
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_STRING "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-01-06.0"
#define WASM_STRING "WASM2018-12-17.0"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <memory>
#include <thread>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <csignal>
#include <string_view>
#include <string>
#include <unordered_map>

#include <boost/algorithm/string.hpp>

#include <uWS/uWS.h>
#include <sqlite3.h>

#include "fits.hpp"

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::mutex fits_mutex;

sqlite3 *splat_db = NULL;

void signalHandler(int signum)
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";

    // cleanup and close up stuff here
    // terminate program

    if (splat_db != NULL)
        sqlite3_close(splat_db);

    std::cout << "FITSWebQL shutdown completed." << std::endl;

    exit(signum);
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
                    write_content_type(res, "application/javascript; charset=utf-8");

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

void http_fits_response(uWS::HttpResponse *res, std::vector<std::string> datasets, bool composite, bool has_fits)
{
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
    html.append("<link href=\"https://fonts.googleapis.com/css?family=Inconsolata\" rel=\"stylesheet\"/>\n");
    html.append("<link href=\"https://fonts.googleapis.com/css?family=Lato\" rel=\"stylesheet\"/>\n");
    html.append("<script src=\"https://d3js.org/d3.v4.min.js\"></script>\n");
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
                "' data-has-fits='" + std::to_string(has_fits) + "'></div>\n");

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

void execute_fits(uWS::HttpResponse *res, std::string db, std::string table, std::vector<std::string> datasets, bool composite, std::string flux)
{
    bool has_fits = true;

    //get_jvo_db

    for (auto const &data_id : datasets)
    {
        std::lock_guard<std::mutex> lock(fits_mutex);
        auto item = DATASETS.find(data_id);

        if (item == DATASETS.end())
        {
            //set has_fits to false and load the FITS dataset
            has_fits = false;
            std::shared_ptr<FITS> fits(new FITS(data_id, flux));
            DATASETS.insert(std::pair(data_id, fits));

            //get_jvo_path

            //launch std::thread with fits            
        }
        else
        {
            auto fits = item->second;

            has_fits = has_fits && fits->has_data;
            fits->update_timestamp();
        }
    }

    std::cout << "has_fits: " << has_fits << std::endl;

    return http_fits_response(res, datasets, composite, has_fits);
}

int main(int argc, char *argv[])
{
    std::cout << SERVER_STRING << " (" << VERSION_STRING << ")" << std::endl;

    int rc = sqlite3_open_v2("splatalogue_v3.db", &splat_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

    if (rc)
    {
        fprintf(stderr, "Can't open local splatalogue database: %s\n", sqlite3_errmsg(splat_db));
        sqlite3_close(splat_db);
        splat_db = NULL;
    }

    // register signal SIGINT and signal handler
    signal(SIGINT, signalHandler);

    std::vector<std::thread *> threads(MAX(std::thread::hardware_concurrency() / 2, 1));
    std::transform(threads.begin(), threads.end(), threads.begin(), [](std::thread *t) {
        return new std::thread([]() {
            uWS::Hub h;

            h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t, size_t) {
                std::string uri = req.getUrl().toString();

                std::cout << "HTTP request for " << uri << std::endl;

                //root
                if (uri == "/")
                    return serve_file(res, "/test.html");

                //FITSWebQL entry
                if (uri.find("FITSWebQL.html") != std::string::npos)
                {
                    //get a position of '?'
                    size_t pos = uri.find("?");

                    if (pos != std::string::npos)
                    {
                        std::vector<std::string> datasets;
                        std::string db, table, flux;
                        bool composite = false;

                        //using std::string for now as std::string_view is broken
                        //in the Intel C++ compiler
                        //LLVM CLANG works OK with std::string_view

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

                                if (key.find("dataset") != std::string::npos)
                                    datasets.push_back(value);

                                if (key == "db")
                                    db = value;

                                if (key == "table")
                                    table = value;

                                if (key == "flux")
                                    flux = value;

                                if (key == "view")
                                {
                                    if (value == "composite")
                                        composite = true;
                                }
                            }
                        }

                        std::cout << "db:" << db << ", table:" << table << ", composite:" << composite << ", flux:" << flux << ", ";
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
                            return execute_fits(res, db, table, datasets, composite, flux);
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
            if (!h.listen(8080, nullptr, uS::ListenOptions::REUSE_PORT))
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
