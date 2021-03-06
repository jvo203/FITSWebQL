#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <map>
#include <math.h>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>

#include <ipp.h>

#include "lz4.h"
#include "lz4hc.h"

#include "App.h"
typedef uWS::WebSocket<false, true> TWebSocket2;

//#include "fifo.hpp"
#include <boost/lockfree/queue.hpp>
#include <boost/histogram.hpp>

using namespace boost::histogram;

using histogram_t = decltype(make_histogram(axis::regular<Ipp32f,
                                                          use_default,
                                                          use_default,
                                                          axis::option::growth_t>()));

using namespace std::chrono;

#define JVO_FITS_SERVER "jvox.vo.nao.ac.jp"
#define JVO_FITS_DB "alma"
#define FITSCACHE ".cache"

#define FITS_CHUNK_LENGTH 2880
#define FITS_LINE_LENGTH 80

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define MIN_HALF_FLOAT -65504
#define MAX_HALF_FLOAT 65504
#define EPSILON 1.0E-15
#define FPzero(A) (fabs(A) <= EPSILON)

#define NBINS 1024
#define NBINS2 16384
#define CELLSIZE 16

#define ZFP_CACHE_REGION 256
#define ZFPMAXPREC 14
#define ZFPACCURACY 1.0e-3
// ZFPMAXPREC 11; perhaps we should use 16 bits as the maximum precision instead of 11 or 8?

int histogram_classifier(float *Slot);
void make_histogram(const std::vector<Ipp32f> &v, Ipp32u *bins, int nbins,
                    float pmins, float pmax);

void tileMirror32f_C1R(Ipp32f *pSrc, Ipp32f *pDst, int width, int height);

IppStatus tileResize32f_C1R(Ipp32f *pSrc, IppiSize srcSize, Ipp32s srcStep,
                            Ipp32f *pDst, IppiSize dstSize, Ipp32s dstStep,
                            bool mirror = false);

IppStatus tileResize8u_C1R(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep,
                           Ipp8u *pDst, IppiSize dstSize, Ipp32s dstStep,
                           bool mirror = false);

size_t write_data(void *contents, size_t size, size_t nmemb, void *user);
bool scan_fits_header(struct FITSDownloadStruct *download, const char *contents, size_t size);
void scan_fits_data(struct FITSDownloadStruct *download, const char *contents, size_t size);

void hdr_set_long_value(char *hdr, long value);
void hdr_set_double_value(char *hdr, double value);
int hdr_get_int_value(char *hdr);
long hdr_get_long_value(char *hdr);
double hdr_get_double_value(char *hdr);
std::string hdr_get_string_value(char *hdr);
std::string hdr_get_string_value_with_spaces(char *hdr);

struct Progress
{
  size_t running;
  size_t total;
  double elapsed;

  Progress()
  {
    running = 0;
    total = 0;
    elapsed = 0.0;
  }
};

enum intensity_mode
{
  mean,
  integrated
};

enum beam_shape
{
  square,
  circle
};

typedef std::map<int, std::map<int, std::shared_ptr<Ipp8u>>> compressed_blocks;

// <unsigned short int> holds half-float pixels
struct CacheEntry
{
  std::atomic<std::time_t> timestamp;
  std::shared_ptr<unsigned short> data;

  CacheEntry()
  {
    timestamp = std::time(nullptr);

    size_t region_size = ZFP_CACHE_REGION * ZFP_CACHE_REGION;
    data = std::shared_ptr<unsigned short>(
        (unsigned short *)malloc(region_size * sizeof(unsigned short)),
        [](unsigned short *ptr) { if(ptr != NULL) free(ptr); });
  }
};

typedef std::map<int, std::map<int, std::shared_ptr<struct CacheEntry>>>
    decompressed_blocks;

class FITS
{
public:
  FITS();
  FITS(std::string id, std::string flux);
  ~FITS();

public:
  void update_timestamp();
  void
  from_url(std::string url, std::string flux, int va_count);
  void from_path(std::string path, bool is_compressed, std::string flux,
                 int va_count, bool use_mmap = true);
  void get_frequency_range(double &freq_start, double &freq_end);
  void get_spectrum_range(double frame_start, double frame_end, double ref_freq,
                          int &start, int &end);
  std::vector<float> get_spectrum(TWebSocket2 *ws, int start, int end, int x1, int y1, int x2,
                                  int y2, intensity_mode intensity,
                                  beam_shape beam, double &elapsed);
  std::tuple<std::shared_ptr<Ipp32f>, std::shared_ptr<Ipp8u>, std::vector<float>, std::vector<float>> get_cube(int start, int end);
  std::tuple<int, int, std::shared_ptr<Ipp8u>, std::shared_ptr<Ipp8u>, bool> get_video_frame(int frame, std::string flux);
  std::tuple<float, float, float, float, float, float, float> make_cube_statistics(std::shared_ptr<Ipp32f> pixels, std::shared_ptr<Ipp8u> mask, Ipp32u *bins);
  void preempt_cache(int start, int end, int x1, int y1, int x2, int y2);
  void to_json(std::ostringstream &json);
  void send_progress_notification(size_t running, size_t total);

public:
  void defaults();
  void serialise();
  void deserialise();
  const char *check_null(const char *str);
  void frame_reference_unit();
  void frame_reference_type();
  void get_freq2vel_bounds(double frame_start, double frame_end,
                           double ref_freq, int &start, int &end);
  void get_frequency_bounds(double freq_start, double freq_end, int &start,
                            int &end);
  void get_velocity_bounds(double vel_start, double vel_end, int &start,
                           int &end);
  bool process_fits_header_unit(const char *buf);
  void make_image_statistics();
  void make_data_statistics();
  void make_image_luma();
  void make_exr_image();
  void update_histogram(Ipp32f *_pixels, Ipp8u *_mask, Ipp32f _min, Ipp32f _max);
  void update_thread_histogram(Ipp32f *_pixels, Ipp8u *_mask, Ipp32f _min, Ipp32f _max, int tid);
  void update_thread_histogram(std::shared_ptr<void> pixels, Ipp32f _min, Ipp32f _max, int tid);
  void auto_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                       float &_ratio_sensitivity);
  float calculate_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                             float _sensitivity);
  void zfp_compress();
  void zfp_compression_thread(int tid);
  void zfp_compress_cube(size_t start_k);
  void zfp_decompress_cube(size_t start_k);
  bool zfp_load_cube(size_t start_k);
  bool zfp_mmap_cube(size_t start_k);
  std::shared_ptr<unsigned short> request_cached_region_ptr(int frame, int idy,
                                                            int idx, TWebSocket2 *ws = NULL);
  void send_cache_notification(TWebSocket2 *ws, int frame, int idy, int idx);
  void purge_cache();

