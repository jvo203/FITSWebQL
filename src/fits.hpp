#pragma once

#include <string>
#include <ctime>
#include <mutex>
#include <string.h>
#include <optional>
#include <variant>
#include <boost/variant/variant.hpp>
#include <zlib.h>

#include <zfparray2.h>
#include <zfparray3.h>

#define JVO_FITS_SERVER "jvox.vo.nao.ac.jp"
#define JVO_FITS_DB "alma"

#define FITS_CHUNK_LENGTH 2880
#define FITS_LINE_LENGTH 80

class FITS
{
public:
  FITS();
  FITS(std::string id, std::string flux);
  ~FITS();

public:
  void update_timestamp();
  void from_url(std::string url, std::string flux, bool is_optical);
  void from_path(std::string path, bool is_compressed, std::string flux, bool is_optical);

private:
  void defaults();
  bool process_fits_header_unit(const char *buf);

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

  //extras
  bool has_header;
  bool has_data;
  bool has_frequency;
  bool has_velocity;
  bool is_optical;
  std::mutex fits_mutex;

private:
  //FITS header
  char *header;

  //ZFP compressed arrays
  std::optional<boost::variant<zfp::array2f, zfp::array3f>> data;

  //housekeeping
  std::time_t timestamp;
  int fits_file_desc;
  gzFile compressed_fits_stream;
  off_t fits_file_size;
  bool gz_compressed;
};