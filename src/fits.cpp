#include "../fits.h"
#include "fits.hpp"

//#include "global.h"

#include "json.h"
//#include "roaring.c"

#include "json.h"
#include "lz4.h"
#include "lz4hc.h"

// Intel IPP ZFP functions
#include <ippdc.h>

// base64 encoding with SSL
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <parallel/algorithm>

char *base64(const unsigned char *input, int length) {
  BIO *bmem, *b64;
  BUF_MEM *bptr;

  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_write(b64, input, length);
  BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bptr);

  char *buff = (char *)malloc(bptr->length);
  memcpy(buff, bptr->data, bptr->length - 1);
  buff[bptr->length - 1] = 0;

  BIO_free_all(b64);

  return buff;
};

int roundUp(int numToRound, int multiple) {
  if (multiple == 0)
    return numToRound;

  int remainder = numToRound % multiple;
  if (remainder == 0)
    return numToRound;

  return numToRound + multiple - remainder;
}

#include <cfloat>
#include <cmath>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <math.h>
#include <memory>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
using std::chrono::steady_clock;

#include <omp.h>

#include <boost/algorithm/string.hpp>

// OpenEXR
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfNamespace.h>
#include <OpenEXR/ImfOutputFile.h>

using namespace OPENEXR_IMF_NAMESPACE;

auto Ipp32fFree = [](Ipp32f *p) {
  static size_t counter = 0;
  if (p != NULL) {
    // printf("freeing <Ipp32f*>#%zu\t", counter++);
    ippsFree(p);
  }
};

auto Ipp8uFree = [](Ipp8u *p) {
  static size_t counter = 0;
  if (p != NULL) {
    // printf("freeing <Ipp8u*>#%zu\t", counter++);
    ippsFree(p);
  }
};

void hdr_set_long_value(char *hdr, long value) {
  unsigned int len = sprintf(hdr, "%ld", value);

  size_t num = FITS_LINE_LENGTH - 10 - len;

  if (num > 0)
    memset(hdr + len, ' ', num);
};

void hdr_set_double_value(char *hdr, double value) {
  unsigned int len = sprintf(hdr, "%E", value);

  size_t num = FITS_LINE_LENGTH - 10 - len;

  if (num > 0)
    memset(hdr + len, ' ', num);
};

int hdr_get_int_value(char *hdr) {
  printf("VALUE(%s)\n", hdr);

  return atoi(hdr);
};

long hdr_get_long_value(char *hdr) {
  printf("VALUE(%s)\n", hdr);

  return atol(hdr);
};

double hdr_get_double_value(char *hdr) {
  printf("VALUE(%s)\n", hdr);

  return atof(hdr);
};

std::string hdr_get_string_value(char *hdr) {
  char string[FITS_LINE_LENGTH] = "";

  printf("VALUE(%s)\n", hdr);

  sscanf(hdr, "'%s'", string);

  if (string[strlen(string) - 1] == '\'')
    string[strlen(string) - 1] = '\0';

  return std::string(string);
};

std::string hdr_get_string_value_with_spaces(char *hdr) {
  char string[FITS_LINE_LENGTH] = "";

  printf("VALUE(%s)\n", hdr);

  char *pos = strstr(hdr, "'");

  if (pos != NULL) {
    char *tmp = strstr(pos + 1, "'");

    if (tmp != NULL) {
      *tmp = '\0';
      strcpy(string, pos + 1);
    };
  };

  return std::string(string);
};

/*template <typename T = double, typename C>
  inline const T stl_median(const C &the_container)
  {
  std::vector<T> tmp_array(std::begin(the_container),
  std::end(the_container));
  size_t n = tmp_array.size() / 2;
  std::nth_element(tmp_array.begin(), tmp_array.begin() + n, tmp_array.end());

  if (tmp_array.size() % 2)
  {
  return tmp_array[n];
  }
  else
  {
  // even sized vector -> average the two middle values
  auto max_it = std::max_element(tmp_array.begin(), tmp_array.begin() +
  n); return (*max_it + tmp_array[n]) / 2.0;
  }
  }*/

void remove_nan(std::vector<Ipp32f> &v) {
  if (v.empty())
    return;

  size_t n = v.size();
  size_t v_end = n - 1;

  // this does not leave an empty vector when all elements are NAN
  // the first NAN remains...
  // it's OK, the median-finding functions will deal with this border-line case

  // iterate through the vector, replacing NAN/INFINITE with valid numbers from
  // the end
  for (size_t i = 0; i <= v_end; i++) {
    if (!std::isfinite(v[i])) {
      // replace it with a finite value from the end
      while (v_end > i && !std::isfinite(v[v_end]))
        v_end--;

      if (v_end <= i)
        break;
      else
        v[i] = v[v_end--];
    }
  }

  v.resize(v_end + 1);
  printf("v: original length: %zu, after NAN/INFINITE pruning: %zu\n", n,
         v.size());
}

Ipp32f stl_median(std::vector<Ipp32f> &v) {
  if (v.empty())
    return NAN;

  if (v.size() == 1)
    return v[0];

  auto start_t = steady_clock::now();

  Ipp32f medVal = NAN;

  size_t n = v.size() / 2;
  //#ifdef __INTEL_COMPILER
  //    std::nth_element(pstl::execution::par_unseq, v.begin(), v.begin() + n,
  //    v.end());
  //#else
  __gnu_parallel::nth_element(v.begin(), v.begin() + n, v.end());
  //#endif

  if (v.size() % 2) {
    medVal = v[n];
  } else {
    // even sized vector -> average the two middle values
    auto max_it = __gnu_parallel::max_element(v.begin(), v.begin() + n);
    medVal = (*max_it + v[n]) / 2.0f;
  }

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("stl_median::<value = %f, elapsed time: %5.2f [ms]>\n", v[n],
         elapsedMilliseconds);

  return medVal;
}

FITS::FITS() {
  std::cout << this->dataset_id << "::default constructor." << std::endl;

  this->timestamp = std::time(nullptr);
  clock_gettime(CLOCK_MONOTONIC, &(this->created));
  this->fits_file_desc = -1;
  this->compressed_fits_stream = NULL;
  this->fits_file_size = 0;
  this->gz_compressed = false;
  this->header = NULL;
  this->hdr_len = 0;
  this->img_pixels = NULL;
  this->img_mask = NULL;
  this->fits_ptr = nullptr;
  this->defaults();
}

FITS::FITS(std::string id, std::string flux) {
  std::cout << id << "::constructor." << std::endl;

  this->dataset_id = id;
  this->data_id = id + "_00_00_00";
  this->flux = flux;
  this->timestamp = std::time(nullptr);
  clock_gettime(CLOCK_MONOTONIC, &(this->created));
  this->fits_file_desc = -1;
  this->compressed_fits_stream = NULL;
  this->fits_file_size = 0;
  this->gz_compressed = false;
  this->header = NULL;
  this->hdr_len = 0;
  this->img_pixels = NULL;
  this->img_mask = NULL;
  this->fits_ptr = nullptr;
  this->defaults();
}

FITS::~FITS() {
  if (compress_thread.joinable())
    compress_thread.join();

  std::cout << this->dataset_id << "::destructor." << std::endl;

  // clear the cube containing pointers to mmaped regions
  fits_cube.clear();

  if (fits_ptr != NULL && fits_file_size > 0)
    munmap(fits_ptr, fits_file_size);

  if (fits_file_desc != -1)
    close(fits_file_desc);

  if (compressed_fits_stream != NULL)
    gzclose(compressed_fits_stream);

  if (header != NULL)
    free(header);

  if (img_pixels != NULL) {
    size_t plane_size = width * height;
    size_t frame_size = plane_size * abs(bitpix / 8);
    munmap(img_pixels, frame_size);
  }

  if (img_mask != NULL) {
    size_t plane_size = width * height;
    munmap(img_mask, plane_size);
  }
}

void FITS::defaults() {
  object = dataset_id;
  boost::replace_all(object, ".fits", "");
  boost::replace_all(object, ".FITS", "");
  bmaj = 0.0;
  bmin = 0.0;
  bpa = 0.0;
  restfrq = 0.0;
  obsra = 0.0;
  obsdec = 0.0;
  datamin = -FLT_MAX;
  datamax = FLT_MAX;
  bitpix = 0;
  naxis = 0;
  naxes[0] = 0;
  naxes[1] = 0;
  naxes[2] = 0;
  naxes[3] = 0;
  width = 0;
  height = 0;
  depth = 1;
  polarisation = 1;
  bscale = 1.0f;
  bzero = 0.0f, ignrval = -FLT_MAX;
  crval1 = 0.0;
  cdelt1 = NAN;
  crpix1 = 0.0;
  crval2 = 0.0;
  cdelt2 = NAN;
  crpix2 = 0.0;
  crval3 = 0.0;
  cdelt3 = 1.0;
  crpix3 = 0.0;
  cd1_1 = NAN;
  cd1_2 = NAN;
  cd2_1 = NAN;
  cd2_2 = NAN;
  frame_multiplier = 1.0;
  has_header = false;
  has_data = false;
  has_error = false;
  processed_header = false;
  processed_data = false;
  has_frequency = false;
  has_velocity = false;
  is_optical = true;
  is_xray = false;

  dmin = -FLT_MAX;
  dmax = FLT_MAX;

  median = NAN;
  min = NAN;
  max = NAN;
  mad = NAN;
  madN = NAN;
  madP = NAN;
  black = NAN;
  white = NAN;
  sensitivity = NAN;
  ratio_sensitivity = NAN;
  lmin = logf(0.5f);
  lmax = logf(1.5f);

  for (int i = 0; i < NBINS; i++)
    hist[i] = 0;
}

