#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <math.h>
#include <mutex>
#include <optional>
#include <string.h>
#include <string>
#include <vector>
#include <zlib.h>

using namespace std::chrono;

#include "roaring.hh"

#include "shared_state.hpp"

#include <zfparray3.h>
//#include "array3fmmap.hpp"

#include <ipp.h>

#define JVO_FITS_SERVER "jvox.vo.nao.ac.jp"
#define JVO_FITS_DB "alma"
#define FITSCACHE "FITSCACHE"

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

class shared_state;

class FITS {
public:
  FITS();
  FITS(std::string id, std::string flux);
  ~FITS();

public:
  void update_timestamp();
  void from_url(std::string url, std::string flux, int va_count, boost::shared_ptr<shared_state> const& state);
  void from_path_zfp(std::string path, bool is_compressed, std::string flux,
                     int va_count, boost::shared_ptr<shared_state> const& state);
  void get_frequency_range(double &freq_start, double &freq_end);
  void to_json(std::ostringstream &json);

private:
  void defaults();
  const char *check_null(const char *str);
  void frame_reference_unit();
  void frame_reference_type();
  bool process_fits_header_unit(const char *buf);
  void image_statistics();
  void auto_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                       float &_ratio_sensitivity);
  float calculate_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                             float _sensitivity);
  void send_progress_notification(size_t running, size_t total);

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
  Ipp32u hist[NBINS];

  // extras
  std::atomic<bool> has_header;
  std::atomic<bool> has_data;
  std::atomic<bool> has_error;
  std::atomic<bool> processed_header;
  std::atomic<bool> processed_data;
  std::mutex header_mtx;
  std::mutex data_mtx;
  std::condition_variable header_cv;
  std::condition_variable data_cv;
  bool has_frequency;
  bool has_velocity;
  bool is_optical;
  bool is_xray;
  std::mutex fits_mutex;

private:
  // FITS header
  char *header;
  size_t hdr_len;
  Ipp32f *img_pixels;
  Ipp8u *img_mask;

  // ZFP compressed arrays + masks
  zfp::array3f *cube;
  std::vector<Roaring64Map> masks;

  // housekeeping
  struct timespec created;
  std::time_t timestamp;
  int fits_file_desc;
  gzFile compressed_fits_stream;
  off_t fits_file_size;
  bool gz_compressed;

  //Boost/Beast shared state
  boost::shared_ptr<shared_state> state_;
};
