#include "../fits.h"
#include "fits.hpp"

#include "global.h"

#include "json.h"
//#include "roaring.c"

#include "json.h"

// Intel IPP ZFP functions
#include <ippdc.h>
#include <limits>

#if !defined(__APPLE__) || !defined(__MACH__)
// bytes swap
#include <byteswap.h>
#endif

// base64 encoding with SSL
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <experimental/algorithm>
#else
#include <parallel/algorithm>
#endif

char *base64(const unsigned char *input, int length)
{
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

int roundUp(int numToRound, int multiple)
{
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
#include <tuple>
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
  if (p != NULL)
  {
    // printf("freeing <Ipp32f*>#%zu\t", counter++);
    ippsFree(p);
  }
};

auto Ipp8uFree = [](Ipp8u *p) {
  static size_t counter = 0;
  if (p != NULL)
  {
    // printf("freeing <Ipp8u*>#%zu\t", counter++);
    ippsFree(p);
  }
};

inline std::tuple<int, int> make_indices(int x, int y)
{
  int idx = x / ZFP_CACHE_REGION;
  int idy = y / ZFP_CACHE_REGION;

  return {idx, idy};
}

void hdr_set_long_value(char *hdr, long value)
{
  unsigned int len = sprintf(hdr, "%ld", value);

  size_t num = FITS_LINE_LENGTH - 10 - len;

  if (num > 0)
    memset(hdr + len, ' ', num);
};

void hdr_set_double_value(char *hdr, double value)
{
  unsigned int len = sprintf(hdr, "%E", value);

  size_t num = FITS_LINE_LENGTH - 10 - len;

  if (num > 0)
    memset(hdr + len, ' ', num);
};

int hdr_get_int_value(char *hdr)
{
  printf("VALUE(%s)\n", hdr);

  return atoi(hdr);
};

long hdr_get_long_value(char *hdr)
{
  printf("VALUE(%s)\n", hdr);

  return atol(hdr);
};

double hdr_get_double_value(char *hdr)
{
  printf("VALUE(%s)\n", hdr);

  return atof(hdr);
};

std::string hdr_get_string_value(char *hdr)
{
  char string[FITS_LINE_LENGTH] = "";

  printf("VALUE(%s)\n", hdr);

  sscanf(hdr, "'%s'", string);

  if (string[strlen(string) - 1] == '\'')
    string[strlen(string) - 1] = '\0';

  return std::string(string);
};

std::string hdr_get_string_value_with_spaces(char *hdr)
{
  char string[FITS_LINE_LENGTH] = "";

  printf("VALUE(%s)\n", hdr);

  char *pos = strstr(hdr, "'");

  if (pos != NULL)
  {
    char *tmp = strstr(pos + 1, "'");

    if (tmp != NULL)
    {
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

void remove_nan(std::vector<Ipp32f> &v)
{
  if (v.empty())
    return;

  size_t n = v.size();
  size_t v_end = n - 1;

  // this does not leave an empty vector when all elements are NAN
  // the first NAN remains...
  // it's OK, the median-finding functions will deal with this border-line case

  // iterate through the vector, replacing NAN/INFINITE with valid numbers from
  // the end
  for (size_t i = 0; i <= v_end; i++)
  {
    if (!std::isfinite(v[i]))
    {
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

Ipp32f stl_median(std::vector<Ipp32f> &v)
{
  if (v.empty())
    return NAN;

  if (v.size() == 1)
    return v[0];

  auto start_t = steady_clock::now();

  Ipp32f medVal = NAN;

  size_t n = v.size() / 2;
#if defined(__APPLE__) && defined(__MACH__)
  std::nth_element(v.begin(), v.begin() + n, v.end());
#else
  __gnu_parallel::nth_element(v.begin(), v.begin() + n, v.end());
#endif

  if (v.size() % 2)
  {
    medVal = v[n];
  }
  else
  {
    // even sized vector -> average the two middle values
#if defined(__APPLE__) && defined(__MACH__)
    auto max_it = std::max_element(v.begin(), v.begin() + n);
#else
    auto max_it = __gnu_parallel::max_element(v.begin(), v.begin() + n);
#endif
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

FITS::FITS()
{
  std::cout << this->dataset_id << "::default constructor." << std::endl;

  this->timestamp = std::time(nullptr);
  clock_gettime(CLOCK_MONOTONIC, &(this->created));
  this->fits_file_desc = -1;
  this->compressed_fits_stream = NULL;
  this->fits_file_size = 0;
  this->gz_compressed = false;
  this->header = NULL;
  this->hdr_len = 0;
  this->defaults();
}

FITS::FITS(std::string id, std::string flux)
{
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
  this->defaults();

  // deserialise();
}

FITS::~FITS()
{
  terminate = true;
  purge_cv.notify_all();

  /*if (compress_thread.joinable())
    compress_thread.join();*/

  for (auto &thread : zfp_pool)
  {
    static int tid = 0;

    if (thread.joinable())
      thread.join();
    else
      printf("thread %d is not joinable\n", tid++);
  }

  // trust but verify
  /*#pragma omp parallel for
  for (size_t k = 0; k < depth; k++)
    zfp_decompress_cube(k);*/

  /*if (depth > 100)
    zfp_decompress_cube(100);*/

  std::cout << this->dataset_id << "::destructor." << std::endl;

  if (purge_thread.joinable())
    purge_thread.join();

  // clear the cache of nested std::maps
  cache.clear();

  // clear the cube containing shared pointers to (mmaped or not) memory regions
  fits_cube.clear();

  // reset the pointer to the underlying FITS file memory region
  fits_ptr.reset();

  // clear compressed cube regions
  int null_pixels = 0;
  for (auto i = 0; i < cube_pixels.size(); i++)
  {
    auto ptr = cube_pixels[i].load();

    if (ptr != nullptr)
      delete ptr;
    else
      null_pixels++;
  }

  int null_masks = 0;
  for (auto i = 0; i < cube_mask.size(); i++)
  {
    auto ptr = cube_mask[i].load();

    if (ptr != nullptr)
      delete ptr;
    else
      null_masks++;
  }

  printf("found %d null cube_pixels entries.\n", null_pixels);
  printf("found %d null cube_mask entries.\n", null_masks);

  cube_pixels.clear();
  cube_mask.clear();

  if (fits_file_desc != -1)
    close(fits_file_desc);

  if (compressed_fits_stream != NULL)
    gzclose(compressed_fits_stream);

  if (header != NULL)
    free(header);

  img_pixels.reset();
  img_mask.reset();

  // serialise to disk
  serialise();
}

void FITS::purge_cache()
{
  int count = 0;

  // iterate through all elements of the cache, purging stale entries
  for (size_t i = 0; i < cache.size(); i++)
  {
    int pixels_idz = i / 4;

    // lock the cache for writing (just in case)
    std::lock_guard<std::shared_mutex> guard(cache_mtx[pixels_idz]);

    decompressed_blocks &z_entry = cache[i];

    auto y_it = z_entry.begin();

    while (y_it != z_entry.end())
    {
      int y_key = y_it->first;

      auto x_it = y_it->second.begin();

      while (x_it != y_it->second.end())
      {
        int x_key = x_it->first;
        std::shared_ptr<struct CacheEntry> entry = x_it->second;

        bool deleted = false;

        if (entry)
        {
          struct CacheEntry *_entry = entry.get();

          // check the timestamp
          timestamp = std::time(nullptr);

          if (timestamp - _entry->timestamp > CACHE_TIMEOUT)
          {
            // remove the key from std::map
            // this will deallocate the std::shared_ptr cache entry too
            x_it = y_it->second.erase(x_it);

            deleted = true;
            /*printf("[%s] erased a stale cache entry(%zu:%d:%d).\n",
                   dataset_id.c_str(), i, y_key, x_key);*/
            count++;
          }
        }

        if (!deleted)
          x_it++;
      }

      y_it++;
    }
  }

  if (count > 0)
    printf("[%s] erased %d stale cache entries.\n", dataset_id.c_str(), count);
}

void FITS::defaults()
{
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
  bzero = 0.0f;
  ignrval = -FLT_MAX;
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

void FITS::serialise()
{
  std::string filename = FITSCACHE + std::string("/") +
                         boost::replace_all_copy(dataset_id, "/", "_") +
                         std::string(".json");

  struct stat64 st;
  int stat = stat64(filename.c_str(), &st);

  if (stat == 0)
    // the file already exists, do nothing
    return;

  std::string tmp = filename + ".tmp";

  /*FILE *fp = fopen(tmp.c_str(), "wb");
  if (fp == NULL)
    return;*/

  std::ofstream fp(tmp);

  if (!fp)
    return;

  // serialise to JSON
  JsonNode *json = json_mkobject();

  if (json == NULL)
    return;

  JsonNode *flux_json = json_mkstring(flux.c_str());
  if (flux_json != NULL)
    json_append_member(json, "flux", flux_json);

  JsonNode *width_json = json_mknumber(width);
  if (width_json != NULL)
    json_append_member(json, "width", width_json);

  JsonNode *height_json = json_mknumber(height);
  if (height_json != NULL)
    json_append_member(json, "height", height_json);

  JsonNode *depth_json = json_mknumber(depth);
  if (depth_json != NULL)
    json_append_member(json, "depth", depth_json);

  JsonNode *polarisation_json = json_mknumber(polarisation);
  if (polarisation_json != NULL)
    json_append_member(json, "polarisation", polarisation_json);

  JsonNode *bitpix_json = json_mknumber(bitpix);
  if (bitpix_json != NULL)
    json_append_member(json, "bitpix", bitpix_json);

  JsonNode *naxis_json = json_mknumber(naxis);
  if (naxis_json != NULL)
    json_append_member(json, "naxis", naxis_json);

  // next build up an array <int naxes[4]>
  JsonNode *_naxes[4];
  JsonNode *naxes_json = json_mkarray();
  if (naxes_json != NULL)
  {
    for (int i = 0; i < 4; i++)
    {
      _naxes[i] = json_mknumber(naxes[i]);
      if (_naxes[i] != NULL)
        json_append_element(naxes_json, _naxes[i]);
    }

    json_append_member(json, "naxes", naxes_json);
  }

  JsonNode *btype_json = json_mkstring(btype.c_str());
  if (btype_json != NULL)
    json_append_member(json, "btype", btype_json);

  JsonNode *bunit_json = json_mkstring(bunit.c_str());
  if (bunit_json != NULL)
    json_append_member(json, "bunit", bunit_json);

  JsonNode *bscale_json = json_mknumber(bscale);
  if (bscale_json != NULL)
    json_append_member(json, "bscale", bscale_json);

  JsonNode *bzero_json = json_mknumber(bzero);
  if (bzero_json != NULL)
    json_append_member(json, "bzero", bzero_json);

  JsonNode *ignrval_json = json_mknumber(ignrval);
  if (ignrval_json != NULL)
    json_append_member(json, "ignrval", ignrval_json);

  JsonNode *crval1_json = json_mknumber(crval1);
  if (crval1_json != NULL)
    json_append_member(json, "crval1", crval1_json);

  JsonNode *cdelt1_json = json_mknumber(cdelt1);
  if (cdelt1_json != NULL)
    json_append_member(json, "cdelt1", cdelt1_json);

  JsonNode *crpix1_json = json_mknumber(crpix1);
  if (crpix1_json != NULL)
    json_append_member(json, "crpix1", crpix1_json);

  JsonNode *cunit1_json = json_mkstring(cunit1.c_str());
  if (cunit1_json != NULL)
    json_append_member(json, "cunit1", cunit1_json);

  JsonNode *ctype1_json = json_mkstring(ctype1.c_str());
  if (ctype1_json != NULL)
    json_append_member(json, "ctype1", ctype1_json);

  JsonNode *crval2_json = json_mknumber(crval2);
  if (crval2_json != NULL)
    json_append_member(json, "crval2", crval2_json);

  JsonNode *cdelt2_json = json_mknumber(cdelt2);
  if (cdelt2_json != NULL)
    json_append_member(json, "cdelt2", cdelt2_json);

  JsonNode *crpix2_json = json_mknumber(crpix2);
  if (crpix2_json != NULL)
    json_append_member(json, "crpix2", crpix2_json);

  JsonNode *cunit2_json = json_mkstring(cunit2.c_str());
  if (cunit2_json != NULL)
    json_append_member(json, "cunit2", cunit2_json);

  JsonNode *ctype2_json = json_mkstring(ctype2.c_str());
  if (ctype2_json != NULL)
    json_append_member(json, "ctype2", ctype2_json);

  JsonNode *crval3_json = json_mknumber(crval3);
  if (crval3_json != NULL)
    json_append_member(json, "crval3", crval3_json);

  JsonNode *cdelt3_json = json_mknumber(cdelt3);
  if (cdelt3_json != NULL)
    json_append_member(json, "cdelt3", cdelt3_json);

  JsonNode *crpix3_json = json_mknumber(crpix3);
  if (crpix3_json != NULL)
    json_append_member(json, "crpix3", crpix3_json);

  JsonNode *cunit3_json = json_mkstring(cunit3.c_str());
  if (cunit3_json != NULL)
    json_append_member(json, "cunit3", cunit3_json);

  JsonNode *ctype3_json = json_mkstring(ctype3.c_str());
  if (ctype3_json != NULL)
    json_append_member(json, "ctype3", ctype3_json);

  JsonNode *frame_multiplier_json = json_mknumber(frame_multiplier);
  if (frame_multiplier_json != NULL)
    json_append_member(json, "frame_multiplier", frame_multiplier_json);

  JsonNode *cd1_1_json = json_mknumber(cd1_1);
  if (cd1_1_json != NULL)
    json_append_member(json, "cd1_1", cd1_1_json);

  JsonNode *cd1_2_json = json_mknumber(cd1_2);
  if (cd1_2_json != NULL)
    json_append_member(json, "cd1_2", cd1_2_json);

  JsonNode *cd2_1_json = json_mknumber(cd2_1);
  if (cd2_1_json != NULL)
    json_append_member(json, "cd2_1", cd2_1_json);

  JsonNode *cd2_2_json = json_mknumber(cd2_2);
  if (cd2_2_json != NULL)
    json_append_member(json, "cd2_2", cd2_2_json);

  JsonNode *bmaj_json = json_mknumber(bmaj);
  if (bmaj_json != NULL)
    json_append_member(json, "bmaj", bmaj_json);

  JsonNode *bmin_json = json_mknumber(bmin);
  if (bmin_json != NULL)
    json_append_member(json, "bmin", bmin_json);

  JsonNode *bpa_json = json_mknumber(bpa);
  if (bpa_json != NULL)
    json_append_member(json, "bpa", bpa_json);

  JsonNode *restfrq_json = json_mknumber(restfrq);
  if (restfrq_json != NULL)
    json_append_member(json, "restfrq", restfrq_json);

  JsonNode *obsra_json = json_mknumber(obsra);
  if (obsra_json != NULL)
    json_append_member(json, "obsra", obsra_json);

  JsonNode *obsdec_json = json_mknumber(obsdec);
  if (obsdec_json != NULL)
    json_append_member(json, "obsdec", obsdec_json);

  JsonNode *datamin_json = json_mknumber(datamin);
  if (datamin_json != NULL)
    json_append_member(json, "datamin", datamin_json);

  JsonNode *datamax_json = json_mknumber(datamax);
  if (datamax_json != NULL)
    json_append_member(json, "datamax", datamax_json);

  // export JSON to string

  char *json_str = json_encode(json);

  if (json_str != NULL)
  {
    std::cout << json_str << std::endl;

    fp << json_str << std::endl;
    free(json_str);
  }

  // clear out JSON nodes

  if (flux_json != NULL)
    json_delete(flux_json);

  if (width_json != NULL)
    json_delete(width_json);

  if (height_json != NULL)
    json_delete(height_json);

  if (depth_json != NULL)
    json_delete(depth_json);

  if (polarisation_json != NULL)
    json_delete(polarisation_json);

  if (bitpix_json != NULL)
    json_delete(bitpix_json);

  if (naxis_json != NULL)
    json_delete(naxis_json);

  if (naxes_json != NULL)
  {
    for (int i = 0; i < 4; i++)
      if (_naxes[i] != NULL)
        json_delete(_naxes[i]);

    json_delete(naxes_json);
  }

  if (btype_json != NULL)
    json_delete(btype_json);

  if (bunit_json != NULL)
    json_delete(bunit_json);

  if (bscale_json != NULL)
    json_delete(bscale_json);

  if (bzero_json != NULL)
    json_delete(bzero_json);

  if (ignrval_json != NULL)
    json_delete(ignrval_json);

  if (crval1_json != NULL)
    json_delete(crval1_json);

  if (cdelt1_json != NULL)
    json_delete(cdelt1_json);

  if (crpix1_json != NULL)
    json_delete(crpix1_json);

  if (cunit1_json != NULL)
    json_delete(cunit1_json);

  if (ctype1_json != NULL)
    json_delete(ctype1_json);

  if (crval2_json != NULL)
    json_delete(crval2_json);

  if (cdelt2_json != NULL)
    json_delete(cdelt2_json);

  if (crpix2_json != NULL)
    json_delete(crpix2_json);

  if (cunit2_json != NULL)
    json_delete(cunit2_json);

  if (ctype2_json != NULL)
    json_delete(ctype2_json);

  if (crval3_json != NULL)
    json_delete(crval3_json);

  if (cdelt3_json != NULL)
    json_delete(cdelt3_json);

  if (crpix3_json != NULL)
    json_delete(crpix3_json);

  if (cunit3_json != NULL)
    json_delete(cunit3_json);

  if (ctype3_json != NULL)
    json_delete(ctype3_json);

  if (frame_multiplier_json != NULL)
    json_delete(frame_multiplier_json);

  if (cd1_1_json != NULL)
    json_delete(cd1_1_json);

  if (cd1_2_json != NULL)
    json_delete(cd1_2_json);

  if (cd2_1_json != NULL)
    json_delete(cd2_1_json);

  if (cd2_2_json != NULL)
    json_delete(cd2_2_json);

  if (bmaj_json != NULL)
    json_delete(bmaj_json);

  if (bmin_json != NULL)
    json_delete(bmin_json);

  if (bpa_json != NULL)
    json_delete(bpa_json);

  if (restfrq_json != NULL)
    json_delete(restfrq_json);

  if (obsra_json != NULL)
    json_delete(obsra_json);

  if (obsdec_json != NULL)
    json_delete(obsdec_json);

  if (datamin_json != NULL)
    json_delete(datamin_json);

  if (datamax_json != NULL)
    json_delete(datamax_json);

  json_delete(json);

  fp.close();

  //rename the temporary file
  //rename(tmp.c_str(), filename.c_str());
}

void FITS::update_timestamp() { timestamp = std::time(nullptr); }

void FITS::frame_reference_type()
{
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

void FITS::frame_reference_unit()
{
  const char *_cunit3 = cunit3.c_str();

  if (!strcasecmp(_cunit3, "Hz"))
  {
    has_frequency = true;
    frame_multiplier = 1.0f;
    return;
  };

  if (!strcasecmp(_cunit3, "kHz"))
  {
    has_frequency = true;
    frame_multiplier = 1e3f;
    return;
  };

  if (!strcasecmp(_cunit3, "MHz"))
  {
    has_frequency = true;
    frame_multiplier = 1e6f;
    return;
  };

  if (!strcasecmp(_cunit3, "GHz"))
  {
    has_frequency = true;
    frame_multiplier = 1e9f;
    return;
  };

  if (!strcasecmp(_cunit3, "THz"))
  {
    has_frequency = true;
    frame_multiplier = 1e12f;
    return;
  };

  if (!strcasecmp(_cunit3, "m/s"))
  {
    has_velocity = true;
    frame_multiplier = 1.0f;
    return;
  };

  if (!strcasecmp(_cunit3, "km/s"))
  {
    has_velocity = true;
    frame_multiplier = 1e3f;
    return;
  };
}

void FITS::get_spectrum_range(double frame_start, double frame_end,
                              double ref_freq, int &start, int &end)
{
  if (depth <= 1)
  {
    start = 0;
    end = 0;
    return;
  }

  int _start, _end;

  if (has_velocity && ref_freq > 0.0)
  {
    get_freq2vel_bounds(frame_start, frame_end, ref_freq, _start, _end);

    start = _start;
    end = _end;

    return;
  }

  if (has_frequency && ref_freq > 0.0)
  {
    get_frequency_bounds(frame_start, frame_end, _start, _end);

    start = _start;
    end = _end;

    return;
  }

  if (has_velocity)
  {
    get_velocity_bounds(frame_start, frame_end, _start, _end);

    start = _start;
    end = _end;

    return;
  }
}

void FITS::get_freq2vel_bounds(double frame_start, double frame_end,
                               double ref_freq, int &start, int &end)
{
  if (!has_header)
  {
    start = 0;
    end = 0;
    return;
  }

  if (depth <= 1)
  {
    start = 0;
    end = 0;
    return;
  }

  if ((restfrq <= 0.0) && (ref_freq <= 0.0))
  {
    start = 0;
    end = depth - 1;
    return;
  }

  double c = 299792458; // speed of light [m/s]
  double fRatio;
  double v1, v2;
  double RESTFRQ;

  if (ref_freq > 0.0)
    RESTFRQ = ref_freq;
  else
    RESTFRQ = this->restfrq;

  fRatio = frame_start / RESTFRQ;
  v1 = (1.0 - fRatio * fRatio) / (1.0 + fRatio * fRatio) * c;

  fRatio = frame_end / RESTFRQ;
  v2 = (1.0 - fRatio * fRatio) / (1.0 + fRatio * fRatio) * c;

  double x1, x2;

  x1 = crpix3 + (v1 - crval3 * frame_multiplier) / (cdelt3 * frame_multiplier) -
       1.0;
  x2 = crpix3 + (v2 - crval3 * frame_multiplier) / (cdelt3 * frame_multiplier) -
       1.0;

  start = (int)round(x1);
  end = (int)round(x2);

  if (cdelt3 < 0.0)
  {
    start = depth - 1 - start;
    end = depth - 1 - end;
  };

  if (end < start)
  {
    int tmp = start;
    start = end;
    end = tmp;
  };

  start = MAX(start, 0);
  start = MIN(start, depth - 1);

  end = MAX(end, 0);
  end = MIN(end, depth - 1);

  std::cout << "<" << frame_start << "," << frame_end << ">\tstart = " << start
            << "\tend = " << end << std::endl;

  return;
}

void FITS::get_frequency_bounds(double freq_start, double freq_end, int &start,
                                int &end)
{
  if (FPzero(freq_start) || FPzero(freq_end))
  {
    start = 0;
    end = depth - 1;
    return;
  }

  double f1 =
      crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
  double f2 = crval3 * frame_multiplier +
              cdelt3 * frame_multiplier * (double(depth) - crpix3);

  double band_lo = MIN(f1, f2);
  double band_hi = MAX(f1, f2);

  if (cdelt3 > 0.0)
  {
    start = (int)round((freq_start - band_lo) / (band_hi - band_lo) *
                       double(depth - 1));
    end = (int)round((freq_end - band_lo) / (band_hi - band_lo) *
                     double(depth - 1));
  }
  else
  {
    start = (int)round((band_hi - freq_start) / (band_hi - band_lo) *
                       double(depth - 1));
    end = (int)round((band_hi - freq_end) / (band_hi - band_lo) *
                     double(depth - 1));
  };

  if (end < start)
  {
    int tmp = start;
    start = end;
    end = tmp;
  };

  start = MAX(start, 0);
  start = MIN(start, depth - 1);

  end = MAX(end, 0);
  end = MIN(end, depth - 1);

  std::cout << "<" << band_lo << "," << band_hi << ">\tdepth = " << depth
            << "\tstart = " << start << "\tend = " << end << std::endl;
}

void FITS::get_velocity_bounds(double vel_start, double vel_end, int &start,
                               int &end)
{
  if (!has_header)
  {
    start = 0;
    end = 0;
    return;
  }

  if (depth <= 1)
  {
    start = 0;
    end = 0;
    return;
  }

  double v1 =
      crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
  double v2 = crval3 * frame_multiplier +
              cdelt3 * frame_multiplier * (double(depth) - crpix3);

  double band_lo = MIN(v1, v2);
  double band_hi = MAX(v1, v2);

  if (cdelt3 > 0.0)
  {
    start = (int)round((vel_start - band_lo) / (band_hi - band_lo) *
                       double(depth - 1));
    end = (int)round((vel_end - band_lo) / (band_hi - band_lo) *
                     double(depth - 1));
  }
  else
  {
    start = (int)round((band_hi - vel_start) / (band_hi - band_lo) *
                       double(depth - 1));
    end = (int)round((band_hi - vel_end) / (band_hi - band_lo) *
                     double(depth - 1));
  };

  if (end < start)
  {
    int tmp = start;
    start = end;
    end = tmp;
  };

  start = MAX(start, 0);
  start = MIN(start, depth - 1);

  end = MAX(end, 0);
  end = MIN(end, depth - 1);

  std::cout << "<" << band_lo << "," << band_hi << ">\tdepth = " << depth
            << "\tstart = " << start << "\tend = " << end << std::endl;
}

void FITS::get_frequency_range(double &freq_start, double &freq_end)
{
  if (has_velocity)
  {
    double c = 299792458; // speed of light [m/s]

    double v1 =
        crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
    double v2 = crval3 * frame_multiplier +
                cdelt3 * frame_multiplier * (double(depth) - crpix3);

    double f1 = restfrq * sqrt((1.0 - v1 / c) / (1.0 + v1 / c));
    double f2 = restfrq * sqrt((1.0 - v2 / c) / (1.0 + v2 / c));

    freq_start = MIN(f1, f2) / 1.0E9; //[Hz -> GHz]
    freq_end = MAX(f1, f2) / 1.0E9;   //[Hz -> GHz]
  }
  else if (has_frequency)
  {
    double f1 =
        crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
    double f2 = crval3 * frame_multiplier +
                cdelt3 * frame_multiplier * (double(depth) - crpix3);

    freq_start = MIN(f1, f2) / 1.0E9; //[Hz -> GHz]
    freq_end = MAX(f1, f2) / 1.0E9;   //[Hz -> GHz]
  }
}

bool FITS::process_fits_header_unit(const char *buf)
{
  char hdrLine[FITS_LINE_LENGTH + 1];
  bool end = false;

  hdrLine[sizeof(hdrLine) - 1] = '\0';

  for (size_t offset = 0; offset < FITS_CHUNK_LENGTH;
       offset += FITS_LINE_LENGTH)
  {
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

    if (datamin == datamax)
    {
      datamin = -FLT_MAX;
      datamax = FLT_MAX;
    }

    // decide on a FITS type (optical? radio? X-ray?)
    if (strncmp(hdrLine, "TELESCOP= ", 10) == 0)
    {
      std::string telescope =
          boost::algorithm::to_lower_copy(hdr_get_string_value(hdrLine + 10));

      if (telescope.find("alma") != std::string::npos)
        is_optical = false;

      if (telescope.find("vla") != std::string::npos ||
          telescope.find("ska") != std::string::npos)
        is_optical = false;

      if (telescope.find("nro45") != std::string::npos)
      {
        is_optical = false;
        flux = "logistic";
      }

      if (telescope.find("chandra") != std::string::npos)
      {
        is_optical = false;
        is_xray = true;
      }
    }

    std::string line(hdrLine);

    if (line.find("ASTRO-F") != std::string::npos)
    {
      is_optical = true;
      flux = "logistic";
    }

    if (line.find("HSCPIPE") != std::string::npos)
    {
      is_optical = true;
      flux = "ratio";
    }

    if (strncmp(hdrLine, "FRAMEID = ", 10) == 0)
    {
      std::string frameid = hdr_get_string_value(hdrLine + 10);

      if (frameid.find("SUPM") != std::string::npos ||
          frameid.find("MCSM") != std::string::npos)
      {
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
          line.find("x-ray") != std::string::npos)
      {
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
    int va_count /*, boost::shared_ptr<shared_state> const& state*/)
{
  // state_ = state;

  int no_omp_threads = MAX(omp_get_max_threads() / va_count, 1);
  printf("downloading %s from %s, va_count = %d, no_omp_threads = %d\n",
         this->dataset_id.c_str(), url.c_str(), va_count, no_omp_threads);
}

void FITS::from_path(std::string path, bool is_compressed, std::string flux,
                     int va_count)
{
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

  if (is_compressed)
  {
    file = gzopen(path.c_str(), "r");

    if (!file)
    {
      printf("gzopen of '%s' failed: %s.\n", path.c_str(), strerror(errno));
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  }
  else
  {
    fd = open(path.c_str(), O_RDONLY);

    if (fd == -1)
    {
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

  if (this->fits_file_size < FITS_CHUNK_LENGTH)
  {
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

  while (naxis == 0)
  {
    bool end = false;

    while (!end)
    {
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

      if (bytes_read != FITS_CHUNK_LENGTH)
      {
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

  if (bitpix != -32)
  {
    printf("%s::unsupported bitpix(%d), FITS data will not be read.\n",
           dataset_id.c_str(), bitpix);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  if (width <= 0 || height <= 0 || depth <= 0)
  {
    printf("%s::incorrect dimensions (width:%ld, height:%ld, depth:%ld)\n",
           dataset_id.c_str(), width, height, depth);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  const size_t plane_size = width * height;
  const size_t frame_size = plane_size * abs(bitpix / 8);

  if (frame_size != plane_size * sizeof(float))
  {
    printf("%s::plane_size != frame_size, is the bitpix correct?\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  // do not use mmap
  if (!img_pixels)
    img_pixels = std::shared_ptr<Ipp32f>(ippsMalloc_32f_L(plane_size),
                                         [=](Ipp32f *ptr) {
                                           const size_t frame_size = plane_size * sizeof(Ipp32f);
                                           std::string filename = FITSCACHE + std::string("/") +
                                                                  boost::replace_all_copy(dataset_id, "/", "_") +
                                                                  std::string(".pixels");

                                           struct stat64 st;
                                           int stat = stat64(filename.c_str(), &st);

                                           bool save = false;

                                           if (stat == -1)
                                             save = true;
                                           else if (st.st_size != frame_size)
                                             save = true;

                                           if (save)
                                           {
                                             printf("[%s]::saving img_pixels.\n", dataset_id.c_str());

                                             FILE *fp = fopen(filename.c_str(), "wb");

                                             if (fp != NULL)
                                             {
                                               size_t no_written = fwrite(ptr, sizeof(Ipp32f), plane_size, fp);

                                               if (no_written != plane_size)
                                                 perror("error writing img_pixels.\n");

                                               fclose(fp);
                                             }
                                           }

                                           Ipp32fFree(ptr);
                                         });

  if (!img_mask)
    img_mask = std::shared_ptr<Ipp8u>(ippsMalloc_8u_L(plane_size),
                                      [=](Ipp8u *ptr) {
                                        std::string filename = FITSCACHE + std::string("/") +
                                                               boost::replace_all_copy(dataset_id, "/", "_") +
                                                               std::string(".mask");

                                        struct stat64 st;
                                        int stat = stat64(filename.c_str(), &st);

                                        bool save = false;

                                        if (stat == -1)
                                          save = true;
                                        else if (st.st_size != plane_size)
                                          save = true;

                                        if (save)
                                        {
                                          printf("[%s]::saving img_mask.\n", dataset_id.c_str());

                                          FILE *fp = fopen(filename.c_str(), "wb");

                                          if (fp != NULL)
                                          {
                                            size_t no_written = fwrite(ptr, 1, plane_size, fp);

                                            if (no_written != plane_size)
                                              perror("error writing img_mask.\n");

                                            fclose(fp);
                                          }
                                        }

                                        Ipp8uFree(ptr);
                                      });

  if (!img_pixels || !img_mask)
  {
    printf("%s::cannot allocate memory for a 2D image buffer (pixels+mask).\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  std::atomic<bool> bSuccess = true;

  float _pmin = FLT_MAX;
  float _pmax = -FLT_MAX;

  if (depth == 1)
  {
    // read/process the FITS plane (image) in parallel
    // unless this is a compressed file, in which case
    // the data can only be read sequentially

    // use ispc to process the plane
    // 1. endianness
    // 2. fill-in {pixels,mask}

    auto _img_pixels = img_pixels.get();
    auto _img_mask = img_mask.get();

    // get pmin, pmax
    int max_threads = omp_get_max_threads();

    // keep the worksize within int32 limits
    size_t max_work_size = 1024 * 1024 * 1024;
    size_t work_size = MIN(plane_size / max_threads, max_work_size);
    int num_threads = plane_size / work_size;

    printf("%s::fits2float32:\tsize = %zu, work_size = %zu, num_threads = %d\n",
           dataset_id.c_str(), plane_size, work_size, num_threads);

    if (is_compressed)
    {
      // load data into the buffer sequentially
      ssize_t bytes_read =
          gzread(this->compressed_fits_stream, _img_pixels, frame_size);

      if (bytes_read != frame_size)
      {
        fprintf(
            stderr,
            "%s::CRITICAL: read less than %zd bytes from the FITS data unit\n",
            dataset_id.c_str(), bytes_read);
        processed_data = true;
        data_cv.notify_all();
        return;
      }
      else
        printf("%s::FITS data read OK.\n", dataset_id.c_str());

#pragma omp parallel for schedule(static) num_threads(no_omp_threads) \
    reduction(min                                                     \
              : _pmin) reduction(max                                  \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++)
      {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        ispc::fits2float32((int32_t *)&(_img_pixels[start]),
                           (uint8_t *)&(_img_mask[start]), bzero, bscale,
                           ignrval, datamin, datamax, _pmin, _pmax, work_size);
      };
    }
    else
    {
      // load data into the buffer in parallel chunks
      // the data part starts at <offset>

      auto _img_pixels = img_pixels.get();
      auto _img_mask = img_mask.get();

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads) \
    reduction(min                                                      \
              : _pmin) reduction(max                                   \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++)
      {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        // parallel read (pread) at a specified offset
        ssize_t bytes_read =
            pread(this->fits_file_desc, &(_img_pixels[start]),
                  work_size * sizeof(float), offset + start * sizeof(float));

        if (bytes_read != work_size * sizeof(float))
        {
          fprintf(stderr,
                  "%s::CRITICAL: only read %zd out of requested %zd bytes.\n",
                  dataset_id.c_str(), bytes_read, (work_size * sizeof(float)));
          bSuccess = false;
        }
        else
          ispc::fits2float32((int32_t *)&(_img_pixels[start]),
                             (uint8_t *)&(_img_mask[start]), bzero, bscale,
                             ignrval, datamin, datamax, _pmin, _pmax,
                             work_size);
      };
    }

    dmin = _pmin;
    dmax = _pmax;
  }
  else
  {
    printf("%s::depth > 1: work-in-progress.\n", dataset_id.c_str());
    // init the variables
    frame_min.resize(depth, FLT_MAX);
    frame_max.resize(depth, -FLT_MAX);
    mean_spectrum.resize(depth, 0.0f);
    integrated_spectrum.resize(depth, 0.0f);

    auto _img_pixels = img_pixels.get();
    auto _img_mask = img_mask.get();

    // prepare the main image/mask
    memset(_img_mask, 0, plane_size);
    for (size_t i = 0; i < plane_size; i++)
      _img_pixels[i] = 0.0f;

    int max_threads = omp_get_max_threads();

    if (!is_compressed)
    {
      // pre-allocated floating-point read buffers
      // to reduce RAM thrashing
      std::vector<Ipp32f *> pixels_buf(max_threads);
      std::vector<Ipp8u *> mask_buf(max_threads);

      // OpenMP per-thread {pixels,mask}
      std::vector<Ipp32f *> omp_pixels(max_threads);
      std::vector<Ipp8u *> omp_mask(max_threads);

      for (int i = 0; i < max_threads; i++)
      {
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

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads) \
    reduction(min                                                      \
              : _pmin) reduction(max                                   \
                                 : _pmax)

      for (size_t frame = 0; frame < depth; frame++)
      {
        int tid = omp_get_thread_num();
        // printf("tid: %d, k: %zu\n", tid, k);
        if (pixels_buf[tid] == NULL || mask_buf[tid] == NULL ||
            omp_pixels[tid] == NULL || omp_mask[tid] == NULL)
        {
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

        if (bytes_read != frame_size)
        {
          fprintf(stderr,
                  "%s::<tid::%d>::CRITICAL: only read %zd out of requested "
                  "%zd bytes.\n",
                  dataset_id.c_str(), tid, bytes_read, frame_size);
          bSuccess = false;
        }
        else
        {
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

      for (int i = 0; i < max_threads; i++)
      {
        float *pixels_tid = omp_pixels[i];
        unsigned char *mask_tid = omp_mask[i];

#pragma omp parallel for num_threads(no_omp_threads)
        for (int tid = 0; tid < num_threads; tid++)
        {
          size_t work_size = plane_size / num_threads;
          size_t start = tid * work_size;

          if (tid == num_threads - 1)
            work_size = plane_size - start;

          ispc::join_pixels_masks(&(_img_pixels[start]), &(pixels_tid[start]),
                                  &(_img_mask[start]), &(mask_tid[start]),
                                  work_size);
        }
      }

      // release memory
      for (int i = 0; i < max_threads; i++)
      {
        if (pixels_buf[i] != NULL)
          ippsFree(pixels_buf[i]);

        if (mask_buf[i] != NULL)
          ippsFree(mask_buf[i]);

        if (omp_pixels[i] != NULL)
          ippsFree(omp_pixels[i]);

        if (omp_mask[i] != NULL)
          ippsFree(omp_mask[i]);
      }
    }
    else
    {
      printf("%s::gz-compressed depth > 1: work-in-progress.\n",
             dataset_id.c_str());

#pragma omp parallel num_threads(no_omp_threads)
      {
#pragma omp single
        {
          for (size_t frame = 0; frame < depth; frame++)
          {
            // allocate {pixel_buf, mask_buf}
            std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
                                               Ipp32fFree);
            std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size),
                                            Ipp8uFree);

            if (pixels_buf.get() == NULL || mask_buf.get() == NULL)
            {
              printf("%s::CRITICAL::cannot malloc memory for {pixels,mask} "
                     "buffers.\n",
                     dataset_id.c_str());
              bSuccess = false;
              break;
            }

            // load data into the buffer sequentially
            ssize_t bytes_read = gzread(this->compressed_fits_stream,
                                        pixels_buf.get(), frame_size);

            if (bytes_read != frame_size)
            {
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
                ignrval, datamin, datamax, _cdelt3, img_pixels.get(),
                img_mask.get(), fmin, fmax, mean, integrated, plane_size);

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

  if (bSuccess)
  {
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

    /*make_image_luma();
    make_exr_image();*/
  }
  else
  {
    this->has_error = true;
  }

  this->has_data = bSuccess ? true : false;
  this->processed_data = true;
  this->data_cv.notify_all();
  this->timestamp = std::time(nullptr);
}

void FITS::from_path_mmap(std::string path, bool is_compressed,
                          std::string flux, int va_count)
{
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

  if (is_compressed)
  {
    file = gzopen(path.c_str(), "r");

    if (!file)
    {
      printf("gzopen of '%s' failed: %s.\n", path.c_str(), strerror(errno));
      processed_header = true;
      header_cv.notify_all();
      processed_data = true;
      data_cv.notify_all();
      return;
    }
  }
  else
  {
    fd = open(path.c_str(), O_RDONLY);

    if (fd == -1)
    {
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

  if (this->fits_file_size < FITS_CHUNK_LENGTH)
  {
    printf("error: FITS file size smaller than %d bytes.", FITS_CHUNK_LENGTH);
    processed_header = true;
    header_cv.notify_all();
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  // mmap the FITS file
  if (this->fits_file_desc != -1)
  {
    this->fits_ptr = std::shared_ptr<void>(
        mmap(nullptr, this->fits_file_size, PROT_READ,
             MAP_PRIVATE /*| MAP_HUGETLB*/, this->fits_file_desc, 0),
        [=](void *ptr) {
          if (ptr != MAP_FAILED)
            munmap(ptr, fits_file_size);
        });

    if (this->fits_ptr.get() == MAP_FAILED)
    {
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

  while (naxis == 0)
  {
    bool end = false;

    while (!end)
    {
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

      if (bytes_read != FITS_CHUNK_LENGTH)
      {
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

  if (bitpix != -32)
  {
    printf("%s::unsupported bitpix(%d), FITS data will not be read.\n",
           dataset_id.c_str(), bitpix);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  if (width <= 0 || height <= 0 || depth <= 0)
  {
    printf("%s::incorrect dimensions (width:%ld, height:%ld, depth:%ld)\n",
           dataset_id.c_str(), width, height, depth);
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  const size_t plane_size = width * height;
  const size_t frame_size = plane_size * abs(bitpix / 8);

  if (frame_size != plane_size * sizeof(float))
  {
    printf("%s::plane_size != frame_size, is the bitpix correct?\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  // do not use mmap
  if (!img_pixels)
    img_pixels = std::shared_ptr<Ipp32f>(ippsMalloc_32f_L(plane_size),
                                         [=](Ipp32f *ptr) {
                                           const size_t frame_size = plane_size * sizeof(Ipp32f);
                                           std::string filename = FITSCACHE + std::string("/") +
                                                                  boost::replace_all_copy(dataset_id, "/", "_") +
                                                                  std::string(".pixels");

                                           struct stat64 st;
                                           int stat = stat64(filename.c_str(), &st);

                                           bool save = false;

                                           if (stat == -1)
                                             save = true;
                                           else if (st.st_size != frame_size)
                                             save = true;

                                           if (save)
                                           {
                                             printf("[%s]::saving img_pixels.\n", dataset_id.c_str());

                                             FILE *fp = fopen(filename.c_str(), "wb");

                                             if (fp != NULL)
                                             {
                                               size_t no_written = fwrite(ptr, sizeof(Ipp32f), plane_size, fp);

                                               if (no_written != plane_size)
                                                 perror("error writing img_pixels.\n");

                                               fclose(fp);
                                             }
                                           }

                                           Ipp32fFree(ptr);
                                         });

  if (!img_mask)
    img_mask = std::shared_ptr<Ipp8u>(ippsMalloc_8u_L(plane_size),
                                      [=](Ipp8u *ptr) {
                                        std::string filename = FITSCACHE + std::string("/") +
                                                               boost::replace_all_copy(dataset_id, "/", "_") +
                                                               std::string(".mask");

                                        struct stat64 st;
                                        int stat = stat64(filename.c_str(), &st);

                                        bool save = false;

                                        if (stat == -1)
                                          save = true;
                                        else if (st.st_size != plane_size)
                                          save = true;

                                        if (save)
                                        {
                                          printf("[%s]::saving img_mask.\n", dataset_id.c_str());

                                          FILE *fp = fopen(filename.c_str(), "wb");

                                          if (fp != NULL)
                                          {
                                            size_t no_written = fwrite(ptr, 1, plane_size, fp);

                                            if (no_written != plane_size)
                                              perror("error writing img_mask.\n");

                                            fclose(fp);
                                          }
                                        }

                                        Ipp8uFree(ptr);
                                      });

  if (!img_pixels || !img_mask)
  {
    printf("%s::cannot allocate memory for a 2D image buffer (pixels+mask).\n",
           dataset_id.c_str());
    processed_data = true;
    data_cv.notify_all();
    return;
  }

  std::atomic<bool> bSuccess = true;

  float _pmin = FLT_MAX;
  float _pmax = -FLT_MAX;

  if (depth == 1)
  {
    // read/process the FITS plane (image) in parallel
    // unless this is a compressed file, in which case
    // the data can only be read sequentially

    // use ispc to process the plane
    // 1. endianness
    // 2. fill-in {pixels,mask}

    auto _img_pixels = img_pixels.get();
    auto _img_mask = img_mask.get();

    // get pmin, pmax
    int max_threads = omp_get_max_threads();

    // keep the worksize within int32 limits
    size_t max_work_size = 1024 * 1024 * 1024;
    size_t work_size = MIN(plane_size / max_threads, max_work_size);
    int num_threads = plane_size / work_size;

    printf("%s::fits2float32:\tsize = %zu, work_size = %zu, num_threads = %d\n",
           dataset_id.c_str(), plane_size, work_size, num_threads);

    if (is_compressed)
    {
      // load data into the buffer sequentially
      ssize_t bytes_read =
          gzread(this->compressed_fits_stream, _img_pixels, frame_size);

      if (bytes_read != frame_size)
      {
        fprintf(
            stderr,
            "%s::CRITICAL: read less than %zd bytes from the FITS data unit\n",
            dataset_id.c_str(), bytes_read);
        processed_data = true;
        data_cv.notify_all();
        return;
      }
      else
        printf("%s::FITS data read OK.\n", dataset_id.c_str());

#pragma omp parallel for schedule(static) num_threads(no_omp_threads) \
    reduction(min                                                     \
              : _pmin) reduction(max                                  \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++)
      {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        ispc::fits2float32((int32_t *)&(_img_pixels[start]),
                           (uint8_t *)&(_img_mask[start]), bzero, bscale,
                           ignrval, datamin, datamax, _pmin, _pmax, work_size);
      };
    }
    else
    {
      // load data into the buffer in parallel chunks
      // the data part starts at <offset>

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads) \
    reduction(min                                                      \
              : _pmin) reduction(max                                   \
                                 : _pmax)
      for (int tid = 0; tid < num_threads; tid++)
      {
        size_t work_size = plane_size / num_threads;
        size_t start = tid * work_size;

        if (tid == num_threads - 1)
          work_size = plane_size - start;

        // parallel read (pread) at a specified offset
        ssize_t bytes_read =
            pread(this->fits_file_desc, &(_img_pixels[start]),
                  work_size * sizeof(float), offset + start * sizeof(float));

        if (bytes_read != work_size * sizeof(float))
        {
          fprintf(stderr,
                  "%s::CRITICAL: only read %zd out of requested %zd bytes.\n",
                  dataset_id.c_str(), bytes_read, (work_size * sizeof(float)));
          bSuccess = false;
        }
        else
          ispc::fits2float32((int32_t *)&(_img_pixels[start]),
                             (uint8_t *)&(_img_mask[start]), bzero, bscale,
                             ignrval, datamin, datamax, _pmin, _pmax,
                             work_size);
      };
    }

    dmin = _pmin;
    dmax = _pmax;
  }
  else
  {
    printf("%s::depth > 1: work-in-progress.\n", dataset_id.c_str());
    // init the variables
    frame_min.resize(depth, FLT_MAX);
    frame_max.resize(depth, -FLT_MAX);
    mean_spectrum.resize(depth, 0.0f);
    integrated_spectrum.resize(depth, 0.0f);

    // prepare the cache directory
    {
      std::string filename = FITSCACHE + std::string("/") +
                             boost::replace_all_copy(dataset_id, "/", "_") +
                             std::string(".zfp");

      // create a directory on a best-effort basis, ignoring any errors
      if (mkdir(filename.c_str(), 0777) != 0)
        perror("(non-critical) cannot create a new pixels cache directory");

      filename = FITSCACHE + std::string("/") +
                 boost::replace_all_copy(dataset_id, "/", "_") +
                 std::string(".lz4");

      // create a directory on a best-effort basis, ignoring any errors
      if (mkdir(filename.c_str(), 0777) != 0)
        perror("(non-critical) cannot create a new mask cache directory");
    }

    // reset the cube just in case
    fits_cube.clear();

    // resize/init the cube with default nullptr-filled std::shared_ptr
    fits_cube.resize(depth);

    // init the compressed regions (sizing: err on the side of caution)
    // cannot resize a vector of atomics in C++ ...
    cube_pixels = std::vector<std::atomic<compressed_blocks *>>(depth / 4 + 4);
    cube_mask = std::vector<std::atomic<compressed_blocks *>>(depth);

    cache_mtx = std::vector<std::shared_mutex>(depth / 4 + 4);
    cache = std::vector<decompressed_blocks>(depth);

    std::cout << "cube_pixels::size = " << cube_pixels.size()
              << ", cube_mask::size = " << cube_mask.size()
              << ", cache::size = " << cache.size()
              << ", cache_mtx::size = " << cache_mtx.size() << std::endl;

    // set up a cache purging thread
    purge_thread = std::thread([this]() {
      std::unique_lock<std::mutex> purge_lck(purge_mtx);

      while (!terminate)
      {
        purge_cache();

        purge_cv.wait_for(purge_lck, 10s);
      }

      printf("%s::purge_cache() thread terminated.\n", dataset_id.c_str());
    });

    // lower its priority
#if defined(__APPLE__) && defined(__MACH__)
    struct sched_param param;
    param.sched_priority = 0;
    if (pthread_setschedparam(purge_thread.native_handle(), SCHED_OTHER,
                              &param) != 0)
      perror("pthread_setschedparam");
    else
      printf("successfully lowered the cache purge thread priority to "
             "SCHED_OTHER.\n");
#else
    struct sched_param param;
    param.sched_priority = 0;
    if (pthread_setschedparam(purge_thread.native_handle(), SCHED_IDLE, &param) !=
        0)
      perror("pthread_setschedparam");
    else
      printf("successfully lowered the cache purge thread priority to "
             "SCHED_IDLE.\n");
#endif

    auto _img_pixels = img_pixels.get();
    auto _img_mask = img_mask.get();

    // prepare the main image/mask
    memset(_img_mask, 0, plane_size);
    for (size_t i = 0; i < plane_size; i++)
      _img_pixels[i] = 0.0f;

    int max_threads = omp_get_max_threads();

    terminate_compression = false;

    for (int i = 0; i < max_threads; i++)
    {
      // std::shared_ptr<zfp_pool_thread> a_thread(new zfp_pool_thread());

      std::thread a_thread =
          std::thread(&FITS::zfp_compression_thread, this, i);

#if defined(__APPLE__) && defined(__MACH__)
      struct sched_param param;
      param.sched_priority = 0;
      if (pthread_setschedparam(a_thread.native_handle(), SCHED_OTHER,
                                &param) != 0)
        perror("pthread_setschedparam");
      else
        printf("successfully lowered the zfp_compress thread priority to "
               "SCHED_OTHER.\n");
#else
      struct sched_param param;
      param.sched_priority = 0;
      if (pthread_setschedparam(a_thread.native_handle(), SCHED_IDLE, &param) !=
          0)
        perror("pthread_setschedparam");
      else
        printf("successfully lowered the zfp_compress thread priority to "
               "SCHED_IDLE.\n");
#endif

      zfp_pool.push_back(std::move(a_thread));
    }

    if (!is_compressed)
    {
      // pre-allocated floating-point read buffers
      // to reduce RAM thrashing
      std::vector<Ipp8u *> mask_buf(max_threads);

      // OpenMP per-thread {pixels,mask}
      std::vector<Ipp32f *> omp_pixels(max_threads);
      std::vector<Ipp8u *> omp_mask(max_threads);

      for (int i = 0; i < max_threads; i++)
      {
        mask_buf[i] = ippsMalloc_8u_L(plane_size);

        omp_pixels[i] = ippsMalloc_32f_L(plane_size);
        if (omp_pixels[i] != NULL)
          for (size_t j = 0; j < plane_size; j++)
            omp_pixels[i][j] = 0.0f;

        omp_mask[i] = ippsMalloc_8u_L(plane_size);
        if (omp_mask[i] != NULL)
          memset(omp_mask[i], 0, plane_size);
      }

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads) \
    reduction(min                                                      \
              : _pmin) reduction(max                                   \
                                 : _pmax)
      for (size_t k = 0; k < depth; k += 4)
      {
        size_t start_k = k;
        size_t end_k = MIN(k + 4, depth);

        for (size_t frame = start_k; frame < end_k; frame++)
        {
          // for (size_t frame = 0; frame < depth; frame++) {
          int tid = omp_get_thread_num();
          // printf("tid: %d, k: %zu\n", tid, k);
          if (mask_buf[tid] == NULL || omp_pixels[tid] == NULL ||
              omp_mask[tid] == NULL)
          {
            fprintf(
                stderr,
                "%s::<tid::%d>::problem allocating thread-local {pixels,buf} "
                "arrays.\n",
                dataset_id.c_str(), tid);
            bSuccess = false;
            continue;
          }

          Ipp32f *pixels_buf = nullptr;

          // point the cube element to an mmaped region
          if (this->fits_ptr && this->fits_ptr.get() != MAP_FAILED)
          {
            char *ptr = (char *)this->fits_ptr.get();
            ptr += this->hdr_len + frame_size * frame;

            fits_cube[frame] = std::shared_ptr<void>(ptr, [=](void *ptr) {
              if (ptr != NULL)
                madvise(ptr, frame_size, MADV_DONTNEED);
            });

            pixels_buf = (Ipp32f *)fits_cube[frame].get();
          }

          if (pixels_buf == nullptr)
          {
            fprintf(stderr, "%s::<tid::%d>::CRITICAL: pixels_buf is nullptr.\n",
                    dataset_id.c_str(), tid);
            bSuccess = false;
          }
          else
          {
            float fmin = FLT_MAX;
            float fmax = -FLT_MAX;
            float mean = 0.0f;
            float integrated = 0.0f;

            float _cdelt3 =
                this->has_velocity
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

        // append <start_k> to a ZFP compression queue
        zfp_queue.push(start_k);
      }

      // join omp_{pixel,mask}

      // keep the worksize within int32 limits
      size_t max_work_size = 1024 * 1024 * 1024;
      size_t work_size = MIN(plane_size / max_threads, max_work_size);
      int num_threads = plane_size / work_size;

      for (int i = 0; i < max_threads; i++)
      {
        float *pixels_tid = omp_pixels[i];
        unsigned char *mask_tid = omp_mask[i];

#pragma omp parallel for num_threads(no_omp_threads)
        for (int tid = 0; tid < num_threads; tid++)
        {
          size_t work_size = plane_size / num_threads;
          size_t start = tid * work_size;

          if (tid == num_threads - 1)
            work_size = plane_size - start;

          ispc::join_pixels_masks(&(_img_pixels[start]), &(pixels_tid[start]),
                                  &(_img_mask[start]), &(mask_tid[start]),
                                  work_size);
        }
      }

      // release memory
      for (int i = 0; i < max_threads; i++)
      {
        if (mask_buf[i] != NULL)
          ippsFree(mask_buf[i]);

        if (omp_pixels[i] != NULL)
          ippsFree(omp_pixels[i]);

        if (omp_mask[i] != NULL)
          ippsFree(omp_mask[i]);
      }

      /*compress_thread = std::thread(&FITS::zfp_compress, this);

      struct sched_param param;
      param.sched_priority = 0;
      if (pthread_setschedparam(compress_thread.native_handle(), SCHED_IDLE,
                                &param) != 0)
        perror("pthread_setschedparam");
      else
        printf("successfully lowered the zfp_compress thread priority to "
               "SCHED_IDLE.\n");*/
    }
    else
    {
      printf("%s::gz-compressed depth > 1: work-in-progress.\n",
             dataset_id.c_str());

      // allocate {pixel_buf, mask_buf}
      /*std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
                                         Ipp32fFree);*/
      std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size), Ipp8uFree);

      if (mask_buf.get() == NULL)
      {
        printf("%s::CRITICAL::cannot malloc memory for {pixels,mask} "
               "buffers.\n",
               dataset_id.c_str());
        bSuccess = false;
      }
      else
        // ZFP requires blocks-of-4 processing
        for (size_t k = 0; k < depth; k += 4)
        {
          size_t start_k = k;
          size_t end_k = MIN(k + 4, depth);

          for (size_t frame = start_k; frame < end_k; frame++)
          {
            fits_cube[frame] = std::shared_ptr<void>(ippsMalloc_32f_L(plane_size), [=](void *ptr) {
              if (ptr != NULL)
                Ipp32fFree((Ipp32f *)ptr);
            });

            Ipp32f *pixels_buf = (Ipp32f *)fits_cube[frame].get();

            // load data into the buffer sequentially
            ssize_t bytes_read = 0;
            if (pixels_buf != NULL)
              bytes_read =
                  gzread(this->compressed_fits_stream, pixels_buf, frame_size);

            if (bytes_read != frame_size)
            {
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

            ispc::make_image_spectrumF32_ro(
                (int32_t *)pixels_buf, mask_buf.get(), bzero, bscale, ignrval,
                datamin, datamax, _cdelt3, img_pixels.get(), img_mask.get(),
                fmin, fmax, mean, integrated, plane_size);

            _pmin = MIN(_pmin, fmin);
            _pmax = MAX(_pmax, fmax);
            frame_min[frame] = fmin;
            frame_max[frame] = fmax;
            mean_spectrum[frame] = mean;
            integrated_spectrum[frame] = integrated;

            send_progress_notification(frame, depth);
          }

          // append <start_k> to a ZFP compression queue
          zfp_queue.push(start_k);
        }
    }

    dmin = _pmin;
    dmax = _pmax;

    /*printf("FMIN/FMAX\tSPECTRUM\n");
      for (int i = 0; i < depth; i++)
      printf("%d (%f):(%f)\t\t(%f):(%f)\n", i, frame_min[i], frame_max[i],
      mean_spectrum[i], integrated_spectrum[i]); printf("\n");*/
  }

  // send a termination signal to the ZFP compression pool
  terminate_compression = true;

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("%s::<data:%s>\tdmin = %f\tdmax = %f\telapsed time: %5.2f [ms]\n",
         dataset_id.c_str(), (bSuccess ? "true" : "false"), dmin, dmax,
         elapsedMilliseconds);

  if (bSuccess)
  {
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

    /*make_image_luma();
    make_exr_image();*/

    auto _img_pixels = img_pixels.get();
    auto _img_mask = img_mask.get();

// replace NaNs with 0.0
#pragma omp parallel for simd
    for (size_t i = 0; i < plane_size; i++)
      if (_img_mask[i] == 0)
        _img_pixels[i] = 0.0f;
  }
  else
  {
    this->has_error = true;
  }

  this->has_data = bSuccess ? true : false;
  this->processed_data = true;
  this->data_cv.notify_all();
  this->timestamp = std::time(nullptr);
}

void FITS::make_exr_image()
{
  auto _img_pixels = img_pixels.get();
  auto _img_mask = img_mask.get();

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

  if (mask == NULL)
  {
    printf("%s::cannot malloc memory for a UNIT mask buffer.\n",
           dataset_id.c_str());
    return;
  }

#pragma omp parallel for simd
  for (size_t i = 0; i < total_size; i++)
    mask[i] = _img_mask[i];

  // export EXR in a YA format
  std::string filename = FITSCACHE + std::string("/") +
                         boost::replace_all_copy(dataset_id, "/", "_") +
                         std::string(".exr");
  try
  {
    Header header(width, height);
    header.compression() = DWAB_COMPRESSION;
    header.channels().insert("Y", Channel(FLOAT));
    header.channels().insert("A", Channel(UINT));

    OutputFile file(filename.c_str(), header);
    FrameBuffer frameBuffer;

    frameBuffer.insert("Y", Slice(FLOAT, (char *)_img_pixels,
                                  sizeof(Ipp32f) * 1, sizeof(Ipp32f) * width));

    frameBuffer.insert("A", Slice(UINT, (char *)mask, sizeof(Ipp16u) * 1,
                                  sizeof(Ipp16u) * width));

    file.setFrameBuffer(frameBuffer);
    file.writePixels(height);
  }
  catch (const std::exception &exc)
  {
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

void FITS::make_image_luma()
{
  auto _img_pixels = img_pixels.get();
  auto _img_mask = img_mask.get();

  auto start_t = steady_clock::now();

  int max_threads = omp_get_max_threads();

  // keep the worksize within int32 limits
  size_t total_size = width * height;
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  Ipp8u *img_luma = ippsMalloc_8u_L(total_size);

  if (img_luma == NULL)
  {
    printf("%s::cannot malloc memory for a 2D image luma buffer.\n",
           dataset_id.c_str());
    return;
  }

  memset(img_luma, 0, total_size);

#pragma omp parallel for
  for (int tid = 0; tid < num_threads; tid++)
  {
    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    // switch to a different luma based on the flux
    if (this->flux == "linear")
    {
      float slope = 1.0f / (this->white - this->black);
      ispc::image_to_luminance_f32_linear(&(_img_pixels[start]),
                                          &(_img_mask[start]), this->black,
                                          slope, &(img_luma[start]), work_size);
    }

    if (this->flux == "logistic")
      ispc::image_to_luminance_f32_logistic(
          &(_img_pixels[start]), &(_img_mask[start]), this->median,
          this->sensitivity, &(img_luma[start]), work_size);

    if (this->flux == "ratio")
      ispc::image_to_luminance_f32_ratio(
          &(_img_pixels[start]), &(_img_mask[start]), this->black,
          this->sensitivity, &(img_luma[start]), work_size);

    if (this->flux == "square")
      ispc::image_to_luminance_f32_square(
          &(_img_pixels[start]), &(_img_mask[start]), this->black,
          this->sensitivity, &(img_luma[start]), work_size);

    if (this->flux == "legacy")
      ispc::image_to_luminance_f32_logarithmic(
          &(_img_pixels[start]), &(_img_mask[start]), this->min, this->max,
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

void FITS::make_image_statistics()
{
  auto _img_pixels = img_pixels.get();
  auto _img_mask = img_mask.get();

  int max_threads = omp_get_max_threads();

  // keep the worksize within int32 limits
  size_t total_size = width * height;
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  float _pmin = FLT_MAX;
  float _pmax = -FLT_MAX;

  if (this->depth == 1)
  {
    _pmin = dmin;
    _pmax = dmax;
  }
  else
  {
    float _cdelt3 = this->has_velocity
                        ? this->cdelt3 * this->frame_multiplier / 1000.0f
                        : 1.0f;

    // use pixels/mask to get min/max
#pragma omp parallel for reduction(min                    \
                                   : _pmin) reduction(max \
                                                      : _pmax)
    for (int tid = 0; tid < num_threads; tid++)
    {
      size_t work_size = total_size / num_threads;
      size_t start = tid * work_size;

      if (tid == num_threads - 1)
        work_size = total_size - start;

      // it also restores NaNs in the pixels array based on the mask
      ispc::image_min_max(&(_img_pixels[start]), &(_img_mask[start]), _cdelt3,
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
  ippiCopy_32f_C1R(_img_pixels, width * sizeof(Ipp32f), v.data(),
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

#pragma omp parallel for reduction(+                                                                                                  \
                                   : _mad) reduction(+                                                                                \
                                                     : _count) reduction(+                                                            \
                                                                         : _madP) reduction(+                                         \
                                                                                            : _countP) reduction(+                    \
                                                                                                                 : _madN) reduction(+ \
                                                                                                                                    : _countN)
  for (int tid = 0; tid < num_threads; tid++)
  {
    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    ispc::asymmetric_mad(&(_img_pixels[start]), &(_img_mask[start]), work_size,
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

  if (this->is_optical)
  {
    // SubaruWebQL-style
    float u = 0.5f;
    float v = 15.0f;
    _black = MAX(_pmin, median - u * _madN);
    _white = MIN(_pmax, median + u * _madP);
    _sensitivity = 1.0f / (v * _mad);
    _ratio_sensitivity = _sensitivity;
    auto_brightness(_img_pixels, _img_mask, _black, _ratio_sensitivity);
  }

  if (this->flux == "")
  {
    long cdf[NBINS];
    float Slot[NBINS];

    long total = hist[0];
    cdf[0] = hist[0];

    for (int i = 1; i < NBINS; i++)
    {
      cdf[i] = cdf[i - 1] + hist[i];
      total += hist[i];
    };

    for (int i = 0; i < NBINS; i++)
    {
      Slot[i] = (float)cdf[i] / (float)total;
    };

    int tone_mapping_class = histogram_classifier(Slot);

    switch (tone_mapping_class)
    {
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
                    float pmin, float pmax)
{
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
  for (int tid = 0; tid < num_threads; tid++)
  {
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

inline const char *FITS::check_null(const char *str)
{
  if (str != nullptr)
    return str;
  else
    return "\"\"";
};

void FITS::to_json(std::ostringstream &json)
{
  if (header == NULL || hdr_len == 0)
    return;

  /*Ipp8u *header_lz4 = NULL;
  int compressed_size = 0;

  // LZ4-compress the FITS header
  int worst_size = LZ4_compressBound(hdr_len);

  header_lz4 = ippsMalloc_8u_L(worst_size);

  if (header_lz4 == NULL)
    return;

  // compress the header with LZ4
  compressed_size = LZ4_compress_HC((const char *)header, (char *)header_lz4,
                                    hdr_len, worst_size, LZ4HC_CLEVEL_MAX);
  printf("FITS HEADER size %zu bytes, LZ4-compressed: %d bytes.\n", hdr_len,
         compressed_size);

  char *encoded_header = NULL;
  char *fits_header =
      base64((const unsigned char *)header_lz4, compressed_size);

  ippsFree(header_lz4);

  if (fits_header != NULL)
  {
    encoded_header = json_encode_string(fits_header);
    free(fits_header);
  };*/

  json << "{";

  // header
  char *encoded_header = json_encode_string(header);
  // json << "\"HEADERSIZE\" : " << hdr_len << ",";
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
  if (mean_spectrum.size() > 0)
  {
    json << "\"mean_spectrum\" : [";

    for (size_t i = 0; i < depth - 1; i++)
      json << std::scientific << mean_spectrum[i] << ",";

    json << std::scientific << mean_spectrum[depth - 1] << "],";
  }
  else
    json << "\"mean_spectrum\" : [],";

  // integrated spectrum
  if (integrated_spectrum.size() > 0)
  {
    json << "\"integrated_spectrum\" : [";

    for (size_t i = 0; i < depth - 1; i++)
      json << std::scientific << integrated_spectrum[i] << ",";

    json << std::scientific << integrated_spectrum[depth - 1] << "],";
  }
  else
    json << "\"integrated_spectrum\" : [],";

  // statistics
  /*json << "\"min\" : " << std::scientific << min << ",";
  json << "\"max\" : " << std::scientific << max << ",";
  json << "\"median\" : " << std::scientific << median << ",";
  json << "\"sensitivity\" : " << std::scientific << sensitivity << ",";
  json << "\"ratio_sensitivity\" : " << std::scientific << ratio_sensitivity
       << ",";
  json << "\"black\" : " << std::scientific << black << ",";
  json << "\"white\" : " << std::scientific << white << ",";
  json << "\"flux\" : \"" << flux << "\",";*/

  // histogram
  json << "\"histogram\" : [";
  for (size_t i = 0; i < NBINS - 1; i++)
    json << hist[i] << ",";
  json << hist[NBINS - 1] << "]}";
}

void FITS::auto_brightness(Ipp32f *_pixels, Ipp8u *_mask, float _black,
                           float &_ratio_sensitivity)
{
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

  do
  {
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
                                 float _sensitivity)
{
  int max_threads = omp_get_max_threads();
  size_t total_size = width * height;
  size_t max_work_size = 1024 * 1024 * 1024;
  size_t work_size = MIN(total_size / max_threads, max_work_size);
  int num_threads = total_size / work_size;

  float brightness = 0.0f;

#pragma omp parallel for reduction(+ \
                                   : brightness)
  for (int tid = 0; tid < num_threads; tid++)
  {
    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    brightness = ispc::pixels_mean_brightness_ratio(
        &(_pixels[start]), &(_mask[start]), _black, _sensitivity, work_size);
  };

  return brightness / float(num_threads);
}

void FITS::send_progress_notification(size_t running, size_t total)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  double elapsed;
  elapsed = (now.tv_sec - this->created.tv_sec) * 1e9;
  elapsed = (elapsed + (now.tv_nsec - this->created.tv_nsec)) * 1e-9;

  // when using libnghttp2_asio
  {
    std::lock_guard<std::shared_mutex> guard(progress_mtx);
    this->progress.running = running;
    this->progress.total = total;
    this->progress.elapsed = elapsed;
  }

  std::ostringstream json;
  json << "{"
       << "\"type\" : \"progress\",";
  json << "\"message\" : \"loading FITS\",";
  json << "\"total\" : " << total << ",";
  json << "\"running\" : " << running << ",";
  json << "\"elapsed\" : " << elapsed << "}";

  std::shared_lock<std::shared_mutex> lock(m_progress_mutex);
  TWebSocketList connections = m_progress[this->dataset_id];

  for (auto it = connections.begin(); it != connections.end(); ++it)
  {
    TWebSocket *ws = *it;

    struct UserData *user = (struct UserData *)ws->getUserData();

    if (user != NULL)
    {
      if (check_progress_timeout(user->ptr, system_clock::now()) ||
          (running == total))
      {
        // std::cout << json.str() << std::endl;
        ws->send(json.str(), uWS::OpCode::TEXT);
        update_session_timestamp(user->ptr);
      }
    }
  };
}

std::shared_ptr<unsigned short>
FITS::request_cached_region_ptr(int frame, int idy, int idx)
{
  int pixels_idz = frame / 4;
  int sub_frame = frame % 4; // a sub-pixels frame count in [0,4)
  int mask_idz = frame;

  std::shared_ptr<unsigned short> res;
  std::shared_ptr<CacheEntry> entry;

  {
    // lock the cache (a shared read lock)
    std::shared_lock<std::shared_mutex> reader(cache_mtx[pixels_idz]);

    // auto z_entry = cache[frame];
    decompressed_blocks &z_entry = cache[frame];

    // check the y-axis
    if (z_entry.find(idy) != z_entry.end())
    {
      // check the x-axis
      auto &y_entry = z_entry[idy];
      if (y_entry.find(idx) != y_entry.end())
        entry = y_entry[idx];
    }

    // the shared lock gets released here
  }

  if (entry)
  {
    struct CacheEntry *_entry = entry.get();
    _entry->timestamp = std::time(nullptr);

    if (_entry->data)
    {
      // zero-copy transfer (return the shared pointer)
      return _entry->data;
    }
    else
      return res;
  }

  // decompress the pixels and a mask
  size_t region_size = ZFP_CACHE_REGION * ZFP_CACHE_REGION;
  size_t mask_size = region_size * sizeof(Ipp8u);
  bool ok;

#if defined(__APPLE__) && defined(__MACH__)
  Ipp32f *_pixels[4];
  Ipp8u *_mask;

  ok = true;

  // pixels
  for (int i = 0; i < 4; i++)
  {
    _pixels[i] = ippsMalloc_32f_L(region_size);
    if (_pixels[i] == NULL)
      ok = false;
    else
      for (size_t j = 0; j < region_size; j++)
        _pixels[i][j] = 0.0f;
  }

  // the mask
  _mask = ippsMalloc_8u_L(region_size);
  if (_mask == NULL)
    ok = false;
  else
    memset(_mask, 0, region_size);

  if (!ok)
  {
    for (int i = 0; i < 4; i++)
    {
      if (_pixels[i] != NULL)
        ippsFree(_pixels[i]);
    }

    if (_mask != NULL)
      ippsFree(_mask);

    return res;
  }
#else
  Ipp32f _pixels[4][region_size];
  Ipp8u _mask[region_size];
#endif

  // first the pixels (four frames)
  {
    auto pixel_blocks = cube_pixels[pixels_idz].load();
    Ipp8u *buffer = (*pixel_blocks)[idy][idx].get();
    int pComprLen = *((int *)buffer);

    int decStateSize;
    IppDecodeZfpState_32f *pDecState;

    ippsDecodeZfpGetStateSize_32f(&decStateSize);
    pDecState = (IppDecodeZfpState_32f *)ippsMalloc_8u(decStateSize);
    ippsDecodeZfpInit_32f((buffer + sizeof(pComprLen)), pComprLen, pDecState);
    // relative accuracy (a Fixed-Precision mode)
    ippsDecodeZfpSet_32f(IppZFPMINBITS, IppZFPMAXBITS, ZFPMAXPREC, IppZFPMINEXP,
                         pDecState);
    // absolute accuracy
    // ippsDecodeZfpSetAccuracy_32f(ZFPACCURACY, pDecState);

    // decompress 4x4x4 zfp blocks from a zfp stream
    float block[4 * 4 * 4];

    for (int y = 0; y < ZFP_CACHE_REGION; y += 4)
      for (int x = 0; x < ZFP_CACHE_REGION; x += 4)
      {
        ippsDecodeZfp444_32f(pDecState, block, 4 * sizeof(Ipp32f),
                             4 * 4 * sizeof(Ipp32f));

        // extract 4-frame data from a 4x4x4 block
        for (int _k = 0; _k < 4; _k++)
        {
          int offset = 4 * 4 * _k;
          for (int _j = 0; _j < 4; _j++)
            for (int _i = 0; _i < 4; _i++)
            {
              size_t _dst = (y + _j) * ZFP_CACHE_REGION + x + _i;
              _pixels[_k][_dst] = block[offset++];
            }
        }
      }

    ippsFree(pDecState);
  }

  // then the mask and final post-processing
  for (int k = 0; k < 4; k++)
  {
    if (mask_idz + k >= depth)
      break;

    auto mask_blocks = cube_mask[mask_idz + k].load();
    if (mask_blocks == nullptr)
      return res;

    Ipp8u *buffer = (*mask_blocks)[idy][idx].get();
    int compressed_size = *((int *)buffer);
    int decompressed_size = 0;

    if (compressed_size > 0)
      decompressed_size =
          LZ4_decompress_safe((const char *)(buffer + sizeof(compressed_size)),
                              (char *)_mask, compressed_size, mask_size);

    if (decompressed_size != mask_size)
    {
      printf("problems decompressing LZ4 mask [%d][%d]; compressed_size = %d, "
             "decompressed = %d\n",
             idy, idx, compressed_size, decompressed_size);
      return res;
    }

    // apply the NaN mask to floating-point pixels
    ispc::nan_mask(_pixels[k], _mask, region_size);
    /*#pragma simd
      for (unsigned int _i = 0; _i < work_size; _i++)
        if (_mask[_i] == 0)
          _pixels[_i] = std::numeric_limits<float>::quiet_NaN();*/
  }

  // add four new decompressed cache entries
  for (int k = 0; k < 4; k++)
  {
    size_t _frame = mask_idz + k;
    if (_frame >= depth)
      break;

    // create a new cache entry
    entry = std::shared_ptr<struct CacheEntry>(new struct CacheEntry());

    // convert float32 _pixels[k] to half-float
    if (entry)
    {
      struct CacheEntry *_entry = entry.get();

      if (_entry->data)
      {
        // printf("[%zu] float32 --> half-float conversion.\n", _frame);
        unsigned short *f16 = _entry->data.get();

        ispc::f32tof16(_pixels[k], f16, frame_min[_frame], frame_max[_frame],
                       MIN_HALF_FLOAT, MAX_HALF_FLOAT, region_size);

        // zero-copy transfer (return the shared pointer)
        if (k == sub_frame)
          res = _entry->data;

        // finally add a new entry to the cache
        std::lock_guard<std::shared_mutex> guard(cache_mtx[pixels_idz]); // lock the cache for writing
        cache[_frame][idy][idx] = std::move(entry);
      }
    }
  }

#if defined(__APPLE__) && defined(__MACH__)
  // release the memory
  for (int i = 0; i < 4; i++)
  {
    if (_pixels[i] != NULL)
      ippsFree(_pixels[i]);
  }

  if (_mask != NULL)
    ippsFree(_mask);
#endif

  return res;
}

void FITS::preempt_cache(int start, int end, int x1, int y1, int x2, int y2)
{

  if ((end < 0) || (start < 0) || (end > depth - 1) || (start > depth - 1))
    return;

  if (end < start)
  {
    int tmp = start;
    start = end;
    end = tmp;
  };

  // passed the sanity checks

  int _x1 = MAX(0, x1);
  int _y1 = MAX(0, y1);
  int _x2 = MIN(width - 1, x2);
  int _y2 = MIN(height - 1, y2);

  auto [_start_x, _start_y] = make_indices(_x1, _y1);
  auto [_end_x, _end_y] = make_indices(_x2, _y2);

  // a workaround for macOS
  int start_x = _start_x;
  int start_y = _start_y;
  int end_x = _end_x;
  int end_y = _end_y;

  //#pragma omp parallel for schedule(dynamic, 4) shared(start_x, end_x, start_y, end_y)
  for (size_t i = (start - (start % 4)); i <= end; i++)
  {
    //int tid = omp_get_thread_num();

    // ZFP needs a chunk of 4 iterations per thread; <start> needs to be a
    // multiple of 4
    if (i < start)
      continue;

    bool compressed_pixels = false;
    bool compressed_mask = false;

    int pixels_idz = i / 4;
    int sub_frame = i % 4; // a sub-pixels frame count in [0,4)
    int mask_idz = i;

    {
      auto pixel_blocks = cube_pixels[pixels_idz].load();
      if (pixel_blocks != nullptr)
        compressed_pixels = true;
    }

    {
      auto mask_blocks = cube_mask[mask_idz].load();
      if (mask_blocks != nullptr)
        compressed_mask = true;
    }

    // pre-empt the cache, ignore the result (std::shared_ptr<unsigned short> )
    if (compressed_pixels && compressed_mask)
    {
      for (auto idy = start_y; idy <= end_y; idy++)
        for (auto idx = start_x; idx <= end_x; idx++)
          request_cached_region_ptr(i, idy, idx);
    }
  }
}

std::vector<float> FITS::get_spectrum(int start, int end, int x1, int y1,
                                      int x2, int y2, intensity_mode intensity,
                                      beam_shape beam, double &elapsed)
{
  std::vector<float> spectrum;
  //std::vector<float> test;

  // sanity checks
  if (bitpix != -32)
    return spectrum;

  if ((end < 0) || (start < 0) || (end > depth - 1) || (start > depth - 1))
    return spectrum;

  if (end < start)
  {
    int tmp = start;
    start = end;
    end = tmp;
  };

  // passed the sanity checks
  int length = end - start + 1;

  // resize the spectrum vector
  spectrum.resize(length, 0);
  //test.resize(length, 0);

  // std::cout << "[get_spectrum]#0 " << x1 << " " << x2 << " " << y1 << " " <<
  // y2 << std::endl;

  int _x1 = MAX(0, x1);
  int _y1 = MAX(0, y1);
  int _x2 = MIN(width - 1, x2);
  int _y2 = MIN(height - 1, y2);

  // std::cout << "[get_spectrum]#1 " << _x1 << " " << _x2 << " " << _y1 << " "
  // << _y2 << std::endl;

  // include at least one pixel
  /*if (_x1 == _x2)
    _x2 = _x1 + 1;

  if (_y1 == _y2)
    _y2 = _y1 + 1;*/

  // std::cout << "[get_spectrum]#2 " << _x1 << " " << _x2 << " " << _y1 << " "
  // << _y2 << std::endl;

  bool average = (intensity == mean) ? true : false;

  int _cx = 0;
  int _cy = 0;
  int _r = 0;
  int _r2 = 0;

  if (beam == circle)
  {
    // calculate the centre and squared radius
    _cx = (_x1 + _x2) >> 1;
    _cy = (_y1 + _y2) >> 1;
    _r = MIN((_x2 - _x1) >> 1, (_y2 - _y1) >> 1);
    _r2 = _r * _r;

    // printf("cx = %d\tcy = %d\tr = %d\n", _cx, _cy, _r);
  };

  auto start_t = steady_clock::now();

  float _cdelt3 = this->has_velocity
                      ? this->cdelt3 * this->frame_multiplier / 1000.0f
                      : 1.0f;

  auto [_start_x, _start_y] = make_indices(_x1, _y1);
  auto [_end_x, _end_y] = make_indices(_x2, _y2);

  // a workaround for macOS
  int start_x = _start_x;
  int start_y = _start_y;
  int end_x = _end_x;
  int end_y = _end_y;

  // std::lock_guard<std::mutex> guard(fits_mtx);

#pragma omp parallel for schedule(dynamic, 4) \
    shared(start_x, end_x, start_y, end_y)
  for (size_t i = (start - (start % 4)); i <= end; i++)
  {
    //int tid = omp_get_thread_num();

    // ZFP needs a chunk of 4 iterations per thread; <start> needs to be a
    // multiple of 4
    if (i < start)
      continue;

    float spectrum_value = 0.0f;
    bool has_compressed_spectrum = false;
    bool compressed_pixels = false;
    bool compressed_mask = false;

    int pixels_idz = i / 4;
    int sub_frame = i % 4; // a sub-pixels frame count in [0,4)
    int mask_idz = i;

    {
      auto pixel_blocks = cube_pixels[pixels_idz].load();
      if (pixel_blocks != nullptr)
        compressed_pixels = true;
    }

    {
      auto mask_blocks = cube_mask[mask_idz].load();
      if (mask_blocks != nullptr)
        compressed_mask = true;
    }

    // use the cache holding decompressed pixel data
    if (compressed_pixels && compressed_mask)
    {
      // a zero-copy virtual <pixels_mosaic> operating on pointers to
      // decompressed regions from the cache
      int __cx = _cx - start_x * ZFP_CACHE_REGION;
      int __cy = _cy - start_y * ZFP_CACHE_REGION;

      int __y1 = _y1 - start_y * ZFP_CACHE_REGION;
      int __y2 = _y2 - start_y * ZFP_CACHE_REGION;

      for (auto idy = start_y; idy <= end_y; idy++)
      {
        int __x1 = _x1 - start_x * ZFP_CACHE_REGION;
        int __x2 = _x2 - start_x * ZFP_CACHE_REGION;
        int dy = (idy - start_y) * ZFP_CACHE_REGION;

        for (auto idx = start_x; idx <= end_x; idx++)
        {
          std::shared_ptr<unsigned short> region =
              request_cached_region_ptr(i, idy, idx);

          if (!region)
            goto jmp;

          // calculate a partial spectrum value based on one mosaic region
          int ___x1 = MAX(__x1, 0);
          int ___y1 = MAX(__y1, 0);
          int ___x2 = MIN(__x2, ZFP_CACHE_REGION);
          int ___y2 = MIN(__y2, ZFP_CACHE_REGION);
          int dx = (idx - start_x) * ZFP_CACHE_REGION;

          if (beam == circle)
            spectrum_value += ispc::calculate_radial_spectrumF16(
                region.get(), frame_min[i], frame_max[i], MIN_HALF_FLOAT,
                MAX_HALF_FLOAT, 0.0f, 1.0f, ignrval, datamin, datamax,
                ZFP_CACHE_REGION, ___x1, ___x2, ___y1, ___y2, dx, dy, __cx,
                __cy, _r2, average, _cdelt3);

          if (beam == square)
            spectrum_value += ispc::calculate_square_spectrumF16(
                region.get(), frame_min[i], frame_max[i], MIN_HALF_FLOAT,
                MAX_HALF_FLOAT, 0.0f, 1.0f, ignrval, datamin, datamax,
                ZFP_CACHE_REGION, ___x1, ___x2, ___y1, ___y2, average, _cdelt3);

          __x1 -= ZFP_CACHE_REGION;
          __x2 -= ZFP_CACHE_REGION;
        }

        __y1 -= ZFP_CACHE_REGION;
        __y2 -= ZFP_CACHE_REGION;
      }

      //test[i - start] = spectrum_value;
      spectrum[i - start] = spectrum_value;
      has_compressed_spectrum = true;
    }

  jmp:
    if (!has_compressed_spectrum && fits_cube[i])
    {
      if (beam == circle)
        spectrum_value = ispc::calculate_radial_spectrumBF32(
            (int32_t *)fits_cube[i].get(), bzero, bscale, ignrval, datamin, datamax,
            width, _x1, _x2, _y1, _y2, _cx, _cy, _r2, average, _cdelt3);

      if (beam == square)
        spectrum_value = ispc::calculate_square_spectrumBF32(
            (int32_t *)fits_cube[i].get(), bzero, bscale, ignrval, datamin, datamax,
            width, _x1, _x2, _y1, _y2, average, _cdelt3);
    }

    spectrum[i - start] = spectrum_value;
  }

  // debug
  /*for (int i = 0; i < length; i++)
    std::cout << i << ": " << test[i] << " *** " << spectrum[i] << std::endl;
  //std::cout << i << " : " << spectrum[i] << "\t";
  std::cout << std::endl;*/

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  elapsed = elapsedMilliseconds;

  return spectrum;
}

void FITS::zfp_compress()
{
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

void FITS::zfp_decompress_cube(size_t start_k)
{
  /*size_t end_k = MIN(start_k + 4, depth);

  for (size_t i = start_k; i < end_k; i++)
    if (!fits_cube[i])
      return;

  // decompress the frame
  int _x1 = 0;
  int _y1 = 0;
  int _x2 = width - 1;
  int _y2 = height - 1;

  auto [start_x, start_y] = make_indices(_x1, _y1);
  auto [end_x, end_y] = make_indices(_x2, _y2);

  // stitch together decompressed regions
  int dimx = end_x - start_x + 1;
  int dimy = end_y - start_y + 1;
  size_t region_size = ZFP_CACHE_REGION * ZFP_CACHE_REGION;
  //printf("verifying frame %zu; dimx: %d\tdimy: %d; start_x: %d, start_y: %d,
end_x: %d, end_y: %d\n", start_k, dimx, dimy, start_x, start_y, end_x, end_y);

  std::shared_ptr<Ipp32f> pixels_mosaic =
      std::shared_ptr<Ipp32f>(ippsMalloc_32f(dimx * dimy * region_size),
Ipp32fFree);

  if (!pixels_mosaic)
    return;

  {
    Ipp32f *_ptr = pixels_mosaic.get();
#pragma simd
    for (size_t _i = 0; _i < dimx * dimy * region_size; _i++)
      _ptr[_i] = std::numeric_limits<float>::quiet_NaN(); // for testing
purposes use NaN
  }

  // fill-in <pixels_mosaic> with decompressed regions from the cache
  for (auto idy = start_y; idy <= end_y; idy++)
  {
    Ipp32f *dst = pixels_mosaic.get() + (idy - start_y) * dimx * region_size;

    for (auto idx = start_x; idx <= end_x; idx++)
    {
      Ipp32f *offset = dst + (idx - start_x) * ZFP_CACHE_REGION;
      if (!request_cached_region(start_k, idy, idx, offset, dimx *
ZFP_CACHE_REGION))
      {
        printf("frame %zu, cannot decompress a region idy = %d, idx = %d\n",
start_k, idy, idx); return;
      }
    }
  }

#if !defined(__APPLE__) || !defined(__MACH__)
  // verify data
  size_t offset = 0;
  int32_t *src = (int32_t *)fits_cube[start_k].get();
  Ipp32f *_ptr = pixels_mosaic.get();
  bool invalid_real = false;
  bool invalid_nan = false;

  for (int line = 0; line < height; line++)
  {
    size_t dst = line * dimx * ZFP_CACHE_REGION;
    for (int x = 0; x < width; x++)
    {
      uint32_t raw = bswap_32(src[offset++]);
      float tmp = bzero + bscale * reinterpret_cast<float &>(raw);
      bool nan = std::isnan(tmp) || std::isinf(tmp) || (tmp <= ignrval) || (tmp
< datamin) || (tmp > datamax);

      if (!nan)
      {
        if (fabs(tmp - _ptr[dst + x]) > ZFPACCURACY)
          invalid_real = true;
        //printf("real: %f\tapprox.: %f\n", tmp, _ptr[dst + x]);
      }
      else
      {
        if (!std::isnan(_ptr[dst + x]))
          invalid_nan = true;
      }
    }
  }

  if (invalid_real || invalid_nan)
    printf("frame %zu: decompression mismatch: real(%d), NaN(%d).\n", start_k,
invalid_real, invalid_nan); else printf("frame %zu: OK.\n", start_k); #endif*/
}

void FITS::zfp_compress_cube(size_t start_k)
{
  size_t end_k = MIN(start_k + 4, depth);

  for (size_t i = start_k; i < end_k; i++)
    if (!fits_cube[i])
      return;

  // create subdirectories for ZFP and LZ4
  int zfp_idz = start_k / 4;

  /*std::string zfp_dir = FITSCACHE + std::string("/") +
                        boost::replace_all_copy(dataset_id, "/", "_") +
                        std::string(".zfp/") + std::to_string(zfp_idz);

  // create a directory on a best-effort basis, ignoring any errors
  if (mkdir(zfp_dir.c_str(), 0777) != 0)
    perror("(non-critical) cannot create a pixels sub-cache directory");*/

  std::string zfp_file = FITSCACHE + std::string("/") +
                         boost::replace_all_copy(dataset_id, "/", "_") +
                         std::string(".zfp/") + std::to_string(zfp_idz) + ".bin";

  // allocate memory for pixels and a mask
  const size_t plane_size = width * height;
  const size_t frame_size = plane_size * abs(bitpix / 8);

  Ipp32f *pixels[4];
  Ipp8u *mask[4];
  bool ok = true;

  for (int i = 0; i < 4; i++)
  {
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

  if (!ok)
  {
    for (int i = 0; i < 4; i++)
    {
      if (pixels[i] != NULL)
        ippsFree(pixels[i]);

      if (mask[i] != NULL)
        ippsFree(mask[i]);
    }

    return;
  }

  // use ispc to fill in the pixels and mask
  int plane_count = 0;
  for (size_t frame = start_k; frame < end_k; frame++)
  {
    ispc::make_planeF32((int32_t *)fits_cube[frame].get(), bzero, bscale, ignrval,
                        datamin, datamax, pixels[plane_count],
                        mask[plane_count], plane_size);

#ifdef PRELOAD
    int pixels_idz = frame / 4;

    // for each pixels[place_count] and mask[place_count] divide the image and convert float32 into half-float
    for (int src_y = 0; src_y < height; src_y += ZFP_CACHE_REGION)
      for (int src_x = 0; src_x < width; src_x += ZFP_CACHE_REGION)
      {
        // block indexing
        int idx = src_x / ZFP_CACHE_REGION;
        int idy = src_y / ZFP_CACHE_REGION;

        // create a new cache entry
        std::shared_ptr<CacheEntry> entry = std::shared_ptr<struct CacheEntry>(new struct CacheEntry());

        if (entry)
        {
          struct CacheEntry *_entry = entry.get();

          if (_entry->data)
          {
            unsigned short *f16 = _entry->data.get();

            Ipp32f *_pixels = pixels[plane_count];
            Ipp8u *_mask = mask[plane_count];

            // convert to half-float (adjust the src offset for src_x and src_y)
            ispc::f32PIXMtof16(_pixels, _mask, src_x, src_y, width, height, f16, ZFP_CACHE_REGION, frame_min[frame], frame_max[frame], MIN_HALF_FLOAT, MAX_HALF_FLOAT);

            // finally add a new entry to the decompression cache
            std::lock_guard<std::shared_mutex> guard(cache_mtx[pixels_idz]); // lock the cache for writing
            cache[frame][idy][idx] = std::move(entry);
          }
        }
      }
#endif

    plane_count++;
  }

  // divide the image into 256 x 256 x 4 regions to be compressed individually
  // a cache scheme will decompress those regions on demand
  size_t storage_size =
      sizeof(Ipp32f) * ZFP_CACHE_REGION * ZFP_CACHE_REGION * 4;
  Ipp8u *pBuffer = ippsMalloc_8u_L(storage_size);

  if (pBuffer != NULL)
  {
    compressed_blocks *blocks = new compressed_blocks();

    if (blocks == NULL)
      printf("error allocating memory for pixels::compressed_blocks@%d\n", zfp_idz);
    else
      for (int src_y = 0; src_y < height; src_y += ZFP_CACHE_REGION)
        for (int src_x = 0; src_x < width; src_x += ZFP_CACHE_REGION)
        {
          // block indexing
          int idx = src_x / ZFP_CACHE_REGION;
          int idy = src_y / ZFP_CACHE_REGION;

          // start a new ZFP stream
          int encStateSize;
          IppEncodeZfpState_32f *pEncState;
          int pComprLen = 0;
          int pComprLen_plus = 0;

          ippsEncodeZfpGetStateSize_32f(&encStateSize);
          pEncState = (IppEncodeZfpState_32f *)ippsMalloc_8u(encStateSize);
          ippsEncodeZfpInit_32f(pBuffer, storage_size, pEncState);
          // relative accuracy (a Fixed-Precision mode)
          ippsEncodeZfpSet_32f(IppZFPMINBITS, IppZFPMAXBITS, ZFPMAXPREC,
                               IppZFPMINEXP, pEncState);
          // absolute accuracy
          // ippsEncodeZfpSetAccuracy_32f(ZFPACCURACY, pEncState);

          // ... ZFP compression
          int x, y;
          int i, j, k;
          float val;
          float block[4 * 4 * 4];

          // compress the pixels with ZFP
          for (y = 0; y < ZFP_CACHE_REGION; y += 4)
            for (x = 0; x < ZFP_CACHE_REGION; x += 4)
            {
              // fill a 4x4x4 block
              int offset = 0;
              for (k = 0; k < 4; k++)
                for (j = y; j < y + 4; j++)
                  for (i = x; i < x + 4; i++)
                  {
                    if (src_x + i >= width || src_y + j >= height)
                      val = 0.0f;
                    else
                    {
                      // adjust the src offset for src_x and src_y
                      size_t src = (src_y + j) * width + src_x + i;
                      val = pixels[k][src];
                    }

                    block[offset++] = val;
                  }

              ippsEncodeZfp444_32f(block, 4 * sizeof(Ipp32f),
                                   4 * 4 * sizeof(Ipp32f), pEncState);
            }

          ippsEncodeZfpFlush_32f(pEncState);
          ippsEncodeZfpGetCompressedSize_32f(pEncState, &pComprLen);
          ippsFree(pEncState);

          pComprLen_plus = pComprLen + sizeof(pComprLen);

          /*printf("zfp-compressing pixels %dx%dx4 at (%d,%d,%zu); pComprLen "
               "= %d, "
               "orig. "
               "= %zu bytes.\n",
               ZFP_CACHE_REGION, ZFP_CACHE_REGION, src_x, src_y, start_k,
               pComprLen,
               storage_size);*/

          std::shared_ptr<Ipp8u> block_pixels;

          // use a file-backed mmap
          bool is_mmapped = false;
          int fd = -1;
          /*std::string storage = zfp_dir + "/" + std::to_string(idy) + "_" +
                                std::to_string(idx) + ".bin";
          fd = open(storage.c_str(), O_RDWR | O_CREAT, (mode_t)0600);

          if (fd != -1)
          {
#if defined(__APPLE__) && defined(__MACH__)
            int stat = ftruncate(fd, pComprLen_plus);
#else
            int stat = ftruncate64(fd, pComprLen_plus);
#endif

            if (!stat)
            {
              // file-mmap pComprLen
              block_pixels = std::shared_ptr<Ipp8u>(
                  (Ipp8u *)mmap(nullptr, pComprLen_plus, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0),
                  [=](void *ptr) {
                    if (ptr != MAP_FAILED)
                      munmap(ptr, pComprLen_plus);
                  });

              if (block_pixels.get() != MAP_FAILED)
              {
                is_mmapped = true;
                madvise(block_pixels.get(), pComprLen_plus, MADV_WILLNEED);
              }
              else
                std::cout << "block_pixels MAP_FAILED, will switch over to RAM\n";
            }
            else
              perror("ftruncate64");

            close(fd);
            fd = -1;
          }
          else
            perror(storage.c_str());*/

          // switch to RAM instead of mmap in case of trouble
          if (!is_mmapped)
            /*block_pixels = std::shared_ptr<Ipp8u>(ippsMalloc_8u_L(pComprLen_plus),
                                                  Ipp8uFree);*/
            block_pixels = std::shared_ptr<Ipp8u>(ippsMalloc_8u(pComprLen_plus),
                                                  [=](Ipp8u *ptr) {
                                                  if(ptr != NULL) {
                                                    // append the buffer to the zfp_file
                                                    int _fd = open(zfp_file.c_str(), O_WRONLY | O_APPEND | O_CREAT, (mode_t)0600);                                                              

                                                    if(_fd != -1)
                                                    {
                                                      // prepare an in-memory buffer
                                                      size_t offset = 0;
                                                      size_t size = sizeof(idy) + sizeof(idx) + pComprLen_plus;
                                                      char out[size];
                                                      
                                                      memcpy(ptr + offset, &idy, sizeof(idy));
                                                      offset += sizeof(idy);

                                                      memcpy(ptr + offset, &idx, sizeof(idx));
                                                      offset += sizeof(idx);

                                                      memcpy(ptr + offset, ptr, pComprLen_plus);

                                                      // finally an atomic append write to the file
                                                      ssize_t bytes_written = pwrite(_fd, out, size, 0);
                                                      if(bytes_written != size)
                                                        printf("an error appending a pixel block [%d][%d] to %s\n", idy, idx, zfp_file.c_str());

                                                      close(_fd);
                                                    }
                                                    else
                                                      perror(zfp_file.c_str());

                                                    // finally release the memory
                                                    Ipp8uFree(ptr);
                                                   } });

          // finally memcpy <pComprLen> bytes+ from pBuffer
          {
            Ipp8u *ptr = block_pixels.get();

            if (ptr != MAP_FAILED && ptr != NULL)
            {
              // compressed size
              memcpy(ptr, &pComprLen, sizeof(pComprLen));

              // compressed data
              memcpy(ptr + sizeof(pComprLen), pBuffer, pComprLen);
            }
          }

          try
          {
            (*blocks)[idy][idx] = std::move(block_pixels);
          }
          catch (std::bad_alloc const &err)
          {
            std::cout << "cube_pixels:" << err.what() << "\t" << zfp_idz << ","
                      << idy << "," << idx << '\n';
            exit(1);
          }

          if (fd != -1)
            close(fd);
        }

    // add the blocks to cube_pixels
    cube_pixels[zfp_idz].store(blocks);

    ippsFree(pBuffer);
  }

  // compress the four masks with LZ4
  int compressed_size = 0;
  size_t compressed_size_plus = 0;
  Ipp8u _mask[ZFP_CACHE_REGION * ZFP_CACHE_REGION];

  size_t mask_size = ZFP_CACHE_REGION * ZFP_CACHE_REGION * sizeof(Ipp8u);
  int worst_size = LZ4_compressBound(mask_size);
  pBuffer = ippsMalloc_8u_L(worst_size);

  if (pBuffer != NULL)
  {
    for (int k = 0; k < 4; k++)
    {
      int lz4_idz = start_k + k;

      /*std::string lz4_dir = FITSCACHE + std::string("/") +
                            boost::replace_all_copy(dataset_id, "/", "_") +
                            std::string(".lz4/") + std::to_string(lz4_idz);

      if (mkdir(lz4_dir.c_str(), 0777) != 0)
        perror("(non-critical) cannot create a mask sub-cache directory");*/

      std::string lz4_file = FITSCACHE + std::string("/") +
                             boost::replace_all_copy(dataset_id, "/", "_") +
                             std::string(".lz4/") + std::to_string(lz4_idz) + ".bin";

      compressed_blocks *blocks = new compressed_blocks();

      if (blocks == NULL)
        printf("error allocating memory for mask::compressed_blocks@%d\n", lz4_idz);
      else
        for (int src_y = 0; src_y < height; src_y += ZFP_CACHE_REGION)
          for (int src_x = 0; src_x < width; src_x += ZFP_CACHE_REGION)
          {
            // block indexing
            int idx = src_x / ZFP_CACHE_REGION;
            int idy = src_y / ZFP_CACHE_REGION;

            int offset = 0;
            char val;

            for (int y = 0; y < ZFP_CACHE_REGION; y++)
              for (int x = 0; x < ZFP_CACHE_REGION; x++)
              {
                if (src_x + x >= width || src_y + y >= height)
                  val = 0;
                else
                {
                  // adjust the src offset for src_x and src_y
                  size_t src = (src_y + y) * width + src_x + x;
                  val = mask[k][src];
                }

                _mask[offset++] = val;
              }

            // _mask has been filled-in; compress it
            compressed_size =
                LZ4_compress_HC((const char *)_mask, (char *)pBuffer, mask_size,
                                worst_size, LZ4HC_CLEVEL_MAX);
            compressed_size_plus = compressed_size + sizeof(compressed_size);

            /*printf("lz4-compressing mask %dx%dx4 at (%d,%d,%d); compressed "
                 "= %d, "
                 "plus = %d, "
                 "orig. "
                 "= %zu bytes.\n",
                 ZFP_CACHE_REGION, ZFP_CACHE_REGION, src_x, src_y, lz4_idz,
                 compressed_size,
                 compressed_size_plus,
                 mask_size);*/

            std::shared_ptr<Ipp8u> block_mask;

            // use a file-backed mmap
            bool is_mmapped = false;
            int fd = -1;
            /*std::string storage = lz4_dir + "/" + std::to_string(idy) + "_" +
                                  std::to_string(idx) + ".bin";
            fd = open(storage.c_str(), O_RDWR | O_CREAT, (mode_t)0600);

            if (fd != -1)
            {
#if defined(__APPLE__) && defined(__MACH__)
              int stat = ftruncate(fd, compressed_size_plus);
#else
              int stat = ftruncate64(fd, compressed_size_plus);
#endif

              if (!stat)
              {
                // file-mmap compressed_size
                block_mask = std::shared_ptr<Ipp8u>(
                    (Ipp8u *)mmap(nullptr, compressed_size_plus,
                                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0),
                    [=](void *ptr) {
                      if (ptr != MAP_FAILED)
                        munmap(ptr, compressed_size_plus);
                    });

                if (block_mask.get() != MAP_FAILED)
                {
                  is_mmapped = true;
                  madvise(block_mask.get(), compressed_size_plus, MADV_WILLNEED);
                }
                else
                  std::cout << "block_mask MAP_FAILED, will switch over to RAM\n";
              }
              else
                perror("ftruncate64");

              close(fd);
              fd = -1;
            }
            else
              perror(storage.c_str());*/

            // switch to RAM instead of mmap in case of trouble
            if (!is_mmapped)
            {
              // std::cout << "mmap failed, failover to RAM\n";
              /*block_mask = std::shared_ptr<Ipp8u>(
                  ippsMalloc_8u_L(compressed_size_plus), Ipp8uFree);*/
              block_mask = std::shared_ptr<Ipp8u>(ippsMalloc_8u(compressed_size_plus),
                                                  [=](Ipp8u *ptr) {
                                                  if(ptr != NULL) {
                                                    // append the buffer to the zfp_file
                                                    int _fd = open(lz4_file.c_str(), O_WRONLY | O_APPEND | O_CREAT, (mode_t)0600);                                                              

                                                    if(_fd != -1)
                                                    {
                                                      // prepare an in-memory buffer
                                                      size_t offset = 0;
                                                      size_t size = sizeof(idy) + sizeof(idx) + compressed_size_plus;
                                                      char out[size];
                                                      
                                                      memcpy(ptr + offset, &idy, sizeof(idy));
                                                      offset += sizeof(idy);

                                                      memcpy(ptr + offset, &idx, sizeof(idx));
                                                      offset += sizeof(idx);

                                                      memcpy(ptr + offset, ptr, compressed_size_plus);

                                                      // finally an atomic append write to the file
                                                      ssize_t bytes_written = pwrite(_fd, out, size, 0);
                                                      if(bytes_written != size)
                                                        printf("an error appending a mask block [%d][%d] to %s\n", idy, idx, lz4_file.c_str());

                                                      close(_fd);
                                                    }
                                                    else
                                                      perror(lz4_file.c_str());

                                                    // finally release the memory
                                                    Ipp8uFree(ptr);
                                                   } });
            }

            // finally memcpy <compressed_size> bytes+ from pBuffer
            {
              Ipp8u *ptr = block_mask.get();

              if (ptr != MAP_FAILED && ptr != NULL)
              {
                // compressed size
                memcpy(ptr, &compressed_size, sizeof(compressed_size));

                // compressed data
                memcpy(ptr + sizeof(compressed_size), pBuffer, compressed_size);
              }
            }

            try
            {
              (*blocks)[idy][idx] = std::move(block_mask);
            }
            catch (std::bad_alloc const &err)
            {
              std::cout << "cube_mask:" << err.what() << "\t" << lz4_idz << ","
                        << idy << "," << idx << '\n';
              exit(1);
            }

            if (fd != -1)
              close(fd);
          }

      // add the blocks to cube_mask
      cube_mask[lz4_idz].store(blocks);
    }

    // LZ4 done, release the buffer
    ippsFree(pBuffer);
  }

  for (int i = 0; i < 4; i++)
  {
    if (pixels[i] != NULL)
      ippsFree(pixels[i]);

    if (mask[i] != NULL)
      ippsFree(mask[i]);
  }

  // free memory / release mmap pages
  for (size_t frame = start_k; frame < end_k; frame++)
    fits_cube[frame].reset();
}

void FITS::zfp_compression_thread(int tid)
{
  printf("launched a ZFP compression thread#%d\n", tid);

  // await compression requests
  while (!terminate_compression)
  {
    size_t frame;

    while (zfp_queue.pop(frame))
      zfp_compress_cube(frame);
  }

  printf("ZFP compression thread#%d has terminated.\n", tid);
}

IppStatus tileResize32f_C1R(Ipp32f *pSrc, IppiSize srcSize, Ipp32s srcStep,
                            Ipp32f *pDst, IppiSize dstSize, Ipp32s dstStep,
                            bool mirror)
{

  // int MAX_NUM_THREADS = omp_get_max_threads();
  int max_threads = omp_get_max_threads();

  // a per-thread limit
  size_t max_work_size = 1024 * 1024 * 4;
  size_t plane_size = size_t(srcSize.width) * size_t(srcSize.height);
  size_t work_size = MIN(plane_size, max_work_size);
  int MAX_NUM_THREADS =
      MAX((int)roundf(float(plane_size) / float(work_size)), 1);
  printf("tileResize32f_C1R::num_threads = %d\n", MAX_NUM_THREADS);

  IppiResizeSpec_32f *pSpec = 0;
  int specSize = 0, initSize = 0, bufSize = 0;
  Ipp8u *pBuffer = 0;
  Ipp8u *pInitBuf = 0;
  Ipp32u numChannels = 1;
  IppiPoint dstOffset = {0, 0};
  IppiPoint srcOffset = {0, 0};
  IppStatus status = ippStsNoErr;
  IppiBorderSize borderSize = {0, 0, 0, 0};
  IppiBorderType border = ippBorderRepl;
  int numThreads, slice, tail;
  int bufSize1, bufSize2;
  IppiSize dstTileSize, dstLastTileSize;
  IppStatus pStatus[MAX_NUM_THREADS];

  /* Spec and init buffer sizes */
  status = ippiResizeGetSize_32f(srcSize, dstSize, ippLanczos, 0, &specSize,
                                 &initSize);

  if (status != ippStsNoErr)
    return status;

  /* Memory allocation */
  pInitBuf = ippsMalloc_8u(initSize);
  pSpec = (IppiResizeSpec_32f *)ippsMalloc_8u(specSize);

  if (pInitBuf == NULL || pSpec == NULL)
  {
    ippsFree(pInitBuf);
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }

  /* Filter initialization */
  status = ippiResizeLanczosInit_32f(srcSize, dstSize, 3, pSpec, pInitBuf);
  ippsFree(pInitBuf);

  if (status != ippStsNoErr)
  {
    ippsFree(pSpec);
    return status;
  }

  status = ippiResizeGetBorderSize_32f(pSpec, &borderSize);
  if (status != ippStsNoErr)
  {
    ippsFree(pSpec);
    return status;
  }

  /* General transform function */
  /* Parallelized only by Y-direction here */
#pragma omp parallel num_threads(MAX_NUM_THREADS)
  {
#pragma omp master
    {
      numThreads = omp_get_num_threads();
      slice = dstSize.height / numThreads;
      tail = dstSize.height % numThreads;

      dstTileSize.width = dstLastTileSize.width = dstSize.width;
      dstTileSize.height = slice;
      dstLastTileSize.height = slice + tail;

      ippiResizeGetBufferSize_32f(pSpec, dstTileSize, ippC1, &bufSize1);
      ippiResizeGetBufferSize_32f(pSpec, dstLastTileSize, ippC1, &bufSize2);

      pBuffer = ippsMalloc_8u(bufSize1 * (numThreads - 1) + bufSize2);
    }

#pragma omp barrier
    {
      if (pBuffer)
      {
        int i;
        Ipp32f *pSrcT, *pDstT;
        Ipp8u *pOneBuf;
        IppiPoint srcOffset = {0, 0};
        IppiPoint dstOffset = {0, 0};
        IppiSize srcSizeT = srcSize;
        IppiSize dstSizeT = dstTileSize;

        i = omp_get_thread_num();
        dstSizeT.height = slice;
        dstOffset.y += i * slice;

        if (i == numThreads - 1)
          dstSizeT = dstLastTileSize;

        pStatus[i] = ippiResizeGetSrcRoi_32f(pSpec, dstOffset, dstSizeT,
                                             &srcOffset, &srcSizeT);

        if (pStatus[i] == ippStsNoErr)
        {
          pSrcT = pSrc + srcOffset.y * srcStep;
          if (!mirror)
            pDstT = pDst + dstOffset.y * dstStep;
          else
          {
            if (i == numThreads - 1)
              pDstT = pDst;
            else
              pDstT = pDst + (dstSize.height - (i + 1) * slice) * dstStep;
          }

          pOneBuf = pBuffer + i * bufSize1;

          pStatus[i] = ippiResizeLanczos_32f_C1R(
              pSrcT, srcStep * sizeof(Ipp32f), pDstT, dstStep * sizeof(Ipp32f),
              dstOffset, dstSizeT, border, 0, pSpec, pOneBuf);

          // flip the buffer
          if (mirror)
            ispc::mirror_float32(pDstT, dstSizeT.width, dstSizeT.height);
        }
      }
    }
  }

  ippsFree(pSpec);

  if (pBuffer == NULL)
    return ippStsNoMemErr;

  ippsFree(pBuffer);

  for (int i = 0; i < numThreads; ++i)
  {
    /* Return bad status */
    if (pStatus[i] != ippStsNoErr)
      return pStatus[i];
  }

  return status;
}

IppStatus tileResize8u_C1R(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep,
                           Ipp8u *pDst, IppiSize dstSize, Ipp32s dstStep,
                           bool mirror)
{

  // int MAX_NUM_THREADS = omp_get_max_threads();
  int max_threads = omp_get_max_threads();

  // a per-thread limit
  size_t max_work_size = 1024 * 1024 * 4;
  size_t plane_size = size_t(srcSize.width) * size_t(srcSize.height);
  size_t work_size = MIN(plane_size, max_work_size);
  int MAX_NUM_THREADS =
      MAX((int)roundf(float(plane_size) / float(work_size)), 1);
  printf("tileResize8u_C1R::num_threads = %d\n", MAX_NUM_THREADS);

  IppiResizeSpec_32f *pSpec = 0;
  int specSize = 0, initSize = 0, bufSize = 0;
  Ipp8u *pBuffer = 0;
  Ipp8u *pInitBuf = 0;
  Ipp32u numChannels = 1;
  IppiPoint dstOffset = {0, 0};
  IppiPoint srcOffset = {0, 0};
  IppStatus status = ippStsNoErr;
  IppiBorderSize borderSize = {0, 0, 0, 0};
  IppiBorderType border = ippBorderRepl;
  int numThreads, slice, tail;
  int bufSize1, bufSize2;
  IppiSize dstTileSize, dstLastTileSize;
  IppStatus pStatus[MAX_NUM_THREADS];

  /* Spec and init buffer sizes */
  status = ippiResizeGetSize_8u(srcSize, dstSize, ippLanczos, 0, &specSize,
                                &initSize);

  if (status != ippStsNoErr)
    return status;

  /* Memory allocation */
  pInitBuf = ippsMalloc_8u(initSize);
  pSpec = (IppiResizeSpec_32f *)ippsMalloc_8u(specSize);

  if (pInitBuf == NULL || pSpec == NULL)
  {
    ippsFree(pInitBuf);
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }

  /* Filter initialization */
  status = ippiResizeLanczosInit_8u(srcSize, dstSize, 3, pSpec, pInitBuf);
  ippsFree(pInitBuf);

  if (status != ippStsNoErr)
  {
    ippsFree(pSpec);
    return status;
  }

  status = ippiResizeGetBorderSize_8u(pSpec, &borderSize);
  if (status != ippStsNoErr)
  {
    ippsFree(pSpec);
    return status;
  }

  /* General transform function */
  /* Parallelized only by Y-direction here */
#pragma omp parallel num_threads(MAX_NUM_THREADS)
  {
#pragma omp master
    {
      numThreads = omp_get_num_threads();
      slice = dstSize.height / numThreads;
      tail = dstSize.height % numThreads;

      dstTileSize.width = dstLastTileSize.width = dstSize.width;
      dstTileSize.height = slice;
      dstLastTileSize.height = slice + tail;

      ippiResizeGetBufferSize_8u(pSpec, dstTileSize, ippC1, &bufSize1);
      ippiResizeGetBufferSize_8u(pSpec, dstLastTileSize, ippC1, &bufSize2);

      pBuffer = ippsMalloc_8u(bufSize1 * (numThreads - 1) + bufSize2);
    }

#pragma omp barrier
    {
      if (pBuffer)
      {
        int i;
        Ipp8u *pSrcT, *pDstT;
        Ipp8u *pOneBuf;
        IppiPoint srcOffset = {0, 0};
        IppiPoint dstOffset = {0, 0};
        IppiSize srcSizeT = srcSize;
        IppiSize dstSizeT = dstTileSize;

        i = omp_get_thread_num();
        dstSizeT.height = slice;
        dstOffset.y += i * slice;

        if (i == numThreads - 1)
          dstSizeT = dstLastTileSize;

        pStatus[i] = ippiResizeGetSrcRoi_8u(pSpec, dstOffset, dstSizeT,
                                            &srcOffset, &srcSizeT);

        if (pStatus[i] == ippStsNoErr)
        {
          pSrcT = (Ipp8u *)((char *)pSrc + srcOffset.y * srcStep);

          if (!mirror)
            pDstT = (Ipp8u *)((char *)pDst + dstOffset.y * dstStep);
          else
          {
            if (i == numThreads - 1)
              pDstT = pDst;
            else
              pDstT = pDst + (dstSize.height - (i + 1) * slice) * dstStep;
          }

          pOneBuf = pBuffer + i * bufSize1;

          pStatus[i] = ippiResizeLanczos_8u_C1R(pSrcT, srcStep, pDstT, dstStep,
                                                dstOffset, dstSizeT, border, 0,
                                                pSpec, pOneBuf);

          // flip the buffer
          if (mirror)
            ispc::mirror_u8(pDstT, dstSizeT.width, dstSizeT.height);
        }
      }
    }
  }

  ippsFree(pSpec);

  if (pBuffer == NULL)
    return ippStsNoMemErr;

  ippsFree(pBuffer);

  for (int i = 0; i < numThreads; ++i)
  {
    /* Return bad status */
    if (pStatus[i] != ippStsNoErr)
      return pStatus[i];
  }

  return status;
}

void tileMirror32f_C1R(Ipp32f *pSrc, Ipp32f *pDst, int width, int height)
{
  int max_threads = omp_get_max_threads();

  // a per-thread limit
  size_t max_work_size = 1024 * 1024 * 4;
  size_t plane_size = size_t(width) * size_t(height);
  size_t work_size = MIN(plane_size, max_work_size);
  int MAX_NUM_THREADS =
      MAX((int)roundf(float(plane_size) / float(work_size)), 1);
  printf("tileMirror32f_C1R::num_threads = %d\n", MAX_NUM_THREADS);

#pragma omp parallel num_threads(MAX_NUM_THREADS)
  {
    int tid = omp_get_thread_num();
    int numThreads = omp_get_num_threads();

    int slice, tail, tileHeight;
    slice = height / numThreads;
    tail = height % numThreads;

    tileHeight = slice;
    if (tid == numThreads - 1)
      tileHeight += tail;

    Ipp32f *pSrcT, *pDstT;
    pSrcT = pSrc + tid * slice * width;
    pDstT = pDst + tid * slice * width;

    // flip the buffer
    ispc::copy_mirror_float32(pSrcT, pDstT, width, height);
  }
}