void FITS::update_timestamp() {
  std::lock_guard<std::mutex> lock(fits_mutex);
  timestamp = std::time(nullptr);
}

void FITS::frame_reference_type() {
  char *pos = NULL;
  const char *_ctype3 = ctype3.c_str();

  {
    pos = (char *)strstr(_ctype3, "F");

    if (pos != NULL)
      has_frequency = true;
  }

  {
    pos = (char *)strstr(_ctype3, "f");

    if (pos != NULL)
      has_frequency = true;
  }

  {
    pos = (char *)strstr(_ctype3, "V");

    if (pos != NULL)
      has_velocity = true;
  }

  {
    pos = (char *)strstr(_ctype3, "v");

    if (pos != NULL)
      has_velocity = true;
  }
};

void FITS::frame_reference_unit() {
  const char *_cunit3 = cunit3.c_str();

  if (!strcasecmp(_cunit3, "Hz")) {
    has_frequency = true;
    frame_multiplier = 1.0f;
    return;
  };

  if (!strcasecmp(_cunit3, "kHz")) {
    has_frequency = true;
    frame_multiplier = 1e3f;
    return;
  };

  if (!strcasecmp(_cunit3, "MHz")) {
    has_frequency = true;
    frame_multiplier = 1e6f;
    return;
  };

  if (!strcasecmp(_cunit3, "GHz")) {
    has_frequency = true;
    frame_multiplier = 1e9f;
    return;
  };

  if (!strcasecmp(_cunit3, "THz")) {
    has_frequency = true;
    frame_multiplier = 1e12f;
    return;
  };

  if (!strcasecmp(_cunit3, "m/s")) {
    has_velocity = true;
    frame_multiplier = 1.0f;
    return;
  };

  if (!strcasecmp(_cunit3, "km/s")) {
    has_velocity = true;
    frame_multiplier = 1e3f;
    return;
  };
}

void FITS::get_frequency_range(double &freq_start, double &freq_end) {
  if (has_velocity) {
    double c = 299792458.0; // speed of light [m/s]

    double v1 =
        crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
    double v2 = crval3 * frame_multiplier +
                cdelt3 * frame_multiplier * (double(depth) - crpix3);

    double f1 = restfrq * sqrt((1.0 - v1 / c) / (1.0 + v1 / c));
    double f2 = restfrq * sqrt((1.0 - v2 / c) / (1.0 + v2 / c));

    freq_start = MIN(f1, f2) / 1.0E9; //[Hz -> GHz]
    freq_end = MAX(f1, f2) / 1.0E9;   //[Hz -> GHz]
  } else if (has_frequency) {
    double f1 =
        crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
    double f2 = crval3 * frame_multiplier +
                cdelt3 * frame_multiplier * (double(depth) - crpix3);

    freq_start = MIN(f1, f2) / 1.0E9; //[Hz -> GHz]
    freq_end = MAX(f1, f2) / 1.0E9;   //[Hz -> GHz]
  }
}

bool FITS::process_fits_header_unit(const char *buf) {
  char hdrLine[FITS_LINE_LENGTH + 1];
  bool end = false;

  hdrLine[sizeof(hdrLine) - 1] = '\0';

  for (size_t offset = 0; offset < FITS_CHUNK_LENGTH;
       offset += FITS_LINE_LENGTH) {
    strncpy(hdrLine, buf + offset, FITS_LINE_LENGTH);
    // printf("%s\n", hdrLine) ;

    if (strncmp(buf + offset, "END       ", 10) == 0)
      end = true;

    if (strncmp(hdrLine, "BITPIX  = ", 10) == 0)
      bitpix = hdr_get_int_value(hdrLine + 10);

    if (strncmp(hdrLine, "NAXIS   = ", 10) == 0)
      naxis = hdr_get_int_value(hdrLine + 10);

    if (strncmp(hdrLine, "NAXIS1  = ", 10) == 0)
      width = hdr_get_long_value(hdrLine + 10);

    if (strncmp(hdrLine, "NAXIS2  = ", 10) == 0)
      height = hdr_get_long_value(hdrLine + 10);

    if (strncmp(hdrLine, "NAXIS3  = ", 10) == 0)
      depth = hdr_get_long_value(hdrLine + 10);

    if (strncmp(hdrLine, "NAXIS4  = ", 10) == 0)
      polarisation = hdr_get_long_value(hdrLine + 10);

    if (strncmp(hdrLine, "BTYPE   = ", 10) == 0)
      btype = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "BUNIT   = ", 10) == 0)
      bunit = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "BSCALE  = ", 10) == 0)
      bscale = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "BZERO   = ", 10) == 0)
      bzero = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "IGNRVAL = ", 10) == 0)
      ignrval = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CRVAL1  = ", 10) == 0)
      crval1 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CDELT1  = ", 10) == 0)
      cdelt1 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CRPIX1  = ", 10) == 0)
      crpix1 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CRVAL2  = ", 10) == 0)
      crval2 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CDELT2  = ", 10) == 0)
      cdelt2 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CRPIX2  = ", 10) == 0)
      crpix2 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CRVAL3  = ", 10) == 0)
      crval3 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CDELT3  = ", 10) == 0)
      cdelt3 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CRPIX3  = ", 10) == 0)
      crpix3 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "BMAJ    = ", 10) == 0)
      bmaj = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "BMIN    = ", 10) == 0)
      bmin = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "BPA     = ", 10) == 0)
      bpa = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "RESTFRQ = ", 10) == 0)
      restfrq = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "RESTFREQ= ", 10) == 0)
      restfrq = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "OBSRA   = ", 10) == 0)
      obsra = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "OBSDEC  = ", 10) == 0)
      obsdec = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "DATAMIN = ", 10) == 0)
      datamin = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "DATAMAX = ", 10) == 0)
      datamax = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "LINE    = ", 10) == 0)
      line = hdr_get_string_value_with_spaces(hdrLine + 10);

    if (strncmp(hdrLine, "J_LINE  = ", 10) == 0)
      line = hdr_get_string_value_with_spaces(hdrLine + 10);

    if (strncmp(hdrLine, "FILTER  = ", 10) == 0)
      filter = hdr_get_string_value_with_spaces(hdrLine + 10);

    if (strncmp(hdrLine, "SPECSYS = ", 10) == 0)
      specsys = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "TIMESYS = ", 10) == 0)
      timesys = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "BUNIT   = ", 10) == 0)
      beam_unit = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "BTYPE   = ", 10) == 0)
      beam_type = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "OBJECT  = ", 10) == 0)
      object = hdr_get_string_value_with_spaces(hdrLine + 10);

    if (strncmp(hdrLine, "DATE-OBS= ", 10) == 0)
      date_obs = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CUNIT1  = ", 10) == 0)
      cunit1 = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CUNIT2  = ", 10) == 0)
      cunit2 = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CUNIT3  = ", 10) == 0)
      cunit3 = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CTYPE1  = ", 10) == 0)
      ctype1 = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CTYPE2  = ", 10) == 0)
      ctype2 = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CTYPE3  = ", 10) == 0)
      ctype3 = hdr_get_string_value(hdrLine + 10);

    if (strncmp(hdrLine, "CD1_1   = ", 10) == 0)
      cd1_1 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CD1_2   = ", 10) == 0)
      cd1_2 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CD2_1   = ", 10) == 0)
      cd2_1 = hdr_get_double_value(hdrLine + 10);

    if (strncmp(hdrLine, "CD2_2   = ", 10) == 0)
      cd2_2 = hdr_get_double_value(hdrLine + 10);

    if (datamin == datamax) {
      datamin = -FLT_MAX;
      datamax = FLT_MAX;
    }

    // decide on a FITS type (optical? radio? X-ray?)
    if (strncmp(hdrLine, "TELESCOP= ", 10) == 0) {
      std::string telescope =
          boost::algorithm::to_lower_copy(hdr_get_string_value(hdrLine + 10));

      if (telescope.find("alma") != std::string::npos)
        is_optical = false;

      if (telescope.find("vla") != std::string::npos ||
          telescope.find("ska") != std::string::npos)
        is_optical = false;

      if (telescope.find("nro45") != std::string::npos) {
        is_optical = false;
        flux = "logistic";
      }

      if (telescope.find("chandra") != std::string::npos) {
        is_optical = false;
        is_xray = true;
      }
    }

    std::string line(hdrLine);

    if (line.find("ASTRO-F") != std::string::npos) {
      is_optical = true;
      flux = "logistic";
    }

    if (line.find("HSCPIPE") != std::string::npos) {
      is_optical = true;
      flux = "ratio";
    }

    if (strncmp(hdrLine, "FRAMEID = ", 10) == 0) {
      std::string frameid = hdr_get_string_value(hdrLine + 10);

      if (frameid.find("SUPM") != std::string::npos ||
          frameid.find("MCSM") != std::string::npos) {
        is_optical = true;
        flux = "ratio";
      }
    }

    // JAXA X-Ray settings
    {
      // in-place to_lower
      boost::algorithm::to_lower(line);

      if (line.find("suzaku") != std::string::npos ||
          line.find("hitomi") != std::string::npos ||
          line.find("x-ray") != std::string::npos) {
        is_optical = false;
        is_xray = true;
        flux = "legacy";
        if (ignrval == -FLT_MAX)
          ignrval = -1.0f;
      }
    }
  }

  return end;
}

