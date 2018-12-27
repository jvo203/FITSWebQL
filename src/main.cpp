#define VERSION_MAJOR 4
#define VERSION_MINOR 1
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_STRING "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2018-12-27.0"

#include <iostream>
using namespace std;

#include <sqlite3.h>

sqlite3 *splat_db;

int main(int argc, char *argv[])
{
    cout << SERVER_STRING << " (" << VERSION_STRING << ")" << endl;    

    int rc = sqlite3_open_v2("splatalogue_v3.db", &splat_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

    if (rc)
    {
        fprintf(stderr, "Can't open local splatalogue database: %s\n", sqlite3_errmsg(splat_db));
        sqlite3_close(splat_db);
        splat_db = NULL;
    }

    if (splat_db != NULL)
        sqlite3_close(splat_db);
}
