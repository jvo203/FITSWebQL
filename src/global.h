#pragma once

#include <chrono>
#include <mutex>
#include <set>
#include <boost/thread/thread.hpp>
#include <thread>
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
  std::shared_mutex mtx;

  // the main fields
  std::string primary_id;
  std::vector<std::string> ids;

  std::shared_ptr<Ipp32f> img_pixels;
  std::shared_ptr<Ipp8u> img_mask;

  // thread management
  std::atomic<int> last_seq;
  boost::thread_group active_threads;
  std::atomic<bool> active;

  UserSession(boost::uuids::uuid _session_id, system_clock::time_point _ts, std::string _primary_id, std::vector<std::string> _ids)
  {
    session_id = _session_id;
    ts = _ts;
    primary_id = _primary_id;
    ids = _ids;
    last_seq = -1;
    active = true;
  }
};

inline std::unordered_map<std::string, std::shared_ptr<struct UserSession>> sessions;
inline std::shared_mutex sessions_mtx;

inline bool session_exists(std::string session_id)
{
  std::shared_lock<std::shared_mutex> lock(sessions_mtx);

  if (sessions.find(session_id) == sessions.end())
    return false;
  else
    return true;
}

inline std::shared_ptr<struct UserSession> get_session(std::string session_id)
{
  std::shared_lock<std::shared_mutex> lock(sessions_mtx);

  auto item = sessions.find(session_id);

  if (item == sessions.end())
    return nullptr;
  else
    return item->second;
}

inline void insert_session(std::string session_id, std::shared_ptr<struct UserSession> session)
{
  std::lock_guard<std::shared_mutex> guard(sessions_mtx);

  sessions.insert(std::pair(session_id, session));
}

inline void erase_session(std::string session_id)
{
  std::lock_guard<std::shared_mutex> guard(sessions_mtx);

  sessions.erase(session_id);
}

// the stuff  below is used by uWebSockets

struct UserData
{
  struct UserSession *ptr;
};

inline bool check_progress_timeout(struct UserSession *session,
                                   system_clock::time_point now)
{
  if (session == NULL)
    return false;

  std::shared_lock<std::shared_mutex> lock(session->mtx);

  duration<double, std::milli> elapsed = now - session->ts;

  if (elapsed >= duration_cast<system_clock::duration>(
                     duration<double>(uWS_PROGRESS_TIMEOUT)))
    return true;
  else
    return false;
}

inline void update_session_timestamp(struct UserSession *session)
{
  if (session == NULL)
    return;

  std::lock_guard<std::shared_mutex> guard(session->mtx);

  session->ts = system_clock::now();
}

// a global mutex used by real-time spectrum updates to prevent OpenMP pool thread contention
inline std::mutex fits_mtx;

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