void FITS::from_url(
    std::string url, std::string flux,
    int va_count /*, boost::shared_ptr<shared_state> const& state*/) {
  // state_ = state;

  int no_omp_threads = MAX(omp_get_max_threads() / va_count, 1);
  printf("downloading %s from %s, va_count = %d, no_omp_threads = %d\n",
         this->dataset_id.c_str(), url.c_str(), va_count, no_omp_threads);
}

void FITS::from_path(std::string path, bool is_compressed, std::string flux,
                     int va_count) {
  std::unique_lock<std::mutex> header_lck(header_mtx);
  std::unique_lock<std::mutex> data_lck(data_mtx);

  auto start_t = steady_clock::now();

  int no_omp_threads = MAX(omp_get_max_threads() / va_count, 1);
  printf("loading %s from %s %s gzip compression, va_count = %d, "
         "no_omp_threads = %d\n",
         this->dataset_id.c_str(), path.c_str(),
         (is_compressed ? "with" : "without"), va_count, no_omp_threads);

  this->gz_compressed = is_compressed;

  // try to open the FITS file
  int fd = -1;
  gzFile file = NULL;

  if (is_compressed) {
    file = gzopen(path.c_str(), "r");

    if (!file) {
      printf("gzopen of '%s' failed: %s.\n", path.c_str(), strerror(errno));
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  } else {
    fd = open(path.c_str(), O_RDONLY);

    if (fd == -1) {
      printf("error opening %s .", path.c_str());
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  }

  struct stat64 st;
  stat64(path.c_str(), &st);

  this->fits_file_desc = fd;
  this->compressed_fits_stream = file;
  this->fits_file_size = st.st_size;

  if (this->fits_file_size < FITS_CHUNK_LENGTH) {
    printf("error: FITS file size smaller than %d bytes.", FITS_CHUNK_LENGTH);
    processed_header = true;
    header_cv.notify_all();
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  printf("%s::reading FITS header...\n", dataset_id.c_str());

  int no_hu = 0;
  size_t offset = 0;

  while (naxis == 0) {
    bool end = false;

    while (!end) {
      // fread FITS_CHUNK_LENGTH from fd into header+offset
      header =
          (char *)realloc(header, offset + FITS_CHUNK_LENGTH +
                                      1); // an extra space for the ending NULL

      if (header == NULL)
        fprintf(stderr, "CRITICAL: could not (re)allocate FITS header\n");

      ssize_t bytes_read = 0;

      if (is_compressed)
        bytes_read = gzread(this->compressed_fits_stream, header + offset,
                            FITS_CHUNK_LENGTH);
      else
        bytes_read =
            read(this->fits_file_desc, header + offset, FITS_CHUNK_LENGTH);

      if (bytes_read != FITS_CHUNK_LENGTH) {
        fprintf(stderr,
                "CRITICAL: read less than %zd bytes from the FITS header\n",
                bytes_read);
        processed_header = true;
        header_cv.notify_all();
        processed_data = true;
        data_cv.notify_all();
        return;
      }

      end = this->process_fits_header_unit(header + offset);

      offset += FITS_CHUNK_LENGTH;
      no_hu++;
    }

    printf("%s::FITS HEADER END.\n", dataset_id.c_str());
  }

  header[offset] = '\0';
  this->hdr_len = offset;

  // test for frequency/velocity
  frame_reference_unit();
  frame_reference_type();

  if (has_frequency || has_velocity)
    is_optical = false;

  if (restfrq > 0.0)
    has_frequency = true;

  this->has_header = true;
  this->processed_header = true;
  this->header_cv.notify_all();
  header_lck.unlock();
  header_lck.release();

  // printf("%s\n", header);

  if (bitpix != -32) {
    printf("%s::unsupported bitpix(%d), FITS data will not be read.\n",
           dataset_id.c_str(), bitpix);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  if (width <= 0 || height <= 0 || depth <= 0) {
    printf("%s::incorrect dimensions (width:%ld, height:%ld, depth:%ld)\n",
           dataset_id.c_str(), width, height, depth);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  const size_t plane_size = width * height;
  const size_t frame_size = plane_size * abs(bitpix / 8);

  if (frame_size != plane_size * sizeof(float)) {
    printf("%s::plane_size != frame_size, is the bitpix correct?\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  // use mmap
  if (img_pixels == NULL && img_mask == NULL) {
    int fd, stat;
    std::string filename;

    filename = FITSCACHE + std::string("/") +
               boost::replace_all_copy(dataset_id, "/", "_") +
               std::string(".pixels");

    fd = open(filename.c_str(), O_RDWR | O_CREAT, (mode_t)0644);

    if (fd != -1) {
#if defined(__APPLE__) && defined(__MACH__)
      stat = ftruncate(fd, frame_size);
#else
      stat = ftruncate64(fd, frame_size);
#endif

      if (!stat)
        img_pixels = (Ipp32f *)mmap(NULL, frame_size, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, 0);

      close(fd);
    }

    filename = FITSCACHE + std::string("/") +
               boost::replace_all_copy(dataset_id, "/", "_") +
               std::string(".mask");

    fd = open(filename.c_str(), O_RDWR | O_CREAT, (mode_t)0644);

    if (fd != -1) {
#if defined(__APPLE__) && defined(__MACH__)
      stat = ftruncate(fd, plane_size);
#else
      stat = ftruncate64(fd, plane_size);
#endif

      if (!stat)
        img_mask = (Ipp8u *)mmap(NULL, plane_size, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, 0);

      close(fd);
    }
  }

  if (img_pixels == NULL || img_mask == NULL) {
    printf("%s::cannot mmap memory for a 2D image buffer (pixels+mask).\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  std::atomic<bool> bSuccess = true;

  float _pmin = FLT_MAX;
  float _pmax = -FLT_MAX;

  if (depth == 1) {
    // read/process the FITS plane (image) in parallel
    // unless this is a compressed file, in which case
    // the data can only be read sequentially

    // use ispc to process the plane
    // 1. endianness
    // 2. fill-in {pixels,mask}

    // get pmin, pmax
    int max_threads = omp_get_max_threads();

    // keep the worksize within int32 limits
    size_t max_work_size = 1024 * 1024 * 1024;
    size_t work_size = MIN(plane_size / max_threads, max_work_size);
    int num_threads = plane_size / work_size;

    printf("%s::fits2float32:\tsize = %zu, work_size = %zu, num_threads = %d\n",
           dataset_id.c_str(), plane_size, work_size, num_threads);

    if (is_compressed) {
      // load data into the buffer sequentially
      ssize_t bytes_read =
          gzread(this->compressed_fits_stream, img_pixels, frame_size);

      if (bytes_read != frame_size) {
        fprintf(
            stderr,
            "%s::CRITICAL: read less than %zd bytes from the FITS data unit\n",
            dataset_id.c_str(), bytes_read);
        processed_data = true;
        data_cv.notify_all();
        return;
      } else
        printf("%s::FITS data read OK.\n", dataset_id.c_str());

#pragma omp parallel for schedule(static) num_threads(no_omp_threads)          \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++) {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        ispc::fits2float32((int32_t *)&(img_pixels[start]),
                           (uint8_t *)&(img_mask[start]), bzero, bscale,
                           ignrval, datamin, datamax, _pmin, _pmax, work_size);
      };
    } else {
      // load data into the buffer in parallel chunks
      // the data part starts at <offset>

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads)         \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++) {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        // parallel read (pread) at a specified offset
        ssize_t bytes_read =
            pread(this->fits_file_desc, &(img_pixels[start]),
                  work_size * sizeof(float), offset + start * sizeof(float));

        if (bytes_read != work_size * sizeof(float)) {
          fprintf(stderr,
                  "%s::CRITICAL: only read %zd out of requested %zd bytes.\n",
                  dataset_id.c_str(), bytes_read, (work_size * sizeof(float)));
          bSuccess = false;
        } else
          ispc::fits2float32((int32_t *)&(img_pixels[start]),
                             (uint8_t *)&(img_mask[start]), bzero, bscale,
                             ignrval, datamin, datamax, _pmin, _pmax,
                             work_size);
      };
    }

    dmin = _pmin;
    dmax = _pmax;
  } else {
    printf("%s::depth > 1: work-in-progress.\n", dataset_id.c_str());
    // init the variables
    frame_min.resize(depth, FLT_MAX);
    frame_max.resize(depth, -FLT_MAX);
    mean_spectrum.resize(depth, 0.0f);
    integrated_spectrum.resize(depth, 0.0f);

    // prepare the main image/mask
    memset(img_mask, 0, plane_size);
    for (size_t i = 0; i < plane_size; i++)
      img_pixels[i] = 0.0f;

    int max_threads = omp_get_max_threads();

    if (!is_compressed) {
      // pre-allocated floating-point read buffers
      // to reduce RAM thrashing
      std::vector<Ipp32f *> pixels_buf(max_threads);
      std::vector<Ipp8u *> mask_buf(max_threads);

      // OpenMP per-thread {pixels,mask}
      std::vector<Ipp32f *> omp_pixels(max_threads);
      std::vector<Ipp8u *> omp_mask(max_threads);

      for (int i = 0; i < max_threads; i++) {
        pixels_buf[i] = ippsMalloc_32f_L(plane_size);
        mask_buf[i] = ippsMalloc_8u_L(plane_size);

        omp_pixels[i] = ippsMalloc_32f_L(plane_size);
        if (omp_pixels[i] != NULL)
          for (size_t j = 0; j < plane_size; j++)
            omp_pixels[i][j] = 0.0f;

        omp_mask[i] = ippsMalloc_8u_L(plane_size);
        if (omp_mask[i] != NULL)
          memset(omp_mask[i], 0, plane_size);
      }

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads)         \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)

      for (size_t frame = 0; frame < depth; frame++) {
        int tid = omp_get_thread_num();
        // printf("tid: %d, k: %zu\n", tid, k);
        if (pixels_buf[tid] == NULL || mask_buf[tid] == NULL ||
            omp_pixels[tid] == NULL || omp_mask[tid] == NULL) {
          fprintf(stderr,
                  "%s::<tid::%d>::problem allocating thread-local {pixels,buf} "
                  "arrays.\n",
                  dataset_id.c_str(), tid);
          bSuccess = false;
          continue;
        }

        // parallel read (pread) at a specified offset
        ssize_t bytes_read = pread(this->fits_file_desc, pixels_buf[tid],
                                   frame_size, offset + frame_size * frame);

        if (bytes_read != frame_size) {
          fprintf(stderr,
                  "%s::<tid::%d>::CRITICAL: only read %zd out of requested "
                  "%zd bytes.\n",
                  dataset_id.c_str(), tid, bytes_read, frame_size);
          bSuccess = false;
        } else {
          float fmin = FLT_MAX;
          float fmax = -FLT_MAX;
          float mean = 0.0f;
          float integrated = 0.0f;

          float _cdelt3 = this->has_velocity
                              ? this->cdelt3 * this->frame_multiplier / 1000.0f
                              : 1.0f;

          ispc::make_image_spectrumF32(
              (int32_t *)pixels_buf[tid], mask_buf[tid], bzero, bscale, ignrval,
              datamin, datamax, _cdelt3, omp_pixels[tid], omp_mask[tid], fmin,
              fmax, mean, integrated, plane_size);

          _pmin = MIN(_pmin, fmin);
          _pmax = MAX(_pmax, fmax);
          frame_min[frame] = fmin;
          frame_max[frame] = fmax;
          mean_spectrum[frame] = mean;
          integrated_spectrum[frame] = integrated;
        }

        send_progress_notification(frame, depth);
      }

      // join omp_{pixel,mask}

      // keep the worksize within int32 limits
      size_t max_work_size = 1024 * 1024 * 1024;
      size_t work_size = MIN(plane_size / max_threads, max_work_size);
      int num_threads = plane_size / work_size;

      for (int i = 0; i < max_threads; i++) {
        float *pixels_tid = omp_pixels[i];
        unsigned char *mask_tid = omp_mask[i];

#pragma omp parallel for num_threads(no_omp_threads)
        for (int tid = 0; tid < num_threads; tid++) {
          size_t work_size = plane_size / num_threads;
          size_t start = tid * work_size;

          if (tid == num_threads - 1)
            work_size = plane_size - start;

          ispc::join_pixels_masks(&(img_pixels[start]), &(pixels_tid[start]),
                                  &(img_mask[start]), &(mask_tid[start]),
                                  work_size);
        }
      }

      // release memory
      for (int i = 0; i < max_threads; i++) {
        if (pixels_buf[i] != NULL)
          ippsFree(pixels_buf[i]);

        if (mask_buf[i] != NULL)
          ippsFree(mask_buf[i]);

        if (omp_pixels[i] != NULL)
          ippsFree(omp_pixels[i]);

        if (omp_mask[i] != NULL)
          ippsFree(omp_mask[i]);
      }
    } else {
      printf("%s::gz-compressed depth > 1: work-in-progress.\n",
             dataset_id.c_str());

#pragma omp parallel num_threads(no_omp_threads)
      {
#pragma omp single
        {
          for (size_t frame = 0; frame < depth; frame++) {
            // allocate {pixel_buf, mask_buf}
            std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
                                               Ipp32fFree);
            std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size),
                                            Ipp8uFree);

            if (pixels_buf.get() == NULL || mask_buf.get() == NULL) {
              printf("%s::CRITICAL::cannot malloc memory for {pixels,mask} "
                     "buffers.\n",
                     dataset_id.c_str());
              bSuccess = false;
              break;
            }

            // load data into the buffer sequentially
            ssize_t bytes_read = gzread(this->compressed_fits_stream,
                                        pixels_buf.get(), frame_size);

            if (bytes_read != frame_size) {
              fprintf(stderr,
                      "%s::CRITICAL: read less than %zd bytes from the FITS "
                      "data unit\n",
                      dataset_id.c_str(), bytes_read);
              bSuccess = false;
              break;
            }

            // process the buffer
            float fmin = FLT_MAX;
            float fmax = -FLT_MAX;
            float mean = 0.0f;
            float integrated = 0.0f;

            float _cdelt3 =
                this->has_velocity
                    ? this->cdelt3 * this->frame_multiplier / 1000.0f
                    : 1.0f;

            ispc::make_image_spectrumF32(
                (int32_t *)pixels_buf.get(), mask_buf.get(), bzero, bscale,
                ignrval, datamin, datamax, _cdelt3, img_pixels, img_mask, fmin,
                fmax, mean, integrated, plane_size);

            _pmin = MIN(_pmin, fmin);
            _pmax = MAX(_pmax, fmax);
            frame_min[frame] = fmin;
            frame_max[frame] = fmax;
            mean_spectrum[frame] = mean;
            integrated_spectrum[frame] = integrated;

            send_progress_notification(frame, depth);
          }
        }
      }
    }

    dmin = _pmin;
    dmax = _pmax;

    /*printf("FMIN/FMAX\tSPECTRUM\n");
      for (int i = 0; i < depth; i++)
      printf("%d (%f):(%f)\t\t(%f):(%f)\n", i, frame_min[i], frame_max[i],
      mean_spectrum[i], integrated_spectrum[i]); printf("\n");*/
  }

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("%s::<data:%s>\tdmin = %f\tdmax = %f\telapsed time: %5.2f [ms]\n",
         dataset_id.c_str(), (bSuccess ? "true" : "false"), dmin, dmax,
         elapsedMilliseconds);

  if (bSuccess) {
    send_progress_notification(depth, depth);
    /*for (int i = 0; i < depth; i++)
      std::cout << "mask[" << i << "]::cardinality: " << masks[i].cardinality()
      << ", size: " << masks[i].getSizeInBytes() << " bytes"
      << std::endl;*/

    make_image_statistics();

    printf("%s::statistics\npmin: %f pmax: %f median: %f mad: %f madP: %f "
           "madN: %f black: %f white: %f sensitivity: %f flux: %s\n",
           dataset_id.c_str(), this->min, this->max, this->median, this->mad,
           this->madP, this->madN, this->black, this->white, this->sensitivity,
           this->flux.c_str());

    make_image_luma();

    make_exr_image();
  } else {
    this->has_error = true;
  }

  this->has_data = bSuccess ? true : false;
  this->processed_data = true;
  this->data_cv.notify_all();
  this->timestamp = std::time(nullptr);
}

