#pragma once

#include <cfloat>
#include <chrono>
#include <mutex>
#include <set>
#include <boost/thread/thread.hpp>
#include <thread>
#include <unordered_map>
#include <shared_mutex>

#include <curl/curl.h>

using namespace std::chrono;

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#if defined(__cplusplus)
extern "C"
{
#include "x265.h"
};
#else
#include "x265.h"
#endif

#include "App.h"

#include "fits.hpp"
#include "kalman.hpp"

#define FITS_CHUNK_LENGTH 2880
#define FITS_LINE_LENGTH 80

struct FITSDownloadStruct
{
  std::string datasetId;
  FILE *fp;
  size_t previous_size;
  size_t size;
  char buffer[CURL_MAX_WRITE_SIZE + FITS_CHUNK_LENGTH];
  size_t buffer_size;
  FITS *fits;
  size_t running_size;
  float dmin;
  float dmax;
  bool hdr_end;
  bool bSuccess;

  FITSDownloadStruct()
  {
    fp = NULL;
    previous_size = 0;
    size = 0;
    buffer_size = 0;
    fits = NULL;
    running_size = 0;
    dmin = FLT_MAX;
    dmax = -FLT_MAX;
    hdr_end = false;
    bSuccess = true;
  }
};

typedef uWS::WebSocket<false, true> TWebSocket;
typedef std::set<TWebSocket *> TWebSocketList;

typedef std::unordered_map<std::string, TWebSocketList> progress_list;
inline std::shared_mutex m_progress_mutex;
inline progress_list m_progress;

#define uWS_PROGRESS_TIMEOUT 0.25
#define CACHE_TIMEOUT 60 * 60
//# 60 is 1 minute
//# 60*60 is 60 minutes = 1 hour

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

  Ipp32u hist[NBINS];
  float min;
  float max;
  float median;
  float black;
  float white;
  float sensitivity;
  float ratio_sensitivity;

  // used by the pre-emptive cache
  std::shared_ptr<KalmanFilter> kal_x;
  std::shared_ptr<KalmanFilter> kal_y;

  // thread management
  std::atomic<int> last_seq;
  boost::thread_group active_threads;
  std::atomic<bool> active;

  // streaming video
  std::shared_ptr<KalmanFilter> kal_z;
  std::shared_ptr<x265_param> params;
  std::shared_ptr<x265_encoder> encoder;
  std::shared_ptr<x265_picture> picture;
  std::atomic<int> last_video_seq;
  std::atomic<int> last_frame_idx;
  std::atomic<bool> streaming;
  std::shared_mutex video_mtx;

  int width;
  int height;
  int fps;
  int bitrate;
  std::string flux;

  UserSession(boost::uuids::uuid _session_id, system_clock::time_point _ts, std::string _primary_id, std::vector<std::string> _ids)
  {
    session_id = _session_id;
    ts = _ts;
    primary_id = _primary_id;
    ids = _ids;
    last_seq = -1;
    last_video_seq = -1;
    last_frame_idx = -1;
    active = true;
    streaming = false;

    // fill the histogram with zeroes
    for (int i = 0; i < NBINS; i++)
      hist[i] = 0;

    width = 0;
    height = 0;
    fps = 10;
    bitrate = 1000;
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