public:
  std::string dataset_id;
  std::string data_id;
  std::string flux;
  long width;
  long height;
  long depth;
  long polarisation;
  int bitpix;
  int naxis;
  std::string btype;
  std::string bunit;
  float bscale;
  float bzero;
  float ignrval;
  double crval1;
  double cdelt1;
  double crpix1;
  std::string cunit1;
  std::string ctype1;
  double crval2;
  double cdelt2;
  double crpix2;
  std::string cunit2;
  std::string ctype2;
  double crval3;
  double cdelt3;
  double crpix3;
  std::string cunit3;
  std::string ctype3;
  double frame_multiplier;
  double cd1_1;
  double cd1_2;
  double cd2_1;
  double cd2_2;
  double bmaj;
  double bmin;
  double bpa;
  double restfrq;
  double obsra;
  double obsdec;
  float datamin;
  float datamax;
  std::string line;
  std::string filter;
  std::string specsys;
  std::string timesys;
  std::string object;
  std::string date_obs;
  std::string beam_unit;
  std::string beam_type;
  bool has_frequency;
  bool has_velocity;
  bool is_optical;
  bool is_xray;

  // values derived from the FITS data
  std::vector<float> frame_min, frame_max;
  std::vector<float> mean_spectrum, integrated_spectrum;

  // statistics
  float min, max, median, mad, madN, madP, black, white, sensitivity,
      ratio_sensitivity;
  float lmin, lmax;
  Ipp32u hist[NBINS];

  // approximate global statistics
  float dmin, dmax; // global data range
  float data_median, data_madP, data_madN;
  std::mutex hist_mtx;
  std::optional<histogram_t> data_hist;
  std::vector<std::optional<histogram_t>> hist_pool;

  // extras
  std::atomic<bool> has_header;
  std::atomic<bool> has_data;
  std::atomic<bool> has_error;
  std::atomic<bool> processed_header;
  std::atomic<bool> processed_data;

  // ZFP compression
  std::vector<std::thread> zfp_pool;

  // deprecated, unused anymore
  boost::lockfree::queue<size_t, boost::lockfree::capacity<1024>> zfp_queue;
  std::atomic<bool> terminate_compression;
  // end of deprecated

  std::mutex header_mtx;
  std::mutex data_mtx;
  std::condition_variable header_cv;
  std::condition_variable data_cv;
  std::mutex fits_mutex;
  std::mutex preempt_mutex;

  // progress
  struct Progress progress;
  std::shared_mutex progress_mtx;

  // float32 pixels and a mask
  std::shared_ptr<Ipp32f> img_pixels;
  std::shared_ptr<Ipp8u> img_mask;

public:
  // FITS header
  char *header;
  size_t hdr_len;

  // housekeeping
  struct timespec created;
  std::atomic<std::time_t> timestamp;
  system_clock::time_point cache_timestamp;

  int fits_file_desc;
  off_t fits_file_size;
  gzFile compressed_fits_stream;
  bool gz_compressed;
  std::mutex file_mtx;

  // mmap pointer to the underlying FITS file
  std::shared_ptr<void> fits_ptr;

  // a pointer array to 2D planes in a 3D cube
  std::vector<std::shared_ptr<void>> fits_cube;

  // compressed FITS cube planes / block regions
  std::vector<std::atomic<compressed_blocks *>> cube_pixels;
  std::vector<std::atomic<compressed_blocks *>> cube_mask;

  std::vector<std::shared_ptr<void>> cube_pixels_mmap;
  std::vector<std::shared_ptr<void>> cube_mask_mmap;

  // decompressed cache
  std::vector<decompressed_blocks> cache;
  std::vector<std::shared_mutex> cache_mtx;

  // cache purge thread
  std::atomic<bool> terminate = false;
  std::condition_variable purge_cv;
  std::thread purge_thread;
  std::mutex purge_mtx;

  // Boost/Beast shared state
  // boost::weak_ptr<shared_state> state_;
};