void FITS::from_path_mmap(std::string path, bool is_compressed,
                          std::string flux, int va_count) {
  std::unique_lock<std::mutex> header_lck(header_mtx);
  std::unique_lock<std::mutex> data_lck(data_mtx);

  auto start_t = steady_clock::now();

  int no_omp_threads = MAX(omp_get_max_threads() / va_count, 1);
  printf("loading %s from %s %s gzip compression, va_count = %d, "
         "no_omp_threads = %d\n",
         this->dataset_id.c_str(), path.c_str(),
         (is_compressed ? "with" : "without"), va_count, no_omp_threads);

  this->gz_compressed = is_compressed;

  // try to open the FITS file
  int fd = -1;
  gzFile file = NULL;

  if (is_compressed) {
    file = gzopen(path.c_str(), "r");

    if (!file) {
      printf("gzopen of '%s' failed: %s.\n", path.c_str(), strerror(errno));
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  } else {
    fd = open(path.c_str(), O_RDONLY);

    if (fd == -1) {
      printf("error opening %s .", path.c_str());
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  }

  struct stat64 st;
  stat64(path.c_str(), &st);

  this->fits_file_desc = fd;
  this->compressed_fits_stream = file;
  this->fits_file_size = st.st_size;

  if (this->fits_file_size < FITS_CHUNK_LENGTH) {
    printf("error: FITS file size smaller than %d bytes.", FITS_CHUNK_LENGTH);
    processed_header = true;
    header_cv.notify_all();
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  // mmap the FITS file
  if (this->fits_file_desc != -1) {
    this->fits_ptr =
        mmap(nullptr, this->fits_file_size, PROT_READ,
             MAP_PRIVATE /*| MAP_HUGETLB*/, this->fits_file_desc, 0);

    if (this->fits_ptr == NULL) {
      printf("%s::error mmaping the FITS file...\n", dataset_id.c_str());
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  }

  printf("%s::reading FITS header...\n", dataset_id.c_str());

  int no_hu = 0;
  size_t offset = 0;

  while (naxis == 0) {
    bool end = false;

    while (!end) {
      // fread FITS_CHUNK_LENGTH from fd into header+offset
      header =
          (char *)realloc(header, offset + FITS_CHUNK_LENGTH +
                                      1); // an extra space for the ending NULL

      if (header == NULL)
        fprintf(stderr, "CRITICAL: could not (re)allocate FITS header\n");

      ssize_t bytes_read = 0;

      if (is_compressed)
        bytes_read = gzread(this->compressed_fits_stream, header + offset,
                            FITS_CHUNK_LENGTH);
      else
        bytes_read =
            read(this->fits_file_desc, header + offset, FITS_CHUNK_LENGTH);

      if (bytes_read != FITS_CHUNK_LENGTH) {
        fprintf(stderr,
                "CRITICAL: read less than %zd bytes from the FITS header\n",
                bytes_read);
        processed_header = true;
        header_cv.notify_all();
        processed_data = true;
        data_cv.notify_all();
        return;
      }

      end = this->process_fits_header_unit(header + offset);

      offset += FITS_CHUNK_LENGTH;
      no_hu++;
    }

    printf("%s::FITS HEADER END.\n", dataset_id.c_str());
  }

  header[offset] = '\0';
  this->hdr_len = offset;

  // test for frequency/velocity
  frame_reference_unit();
  frame_reference_type();

  if (has_frequency || has_velocity)
    is_optical = false;

  if (restfrq > 0.0)
    has_frequency = true;

  this->has_header = true;
  this->processed_header = true;
  this->header_cv.notify_all();
  header_lck.unlock();
  header_lck.release();

  // printf("%s\n", header);

  if (bitpix != -32) {
    printf("%s::unsupported bitpix(%d), FITS data will not be read.\n",
           dataset_id.c_str(), bitpix);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  if (width <= 0 || height <= 0 || depth <= 0) {
    printf("%s::incorrect dimensions (width:%ld, height:%ld, depth:%ld)\n",
           dataset_id.c_str(), width, height, depth);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  const size_t plane_size = width * height;
  const size_t frame_size = plane_size * abs(bitpix / 8);

  if (frame_size != plane_size * sizeof(float)) {
    printf("%s::plane_size != frame_size, is the bitpix correct?\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  // use mmap
  if (img_pixels == NULL && img_mask == NULL) {
    int fd, stat;
    std::string filename;

    filename = FITSCACHE + std::string("/") +
               boost::replace_all_copy(dataset_id, "/", "_") +
               std::string(".pixels");

    fd = open(filename.c_str(), O_RDWR | O_CREAT, (mode_t)0644);

    if (fd != -1) {
#if defined(__APPLE__) && defined(__MACH__)
      stat = ftruncate(fd, frame_size);
#else
      stat = ftruncate64(fd, frame_size);
#endif

      if (!stat)
        img_pixels = (Ipp32f *)mmap(NULL, frame_size, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, 0);

      close(fd);
    }

    filename = FITSCACHE + std::string("/") +
               boost::replace_all_copy(dataset_id, "/", "_") +
               std::string(".mask");

    fd = open(filename.c_str(), O_RDWR | O_CREAT, (mode_t)0644);

    if (fd != -1) {
#if defined(__APPLE__) && defined(__MACH__)
      stat = ftruncate(fd, plane_size);
#else
      stat = ftruncate64(fd, plane_size);
#endif

      if (!stat)
        img_mask = (Ipp8u *)mmap(NULL, plane_size, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, 0);

      close(fd);
    }
  }

  if (img_pixels == NULL || img_mask == NULL) {
    printf("%s::cannot mmap memory for a 2D image buffer (pixels+mask).\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  std::atomic<bool> bSuccess = true;

  float _pmin = FLT_MAX;
  float _pmax = -FLT_MAX;

  if (depth == 1) {
    // read/process the FITS plane (image) in parallel
    // unless this is a compressed file, in which case
    // the data can only be read sequentially

    // use ispc to process the plane
    // 1. endianness
    // 2. fill-in {pixels,mask}

    // get pmin, pmax
    int max_threads = omp_get_max_threads();

    // keep the worksize within int32 limits
    size_t max_work_size = 1024 * 1024 * 1024;
    size_t work_size = MIN(plane_size / max_threads, max_work_size);
    int num_threads = plane_size / work_size;

    printf("%s::fits2float32:\tsize = %zu, work_size = %zu, num_threads = %d\n",
           dataset_id.c_str(), plane_size, work_size, num_threads);

    if (is_compressed) {
      // load data into the buffer sequentially
      ssize_t bytes_read =
          gzread(this->compressed_fits_stream, img_pixels, frame_size);

      if (bytes_read != frame_size) {
        fprintf(
            stderr,
            "%s::CRITICAL: read less than %zd bytes from the FITS data unit\n",
            dataset_id.c_str(), bytes_read);
        processed_data = true;
        data_cv.notify_all();
        return;
      } else
        printf("%s::FITS data read OK.\n", dataset_id.c_str());

#pragma omp parallel for schedule(static) num_threads(no_omp_threads)          \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++) {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        ispc::fits2float32((int32_t *)&(img_pixels[start]),
                           (uint8_t *)&(img_mask[start]), bzero, bscale,
                           ignrval, datamin, datamax, _pmin, _pmax, work_size);
      };
    } else {
      // load data into the buffer in parallel chunks
      // the data part starts at <offset>

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads)         \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++) {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        // parallel read (pread) at a specified offset
        ssize_t bytes_read =
            pread(this->fits_file_desc, &(img_pixels[start]),
                  work_size * sizeof(float), offset + start * sizeof(float));

        if (bytes_read != work_size * sizeof(float)) {
          fprintf(stderr,
                  "%s::CRITICAL: only read %zd out of requested %zd bytes.\n",
                  dataset_id.c_str(), bytes_read, (work_size * sizeof(float)));
          bSuccess = false;
        } else
          ispc::fits2float32((int32_t *)&(img_pixels[start]),
                             (uint8_t *)&(img_mask[start]), bzero, bscale,
                             ignrval, datamin, datamax, _pmin, _pmax,
                             work_size);
      };
    }

    dmin = _pmin;
    dmax = _pmax;
  } else {
    printf("%s::depth > 1: work-in-progress.\n", dataset_id.c_str());
    // init the variables
    frame_min.resize(depth, FLT_MAX);
    frame_max.resize(depth, -FLT_MAX);
    mean_spectrum.resize(depth, 0.0f);
    integrated_spectrum.resize(depth, 0.0f);

    // reset the cube just in case
    fits_cube.clear();
    // init the cube with nullptr
    fits_cube.resize(depth, nullptr);

    // prepare the main image/mask
    memset(img_mask, 0, plane_size);
    for (size_t i = 0; i < plane_size; i++)
      img_pixels[i] = 0.0f;

    int max_threads = omp_get_max_threads();

    if (!is_compressed) {
      // pre-allocated floating-point read buffers
      // to reduce RAM thrashing
      std::vector<Ipp8u *> mask_buf(max_threads);

      // OpenMP per-thread {pixels,mask}
      std::vector<Ipp32f *> omp_pixels(max_threads);
      std::vector<Ipp8u *> omp_mask(max_threads);

      for (int i = 0; i < max_threads; i++) {
        mask_buf[i] = ippsMalloc_8u_L(plane_size);

        omp_pixels[i] = ippsMalloc_32f_L(plane_size);
        if (omp_pixels[i] != NULL)
          for (size_t j = 0; j < plane_size; j++)
            omp_pixels[i][j] = 0.0f;

        omp_mask[i] = ippsMalloc_8u_L(plane_size);
        if (omp_mask[i] != NULL)
          memset(omp_mask[i], 0, plane_size);
      }

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads)         \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)

      for (size_t frame = 0; frame < depth; frame++) {
        int tid = omp_get_thread_num();
        // printf("tid: %d, k: %zu\n", tid, k);
        if (mask_buf[tid] == NULL || omp_pixels[tid] == NULL ||
            omp_mask[tid] == NULL) {
          fprintf(stderr,
                  "%s::<tid::%d>::problem allocating thread-local {pixels,buf} "
                  "arrays.\n",
                  dataset_id.c_str(), tid);
          bSuccess = false;
          continue;
        }

        Ipp32f *pixels_buf = nullptr;

        // point the cube element to an mmaped region
        if (this->fits_ptr != nullptr) {
          char *ptr = (char *)this->fits_ptr;
          ptr += this->hdr_len + frame_size * frame;
          fits_cube[frame] = ptr;
          pixels_buf = (Ipp32f *)ptr;
        }

        if (pixels_buf == nullptr) {
          fprintf(stderr, "%s::<tid::%d>::CRITICAL: pixels_buf is nullptr.\n",
                  dataset_id.c_str(), tid);
          bSuccess = false;
        } else {
          float fmin = FLT_MAX;
          float fmax = -FLT_MAX;
          float mean = 0.0f;
          float integrated = 0.0f;

          float _cdelt3 = this->has_velocity
                              ? this->cdelt3 * this->frame_multiplier / 1000.0f
                              : 1.0f;

          ispc::make_image_spectrumF32_ro(
              (int32_t *)pixels_buf, mask_buf[tid], bzero, bscale, ignrval,
              datamin, datamax, _cdelt3, omp_pixels[tid], omp_mask[tid], fmin,
              fmax, mean, integrated, plane_size);

          _pmin = MIN(_pmin, fmin);
          _pmax = MAX(_pmax, fmax);
          frame_min[frame] = fmin;
          frame_max[frame] = fmax;
          mean_spectrum[frame] = mean;
          integrated_spectrum[frame] = integrated;
        }

        send_progress_notification(frame, depth);
      }

      // join omp_{pixel,mask}

      // keep the worksize within int32 limits
      size_t max_work_size = 1024 * 1024 * 1024;
      size_t work_size = MIN(plane_size / max_threads, max_work_size);
      int num_threads = plane_size / work_size;

      for (int i = 0; i < max_threads; i++) {
        float *pixels_tid = omp_pixels[i];
        unsigned char *mask_tid = omp_mask[i];

#pragma omp parallel for num_threads(no_omp_threads)
        for (int tid = 0; tid < num_threads; tid++) {
          size_t work_size = plane_size / num_threads;
          size_t start = tid * work_size;

          if (tid == num_threads - 1)
            work_size = plane_size - start;

          ispc::join_pixels_masks(&(img_pixels[start]), &(pixels_tid[start]),
                                  &(img_mask[start]), &(mask_tid[start]),
                                  work_size);
        }
      }

      // release memory
      for (int i = 0; i < max_threads; i++) {
        if (mask_buf[i] != NULL)
          ippsFree(mask_buf[i]);

        if (omp_pixels[i] != NULL)
          ippsFree(omp_pixels[i]);

        if (omp_mask[i] != NULL)
          ippsFree(omp_mask[i]);
      }

      compress_thread = std::thread(&FITS::zfp_compress, this);

      struct sched_param param;
      param.sched_priority = 0;
      if (pthread_setschedparam(compress_thread.native_handle(), SCHED_IDLE,
                                &param) != 0)
        perror("pthread_setschedparam");
      else
        printf("successfully lowered the zfp_compress thread priority to "
               "SCHED_IDLE.\n");
    } else {
      printf("%s::gz-compressed depth > 1: work-in-progress.\n",
             dataset_id.c_str());

#pragma omp parallel num_threads(no_omp_threads)
      {
#pragma omp single
        {
          for (size_t frame = 0; frame < depth; frame++) {
            // allocate {pixel_buf, mask_buf}
            std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
                                               Ipp32fFree);
            std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size),
                                            Ipp8uFree);

            if (pixels_buf.get() == NULL || mask_buf.get() == NULL) {
              printf("%s::CRITICAL::cannot malloc memory for {pixels,mask} "
                     "buffers.\n",
                     dataset_id.c_str());
              bSuccess = false;
              break;
            }

            // load data into the buffer sequentially
            ssize_t bytes_read = gzread(this->compressed_fits_stream,
                                        pixels_buf.get(), frame_size);

            if (bytes_read != frame_size) {
              fprintf(stderr,
                      "%s::CRITICAL: read less than %zd bytes from the FITS "
                      "data unit\n",
                      dataset_id.c_str(), bytes_read);
              bSuccess = false;
              break;
            }

            // process the buffer
            float fmin = FLT_MAX;
            float fmax = -FLT_MAX;
            float mean = 0.0f;
            float integrated = 0.0f;

            float _cdelt3 =
                this->has_velocity
                    ? this->cdelt3 * this->frame_multiplier / 1000.0f
                    : 1.0f;

            ispc::make_image_spectrumF32(
                (int32_t *)pixels_buf.get(), mask_buf.get(), bzero, bscale,
                ignrval, datamin, datamax, _cdelt3, img_pixels, img_mask, fmin,
                fmax, mean, integrated, plane_size);

            _pmin = MIN(_pmin, fmin);
            _pmax = MAX(_pmax, fmax);
            frame_min[frame] = fmin;
            frame_max[frame] = fmax;
            mean_spectrum[frame] = mean;
            integrated_spectrum[frame] = integrated;

            // save a plane to an mmaped-file?

            send_progress_notification(frame, depth);
          }
        }
      }
    }

    dmin = _pmin;
    dmax = _pmax;

    /*printf("FMIN/FMAX\tSPECTRUM\n");
      for (int i = 0; i < depth; i++)
      printf("%d (%f):(%f)\t\t(%f):(%f)\n", i, frame_min[i], frame_max[i],
      mean_spectrum[i], integrated_spectrum[i]); printf("\n");*/
  }

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("%s::<data:%s>\tdmin = %f\tdmax = %f\telapsed time: %5.2f [ms]\n",
         dataset_id.c_str(), (bSuccess ? "true" : "false"), dmin, dmax,
         elapsedMilliseconds);

  if (bSuccess) {
    send_progress_notification(depth, depth);
    /*for (int i = 0; i < depth; i++)
      std::cout << "mask[" << i << "]::cardinality: " << masks[i].cardinality()
      << ", size: " << masks[i].getSizeInBytes() << " bytes"
      << std::endl;*/

    make_image_statistics();

    printf("%s::statistics\npmin: %f pmax: %f median: %f mad: %f madP: %f "
           "madN: %f black: %f white: %f sensitivity: %f flux: %s\n",
           dataset_id.c_str(), this->min, this->max, this->median, this->mad,
           this->madP, this->madN, this->black, this->white, this->sensitivity,
           this->flux.c_str());

    make_image_luma();

    make_exr_image();
  } else {
    this->has_error = true;
  }

  this->has_data = bSuccess ? true : false;
  this->processed_data = true;
  this->data_cv.notify_all();
  this->timestamp = std::time(nullptr);
}

void FITS::make_exr_image() {
  auto start_t = steady_clock::now();

  // save luminance only for the time being

  /*Array2D<Rgba> pixels(height, width);

#pragma omp parallel for
  for (long i = 0; i < height; i++) {
    size_t offset = i * height;

    for (long j = 0; j < width; j++) {
      Rgba &p = pixels[i][j];

      if (img_mask[offset + j] > 0) {
        float val = img_pixels[offset + j];
        p.r = val;
        p.g = val;
        p.b = val;
        p.a = 1.0f;
      } else {
        p.r = 0.0f;
        p.g = 0.0f;
        p.b = 0.0f;
        p.a = 1.0f;
      }
    }
  }*/

  /*size_t work_size = width * height;
  ispc::image_to_luminance_f32_logarithmic_inplace(
      img_pixels, img_mask, this->min, this->max, this->lmin, this->lmax,
      work_size);*/

  size_t total_size = width * height;
  Ipp16u *mask = ippsMalloc_16u_L(total_size);

  if (mask == NULL) {
    printf("%s::cannot malloc memory for a UNIT mask buffer.\n",
           dataset_id.c_str());
    return;
  }

#pragma omp parallel for simd
  for (size_t i = 0; i < total_size; i++)
    mask[i] = img_mask[i];

  // export EXR in a YA format
  std::string filename = FITSCACHE + std::string("/") +
                         boost::replace_all_copy(dataset_id, "/", "_") +
                         std::string(".exr");
  try {
    Header header(width, height);
    header.compression() = DWAB_COMPRESSION;
    header.channels().insert("Y", Channel(FLOAT));
    header.channels().insert("A", Channel(UINT));

    OutputFile file(filename.c_str(), header);
    FrameBuffer frameBuffer;

    frameBuffer.insert("Y", Slice(FLOAT, (char *)img_pixels, sizeof(Ipp32f) * 1,
                                  sizeof(Ipp32f) * width));

    frameBuffer.insert("A", Slice(UINT, (char *)mask, sizeof(Ipp16u) * 1,
                                  sizeof(Ipp16u) * width));

    file.setFrameBuffer(frameBuffer);
    file.writePixels(height);
  } catch (const std::exception &exc) {
    std::cerr << exc.what() << std::endl;
  }

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("make_exr_image::elapsed time: %5.2f [ms]\n", elapsedMilliseconds);

  ippsFree(mask);
}

void FITS::make_image_luma() {
  auto start_t = steady_clock::now();

  int max_threads = omp_get_max_threads();

  // keep the worksize within int32 limits
  size_t total_size = width * height;
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  Ipp8u *img_luma = ippsMalloc_8u_L(total_size);

  if (img_luma == NULL) {
    printf("%s::cannot malloc memory for a 2D image luma buffer.\n",
           dataset_id.c_str());
    return;
  }

  memset(img_luma, 0, total_size);

#pragma omp parallel for
  for (int tid = 0; tid < num_threads; tid++) {
    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    // switch to a different luma based on the flux
    if (this->flux == "linear") {
      float slope = 1.0f / (this->white - this->black);
      ispc::image_to_luminance_f32_linear(&(img_pixels[start]),
                                          &(img_mask[start]), this->black,
                                          slope, &(img_luma[start]), work_size);
    }

    if (this->flux == "logistic")
      ispc::image_to_luminance_f32_logistic(
          &(img_pixels[start]), &(img_mask[start]), this->median,
          this->sensitivity, &(img_luma[start]), work_size);

    if (this->flux == "ratio")
      ispc::image_to_luminance_f32_ratio(
          &(img_pixels[start]), &(img_mask[start]), this->black,
          this->sensitivity, &(img_luma[start]), work_size);

    if (this->flux == "square")
      ispc::image_to_luminance_f32_square(
          &(img_pixels[start]), &(img_mask[start]), this->black,
          this->sensitivity, &(img_luma[start]), work_size);

    if (this->flux == "legacy")
      ispc::image_to_luminance_f32_logarithmic(
          &(img_pixels[start]), &(img_mask[start]), this->min, this->max,
          this->lmin, this->lmax, &(img_luma[start]), work_size);
  };

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("make_image_luma::elapsed time: %5.2f [ms]\n", elapsedMilliseconds);

  // export luma to a PGM file for a cross-check
  std::string filename = FITSCACHE + std::string("/") +
                         boost::replace_all_copy(dataset_id, "/", "_") +
                         std::string(".pgm");

  std::fstream pgm_file(filename, std::ios::out | std::ios::binary);

  if (!pgm_file)
    return;

  pgm_file << "P5" << std::endl;
  pgm_file << width << " " << height << " 255" << std::endl;
  pgm_file.write((const char *)img_luma, total_size);
  pgm_file.close();

  ippsFree(img_luma);
}

void FITS::make_image_statistics() {
  int max_threads = omp_get_max_threads();

  // keep the worksize within int32 limits
  size_t total_size = width * height;
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  float _pmin = FLT_MAX;
  float _pmax = -FLT_MAX;

  if (this->depth == 1) {
    _pmin = dmin;
    _pmax = dmax;
  } else {
    float _cdelt3 = this->has_velocity
                        ? this->cdelt3 * this->frame_multiplier / 1000.0f
                        : 1.0f;

    // use pixels/mask to get min/max
#pragma omp parallel for reduction(min : _pmin) reduction(max : _pmax)
    for (int tid = 0; tid < num_threads; tid++) {
      size_t work_size = total_size / num_threads;
      size_t start = tid * work_size;

      if (tid == num_threads - 1)
        work_size = total_size - start;

      // it also restores NaNs in the pixels array based on the mask
      ispc::image_min_max(&(img_pixels[start]), &(img_mask[start]), _cdelt3,
                          work_size, _pmin, _pmax);
    };
  };

  printf("%s::pixel_range<%f,%f>\n", dataset_id.c_str(), _pmin, _pmax);

  size_t len = width * height;
  std::vector<Ipp32f> v(len);
  // memcpy(v.data(), pixels, len * sizeof(Ipp32f));

  IppiSize roiSize;
  roiSize.width = width;
  roiSize.height = height;
  ippiCopy_32f_C1R(img_pixels, width * sizeof(Ipp32f), v.data(),
                   width * sizeof(Ipp32f), roiSize);

  remove_nan(v);

  make_histogram(v, hist, NBINS, _pmin, _pmax);

  median = stl_median(v);

  float _mad = 0.0f;
  int64_t _count = 0;

  float _madP = 0.0f;
  float _madN = 0.0f;
  int64_t _countP = 0;
  int64_t _countN = 0;

#pragma omp parallel for reduction(+					\
                                   : _mad) reduction(+			\
                                                     : _count) reduction(+ \
                                                                         : _madP) reduction(+ \
                                                                                            : _countP) reduction(+ \
                                                                                                                 : _madN) reduction(+ \
                                                                                                                                    : _countN)
  for (int tid = 0; tid < num_threads; tid++) {
    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    ispc::asymmetric_mad(&(img_pixels[start]), &(img_mask[start]), work_size,
                         median, _count, _mad, _countP, _madP, _countN, _madN);
  };

  if (_count > 0)
    _mad /= float(_count);

  if (_countP > 0)
    _madP /= float(_countP);
  else
    _madP = _mad;

  if (_countN > 0)
    _madN /= float(_countN);
  else
    _madN = _mad;

  // ALMAWebQL-style
  float u = 7.5f;
  float _black = MAX(_pmin, median - u * _madN);
  float _white = MIN(_pmax, median + u * _madP);
  float _sensitivity = 1.0f / (_white - _black);
  float _ratio_sensitivity = _sensitivity;

  if (this->is_optical) {
    // SubaruWebQL-style
    float u = 0.5f;
    float v = 15.0f;
    _black = MAX(_pmin, median - u * _madN);
    _white = MIN(_pmax, median + u * _madP);
    _sensitivity = 1.0f / (v * _mad);
    _ratio_sensitivity = _sensitivity;
    auto_brightness(img_pixels, img_mask, _black, _ratio_sensitivity);
  }

  if (this->flux == "") {
    long cdf[NBINS];
    float Slot[NBINS];

    long total = hist[0];
    cdf[0] = hist[0];

    for (int i = 1; i < NBINS; i++) {
      cdf[i] = cdf[i - 1] + hist[i];
      total += hist[i];
    };

    for (int i = 0; i < NBINS; i++) {
      Slot[i] = (float)cdf[i] / (float)total;
    };

    int tone_mapping_class = histogram_classifier(Slot);

    switch (tone_mapping_class) {
    case 0:
      this->flux = std::string("legacy");
      break;

    case 1:
      this->flux = std::string("linear");
      break;

    case 2:
      this->flux = std::string("logistic");
      break;

    case 3:
      this->flux = std::string("ratio");
      break;

    case 4:
      this->flux = std::string("square");
      break;

    default:
      this->flux = std::string("legacy");
    };
  }

  this->min = _pmin;
  this->max = _pmax;
  this->mad = _mad;
  this->madN = _madN;
  this->madP = _madP;
  this->black = _black;
  this->white = _white;
  this->sensitivity = _sensitivity;
  this->ratio_sensitivity = _ratio_sensitivity;
}

