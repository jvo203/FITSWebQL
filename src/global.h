#pragma once

#include <mutex>
#include <set>

#include "App.h"

typedef uWS::WebSocket<false, true> TWebSocket;                                                      
typedef std::set<TWebSocket*> TWebSocketList;

typedef std::unordered_map<std::string, TWebSocketList> progress_list ;
inline std::mutex m_progress_mutex;
inline progress_list m_progress;

#ifdef CLUSTER
#include <czmq.h>

inline std::set<std::string> nodes;
inline std::mutex nodes_mtx;
#endif