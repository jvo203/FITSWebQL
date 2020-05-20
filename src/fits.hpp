#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <math.h>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>

#include "lz4.h"
#include "lz4hc.h"

//#include "fifo.hpp"
#include <boost/lockfree/queue.hpp>

using namespace std::chrono;

#include <ipp.h>

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

#define EPSILON 1.0E-15
#define FPzero(A) (fabs(A) <= EPSILON)

#define NBINS 1024

int histogram_classifier(float *Slot);
void make_histogram(const std::vector<Ipp32f> &v, Ipp32u *bins, int nbins,
                    float pmins, float pmax);

void tileMirror32f_C1R(Ipp32f *pSrc, Ipp32f *pDst, int width, int height);

IppStatus tileResize32f_C1R(Ipp32f *pSrc, IppiSize srcSize, Ipp32s srcStep,
                            Ipp32f *pDst, IppiSize dstSize, Ipp32s dstStep, bool mirror = false);

IppStatus tileResize8u_C1R(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep,
                           Ipp8u *pDst, IppiSize dstSize, Ipp32s dstStep, bool mirror = false);

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

class FITS
{
public:
  FITS();
  FITS(std::string id, std::string flux);
  ~FITS();

public:
  void update_timestamp();
  void
  from_url(std::string url, std::string flux,
           int va_count /*, boost::shared_ptr<shared_state> const& state*/);
  void from_path(std::string path, bool is_compressed, std::string flux,
                 int va_count);
  void from_path_mmap(std::string path, bool is_compressed, std::string flux,
                      int va_count);
  void get_frequency_range(double &freq_start, double &freq_end);
  void get_bounds(double frame_start, double frame_end, double ref_freq, int &start, int &end);
  void to_json(std::ostringstream &json);

private:
  void defaults();
  const char *check_null(const char *str);
  void frame_reference_unit();
  void frame_reference_type();
  void get_freq2vel_bounds(double frame_start, double frame_end, double ref_freq, int &start, int &end);
  void get_frequency_bounds(double freq_start, double freq_end, int &start, int &end);
  void get_velocity_bounds(double vel_start, double vel_end, int &start, int &end);
  bool process_fits_header_unit(const char *buf);
  void make_image_statistics();
  void make_image_luma();
  void make_exr_image();
  void auto_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                       float &_ratio_sensitivity);
  float calculate_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                             float _sensitivity);
  void send_progress_notification(size_t running, size_t total);
  void zfp_compress();
  void zfp_compression_thread(int tid);
  void zfp_compress_cube(size_t frame);

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
  int naxes[4];
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

  // values derived from the FITS data
  float dmin, dmax; // global data range
  std::vector<float> frame_min, frame_max;
  std::vector<float> mean_spectrum, integrated_spectrum;

  // statistics
  float min, max, median, mad, madN, madP, black, white, sensitivity,
      ratio_sensitivity;
  float lmin, lmax;
  Ipp32u hist[NBINS];

  // extras
  std::atomic<bool> has_header;
  std::atomic<bool> has_data;
  std::atomic<bool> has_error;
  std::atomic<bool> processed_header;
  std::atomic<bool> processed_data;

  // ZFP compression
  // std::thread compress_thread;
  std::vector<std::thread> zfp_pool;
  boost::lockfree::queue<size_t, boost::lockfree::capacity<1024>> zfp_queue;
  std::atomic<bool> terminate_compression;

  std::mutex header_mtx;
  std::mutex data_mtx;
  std::condition_variable header_cv;
  std::condition_variable data_cv;
  bool has_frequency;
  bool has_velocity;
  bool is_optical;
  bool is_xray;
  std::mutex fits_mutex;

  // progress
  struct Progress progress;
  std::shared_mutex progress_mtx;

  // float32 pixels and a mask
  Ipp32f *img_pixels;
  Ipp8u *img_mask;

private:
  // FITS header
  char *header;
  size_t hdr_len;

  // housekeeping
  struct timespec created;
  std::time_t timestamp;
  int fits_file_desc;
  gzFile compressed_fits_stream;
  off_t fits_file_size;
  bool gz_compressed;

  // mmap pointer to the underlying FITS file
  void *fits_ptr;
  size_t fits_ptr_size;

  // a pointer array to 2D planes in a 3D cube
  std::vector<void *> fits_cube;

  // Boost/Beast shared state
  // boost::weak_ptr<shared_state> state_;
};