void make_histogram(const std::vector<Ipp32f> &v, Ipp32u *bins, int nbins,
                    float pmin, float pmax) {
  if (v.size() <= 1)
    return;

  auto start_t = steady_clock::now();

  for (int i = 0; i < nbins; i++)
    bins[i] = 0;

  int max_threads = omp_get_max_threads();

  // keep the worksize within int32 limits
  size_t total_size = v.size();
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  printf("make_histogram::num_threads: %d\n", num_threads);

#pragma omp parallel for
  for (int tid = 0; tid < num_threads; tid++) {
    Ipp32u thread_hist[NBINS];

    for (int i = 0; i < nbins; i++)
      thread_hist[i] = 0;

    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    ispc::histogram((float *)&(v[start]), work_size, thread_hist, nbins, pmin,
                    pmax);

#pragma omp critical
    {
      IppStatus sts = ippsAdd_32u_I(thread_hist, bins, nbins);

      if (sts != ippStsNoErr)
        printf("%s\n", ippGetStatusString(sts));
    };
  };

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("make_histogram::elapsed time: %5.2f [ms]\n", elapsedMilliseconds);
}

inline const char *FITS::check_null(const char *str) {
  if (str != nullptr)
    return str;
  else
    return "\"\"";
};

void FITS::to_json(std::ostringstream &json) {
  if (header == NULL || hdr_len == 0)
    return;

  Ipp8u *header_lz4 = NULL;
  int compressed_size = 0;

  // LZ4-compress the FITS header
  int worst_size = LZ4_compressBound(hdr_len);

  header_lz4 = ippsMalloc_8u_L(worst_size);

  if (header_lz4 == NULL)
    return;

  // compress the header with LZ4
  compressed_size = LZ4_compress_default(
      (const char *)header, (char *)header_lz4, hdr_len, worst_size);
  printf("FITS HEADER size %zu bytes, LZ4-compressed: %d bytes.\n", hdr_len,
         compressed_size);

  char *encoded_header = NULL;
  char *fits_header =
      base64((const unsigned char *)header_lz4, compressed_size);

  ippsFree(header_lz4);

  if (fits_header != NULL) {
    encoded_header = json_encode_string(fits_header);
    free(fits_header);
  };

  json << "{";

  // header
  json << "\"HEADERSIZE\" : " << hdr_len << ",";
  json << "\"HEADER\" : " << check_null(encoded_header) << ",";

  if (encoded_header != NULL)
    free(encoded_header);

  // fields
  json << "\"width\" : " << width << ",";
  json << "\"height\" : " << height << ",";
  json << "\"depth\" : " << depth << ",";
  json << "\"polarisation\" : " << polarisation << ",";
  json << "\"filesize\" : " << fits_file_size << ",";
  json << "\"IGNRVAL\" : " << std::scientific << ignrval << ",";

  if (std::isnan(cd1_1))
    json << "\"CD1_1\" : null,";
  else
    json << "\"CD1_1\" : " << std::scientific << cd1_1 << ",";

  if (std::isnan(cd1_2))
    json << "\"CD1_2\" : null,";
  else
    json << "\"CD1_2\" : " << std::scientific << cd1_2 << ",";

  if (std::isnan(cd2_1))
    json << "\"CD2_1\" : null,";
  else
    json << "\"CD2_1\" : " << std::scientific << cd2_1 << ",";

  if (std::isnan(cd2_2))
    json << "\"CD2_2\" : null,";
  else
    json << "\"CD2_2\" : " << std::scientific << cd2_2 << ",";

  json << "\"CRVAL1\" : " << std::scientific << crval1 << ",";

  if (std::isnan(cdelt1))
    json << "\"CDELT1\" : null,";
  else
    json << "\"CDELT1\" : " << std::scientific << cdelt1 << ",";

  json << "\"CRPIX1\" : " << std::scientific << crpix1 << ",";
  json << "\"CUNIT1\" : \"" << cunit1 << "\",";
  json << "\"CTYPE1\" : \"" << ctype1 << "\",";
  json << "\"CRVAL2\" : " << std::scientific << crval2 << ",";

  if (std::isnan(cdelt2))
    json << "\"CDELT2\" : null,";
  else
    json << "\"CDELT2\" : " << std::scientific << cdelt2 << ",";

  json << "\"CRPIX2\" : " << std::scientific << crpix2 << ",";
  json << "\"CUNIT2\" : \"" << cunit2 << "\",";
  json << "\"CTYPE2\" : \"" << ctype2 << "\",";
  json << "\"CRVAL3\" : " << std::scientific << crval3 << ",";
  json << "\"CDELT3\" : " << std::scientific << cdelt3 << ",";
  json << "\"CRPIX3\" : " << std::scientific << crpix3 << ",";
  json << "\"CUNIT3\" : \"" << cunit3 << "\",";
  json << "\"CTYPE3\" : \"" << ctype3 << "\",";
  json << "\"BMAJ\" : " << std::scientific << bmaj << ",";
  json << "\"BMIN\" : " << std::scientific << bmin << ",";
  json << "\"BPA\" : " << std::scientific << bpa << ",";
  json << "\"BUNIT\" : \"" << beam_unit << "\",";
  json << "\"BTYPE\" : \"" << beam_type << "\",";
  json << "\"SPECSYS\" : \"" << specsys << "\",";
  json << "\"RESTFRQ\" : " << std::scientific << restfrq << ",";
  json << "\"OBSRA\" : " << std::scientific << obsra << ",";
  json << "\"OBSDEC\" : " << std::scientific << obsdec << ",";
  json << "\"OBJECT\" : \"" << object << "\",";
  json << "\"DATEOBS\" : \"" << date_obs << "\",";
  json << "\"TIMESYS\" : \"" << timesys << "\",";
  json << "\"LINE\" : \"" << line << "\",";
  json << "\"FILTER\" : \"" << filter << "\",";

  // needs this->has_data

  // mean spectrum
  if (mean_spectrum.size() > 0) {
    json << "\"mean_spectrum\" : [";

    for (size_t i = 0; i < depth - 1; i++)
      json << std::scientific << mean_spectrum[i] << ",";

    json << std::scientific << mean_spectrum[depth - 1] << "],";
  } else
    json << "\"mean_spectrum\" : [],";

  // integrated spectrum
  if (integrated_spectrum.size() > 0) {
    json << "\"integrated_spectrum\" : [";

    for (size_t i = 0; i < depth - 1; i++)
      json << std::scientific << integrated_spectrum[i] << ",";

    json << std::scientific << integrated_spectrum[depth - 1] << "],";
  } else
    json << "\"integrated_spectrum\" : [],";

  // statistics
  json << "\"min\" : " << std::scientific << min << ",";
  json << "\"max\" : " << std::scientific << max << ",";
  json << "\"median\" : " << std::scientific << median << ",";
  json << "\"sensitivity\" : " << std::scientific << sensitivity << ",";
  json << "\"ratio_sensitivity\" : " << std::scientific << ratio_sensitivity
       << ",";
  json << "\"black\" : " << std::scientific << black << ",";
  json << "\"white\" : " << std::scientific << white << ",";
  json << "\"flux\" : \"" << flux << "\",";

  // histogram
  json << "\"histogram\" : [";
  for (size_t i = 0; i < NBINS - 1; i++)
    json << hist[i] << ",";
  json << hist[NBINS - 1] << "]}";
}

