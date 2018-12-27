#define VERSION_MAJOR 4
#define VERSION_MINOR 1
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_STRING "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2018-12-27.0"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#include <thread>
#include <algorithm>
#include <iostream>

#include <string.h>

#include <uWS/uWS.h>
#include <sqlite3.h>

sqlite3 *splat_db;

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

    std::vector<std::thread *> threads(MAX(std::thread::hardware_concurrency() / 2, 1));
    std::transform(threads.begin(), threads.end(), threads.begin(), [](std::thread *t) {
        return new std::thread([]() {
            uWS::Hub h;

            h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t, size_t) {
                std::string url = req.getUrl().toString();

                std::cout << "HTTP request for " << url << std::endl;

                if (url == "/")
                {
                    const std::string s = "<h1>Hello " SERVER_STRING "!</h1>";
                    res->end(s.data(), s.length());
                    return;
                }

                //res->end(nullptr, 0);
                const std::string not_found = "HTTP/1.1 404 Not Found\r\n\r\n";
                res->write(not_found.data(), not_found.length());
            });

            h.onMessage([](uWS::WebSocket<uWS::SERVER> *ws, char *message, size_t length, uWS::OpCode opCode) {
                ws->send(message, length, opCode);
            });

            // This makes use of the SO_REUSEPORT of the Linux kernel
            // Other solutions include listening to one port per thread
            // with or without some kind of proxy inbetween
            if (!h.listen(8080, nullptr, uS::ListenOptions::REUSE_PORT))
            {
                std::cout << "Failed to listen" << std::endl;
            }

            std::cout << "Launching a uWS HTTP/WS thread" << std::endl;

            h.run();
        });
    });

    std::for_each(threads.begin(), threads.end(), [](std::thread *t) {
        t->join();
    });

    if (splat_db != NULL)
        sqlite3_close(splat_db);

    std::cout << "FITSWebQL clean shutdown completed." << std::endl;
}
