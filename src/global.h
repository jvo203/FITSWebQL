#pragma once

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <set>

using namespace std::chrono;

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#include "App.h"

typedef uWS::WebSocket<false, true> TWebSocket;                                                      
typedef std::set<TWebSocket*> TWebSocketList;

typedef std::unordered_map<std::string, TWebSocketList> progress_list ;
inline std::mutex m_progress_mutex;
inline progress_list m_progress;

struct UserSession {
  //session management
  boost::uuids::uuid session_id;
  system_clock::time_point ts;
  std::shared_mutex ts_mtx;

  //the main fields
  std::string primary_id;
  std::vector<std::string> ids;
};

struct UserData {
  struct UserSession* ptr;
};

inline bool check_progress_timeout(struct UserSession* session, system_clock::time_point now)
{
  if(session == NULL)
    return false;
  
  std::shared_lock<std::shared_mutex> lock(session->ts_mtx);

  duration<double, std::milli> elapsed = now - session->ts;

  if( elapsed >= duration_cast<system_clock::duration>(duration<double>(0.5)) )
    return true;
  else
    return false;
}

inline void update_progress_timestamp(struct UserSession* session, system_clock::time_point now)
{
  if(session == NULL)
    return ;
  
  std::lock_guard<std::shared_mutex> guard(session->ts_mtx);

  session->ts = now;
}

#ifdef CLUSTER
#include <czmq.h>

inline std::set<std::string> nodes;
inline std::shared_mutex nodes_mtx;
#endif