void FITS::auto_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                           float &_ratio_sensitivity) {
  if (std::isnan(_ratio_sensitivity))
    return;

  float target_brightness = 0.1f;
  int max_iter = 20;
  int iter = 0;

  float a = 0.01f * _ratio_sensitivity;
  float b = 100.0f * _ratio_sensitivity;

  // perform the first step manually (verify that br(a) <= target_brightness <=
  // br(b) )
  float a_brightness = calculate_brightness(_pixels, _mask, _black, a);
  float b_brightness = calculate_brightness(_pixels, _mask, _black, b);

  if (target_brightness < a_brightness || target_brightness > b_brightness)
    return;

  do {
    _ratio_sensitivity = 0.5f * (a + b);
    float brightness =
        calculate_brightness(_pixels, _mask, _black, _ratio_sensitivity);

    printf("iteration: %d, sensitivity: %f, brightness: %f divergence: %f\n",
           iter, _ratio_sensitivity, brightness,
           fabs(target_brightness - brightness));

    if (brightness > target_brightness)
      b = _ratio_sensitivity;

    if (brightness < target_brightness)
      a = _ratio_sensitivity;

    if (fabs(target_brightness - brightness) < 0.1f * target_brightness)
      break;

  } while (iter++ < max_iter);

  // an approximate solution
  _ratio_sensitivity = 0.5f * (a + b);

  printf("bi-section sensitivity = %f\n", _ratio_sensitivity);
}

