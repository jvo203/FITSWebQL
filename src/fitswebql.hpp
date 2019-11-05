#pragma once

#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SERVER_PORT 8080
#define BEACON_PORT 50000
#define SERVER_STRING                                                          \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)
#define VERSION_STRING "SV2019-11-05.0"
#define WASM_STRING "WASM2019-02-08.1"

#define PROGRESS_TIMEOUT 0.25

#include <iostream>
#include <sstream> 
#include <string>
#include <shared_mutex>
#include <set>

#ifdef CLUSTER
#include <czmq.h>

inline std::set<std::string> cluster;
inline std::shared_mutex cluster_mtx;

inline bool cluster_contains_node(std::string node)
{
  std::shared_lock<std::shared_mutex> lock(cluster_mtx);

  if(cluster.find(node) == cluster.end())
    return false;
  else
    return true;
}

inline void cluster_insert_node(std::string node)
{
  std::lock_guard<std::shared_mutex> guard(cluster_mtx);
  cluster.insert(node);
}

inline void cluster_erase_node(std::string node)
{
  std::lock_guard<std::shared_mutex> guard(cluster_mtx);
  cluster.erase(node);
}
#endif

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

inline std::mutex PrintThread::_mutexPrint{};