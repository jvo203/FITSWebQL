#pragma once

#include <chrono>
#include <mutex>
#include <set>
#include <unordered_map>
#include <shared_mutex>

using namespace std::chrono;

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#include "App.h"

typedef uWS::WebSocket<false, true> TWebSocket;
typedef std::set<TWebSocket *> TWebSocketList;

typedef std::unordered_map<std::string, TWebSocketList> progress_list;
inline std::shared_mutex m_progress_mutex;
inline progress_list m_progress;

#define uWS_PROGRESS_TIMEOUT 0.25

struct UserSession
{
  // session management
  boost::uuids::uuid session_id;
  system_clock::time_point ts;
  std::shared_mutex ts_mtx;

  // the main fields
  std::string primary_id;
  std::vector<std::string> ids;
};

struct UserData
{
  struct UserSession *ptr;
};

inline bool check_progress_timeout(struct UserSession *session,
                                   system_clock::time_point now)
{
  if (session == NULL)
    return false;

  std::shared_lock<std::shared_mutex> lock(session->ts_mtx);

  duration<double, std::milli> elapsed = now - session->ts;

  if (elapsed >= duration_cast<system_clock::duration>(
                     duration<double>(uWS_PROGRESS_TIMEOUT)))
    return true;
  else
    return false;
}

inline void update_progress_timestamp(struct UserSession *session)
{
  if (session == NULL)
    return;

  std::lock_guard<std::shared_mutex> guard(session->ts_mtx);

  session->ts = system_clock::now();
}

// a global mutex used by real-time spectrum updates to prevent OpenMP pool thread contention
//inline std::mutex fits_mtx;

#ifdef CLUSTER
#include <czmq.h>

inline std::set<std::string> cluster;
inline std::shared_mutex cluster_mtx;

inline bool cluster_contains_node(std::string node)
{
  std::shared_lock<std::shared_mutex> lock(cluster_mtx);

  if (cluster.find(node) == cluster.end())
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