float FITS::calculate_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                                 float _sensitivity) {
  int max_threads = omp_get_max_threads();
  size_t total_size = width * height;
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  float brightness = 0.0f;

#pragma omp parallel for reduction(+ : brightness)
  for (int tid = 0; tid < num_threads; tid++) {
    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    brightness = ispc::pixels_mean_brightness_ratio(&(img_pixels[start]),
                                                    &(img_mask[start]), _black,
                                                    _sensitivity, work_size);
  };

  return brightness / float(num_threads);
}

void FITS::send_progress_notification(size_t running, size_t total) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  double elapsed;
  elapsed = (now.tv_sec - this->created.tv_sec) * 1e9;
  elapsed = (elapsed + (now.tv_nsec - this->created.tv_nsec)) * 1e-9;

  std::lock_guard<std::shared_mutex> guard(progress_mtx);

  this->progress.running = running;
  this->progress.total = total;
  this->progress.elapsed = elapsed;

  /*std::ostringstream json;
  json << "{"
       << "\"type\" : \"progress\",";
  json << "\"message\" : \"loading FITS\",";
  json << "\"total\" : " << total << ",";
  json << "\"running\" : " << running << ",";
  json << "\"elapsed\" : " << elapsed << "}";*/

  /*bool forced = (running == total) ? true : false;
  if(boost::shared_ptr<shared_state> _state = state_.lock())
    _state->send_progress  (json.str(), dataset_id, forced);*/

  /*std::shared_lock<std::shared_mutex> lock(m_progress_mutex);
  TWebSocketList connections = m_progress[this->dataset_id];

  for (auto it = connections.begin(); it != connections.end(); ++it) {
    TWebSocket *ws = *it;

    struct UserData *user = (struct UserData *)ws->getUserData();

    if (user != NULL) {
      if (check_progress_timeout(user->ptr, system_clock::now()) ||
          (running == total)) {
        // std::cout << json.str() << std::endl;
        ws->send(json.str(), uWS::OpCode::TEXT);
        update_progress_timestamp(user->ptr);
      }
    }
  };*/
}

void FITS::zfp_compress() {
  printf("[%s]::zfp_compress started.\n", dataset_id.c_str());

  // do nothing for single planes
  if (depth <= 1)
    return;

    // use blocks of 4; each zfp_compress_4_frames
#pragma omp parallel for
  for (size_t k = 0; k < depth; k += 4)
    zfp_compress_cube(k);

  printf("[%s]::zfp_compress ended.\n", dataset_id.c_str());
}

void FITS::zfp_compress_cube(size_t start_k) {
  size_t end_k = MIN(start_k + 4, depth);

  for (size_t i = start_k; i < end_k; i++)
    if (fits_cube[i] == NULL)
      return;

  // allocate memory for pixels and a mask
  const size_t plane_size = width * height;
  const size_t frame_size = plane_size * abs(bitpix / 8);

  Ipp32f *pixels[4];
  Ipp8u *mask[4];
  float mean[4];

  bool ok = true;

  for (int i = 0; i < 4; i++) {
    mean[i] = 0.0f;

    pixels[i] = ippsMalloc_32f_L(plane_size);
    if (pixels[i] == NULL)
      ok = false;
    else
      for (size_t j = 0; j < plane_size; j++)
        pixels[i][j] = 0.0f;

    mask[i] = ippsMalloc_8u_L(plane_size);
    if (mask[i] == NULL)
      ok = false;
    else
      memset(mask[i], 0, plane_size);
  }

  if (!ok) {
    for (int i = 0; i < 4; i++) {
      if (pixels[i] != NULL)
        ippsFree(pixels[i]);

      if (mask[i] != NULL)
        ippsFree(mask[i]);
    }

    return;
  }

  // use ispc to fill in the pixels and mask
  int offset = 0;
  for (size_t frame = start_k; frame < end_k; frame++) {
    float _mean;
    ispc::make_planeF32((int32_t *)fits_cube[frame], bzero, bscale, ignrval,
                        datamin, datamax, pixels[offset], mask[offset], _mean,
                        plane_size);

    mean[offset++] = _mean;
  }

  int maxX = roundUp(width, 4);
  int maxY = roundUp(height, 4);

  int encStateSize;
  IppEncodeZfpState_32f *pEncState;
  int pComprLen = 0;

  Ipp8u *pBuffer = ippsMalloc_8u(sizeof(Ipp32f) * maxX * maxY);
  Ipp64f accur = 1.0e-3;

  ippsEncodeZfpGetStateSize_32f(&encStateSize);
  pEncState = (IppEncodeZfpState_32f *)ippsMalloc_8u(encStateSize);
  ippsEncodeZfpInit_32f(pBuffer, sizeof(Ipp32f) * (maxX * maxY), pEncState);
  ippsEncodeZfpSetAccuracy_32f(accur, pEncState);

  // the code needs to be re-written in order to use full 4x4x4 blocks

  int x, y;
  int i, j, k;
  float val;
  float block[4 * 4 * 4];

  // compress the pixels with ZFP
  for (y = 0; y < height; y += 4)
    for (x = 0; x < width; x += 4) {
      // fill a 4x4x4 block
      int offset = 0;
      for (k = 0; k < 4; k++) {
        for (j = y; j < y + 4; j++)
          for (i = x; i < x + 4; i++) {
            if (i >= width || j >= height)
              val = mean[k];
            else {
              size_t src = j * width + i;

              if (mask[k][src] == 0)
                val = mean[k];
              else
                val = 1.17f; // pixels[k][src];
            }

            block[offset++] = val;
          }
      }

      /*for (offset = 0; offset < 4 * 4 * 4; offset++)
        block[offset] = 1.17f;*/

      ippsEncodeZfp444_32f(block, 4 * sizeof(Ipp32f), 4 * 4 * sizeof(Ipp32f),
                           pEncState);
    }

  ippsEncodeZfpFlush_32f(pEncState);
  ippsEncodeZfpGetCompressedSize_32f(pEncState, &pComprLen);
  ippsFree(pEncState);

  // compress the four masks with LZ4

  printf("zfp-compressing 4 frames starting at %zu; ZFP::pComprLen = %d, orig. "
         "= %zu bytes.\n",
         start_k, pComprLen, frame_size);

  if (pBuffer != NULL)
    ippsFree(pBuffer);

  for (int i = 0; i < 4; i++) {
    if (pixels[i] != NULL)
      ippsFree(pixels[i]);

    if (mask[i] != NULL)
      ippsFree(mask[i]);
  }
}