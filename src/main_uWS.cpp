#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define SERVER_PORT 8080
#define SERVER_STRING                                                 \
  "FITSWebQL SE v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR( \
      VERSION_SUB)

#define WASM_VERSION "20.11.27.2"
#define VERSION_STRING "SV2020-12-25.4"

// OpenEXR
#include <OpenEXR/IlmThread.h>
#include <OpenEXR/ImfNamespace.h>
#include <OpenEXR/ImfThreading.h>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include <OpenEXR/ImfStdIO.h>

using namespace OPENEXR_IMF_NAMESPACE;

#include <omp.h>

#include <zlib.h>

/* CHUNK is the size of the memory chunk used by the zlib routines. */

#define CHUNK 0x4000
#define _windowBits 15
#define GZIP_ENCODING 16

/* The following macro calls a zlib routine and checks the return
   value. If the return value ("status") is not OK, it prints an error
   message and exits the program. Zlib's error statuses are all less
   than zero. */

#define CALL_ZLIB(x)                                                        \
  {                                                                         \
    int status;                                                             \
    status = x;                                                             \
    if (status < 0)                                                         \
    {                                                                       \
      fprintf(stderr, "%s:%d: %s returned a bad status of %d.\n", __FILE__, \
              __LINE__, #x, status);                                        \
      /*exit(EXIT_FAILURE);*/                                               \
    }                                                                       \
  }

#include <pwd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <time.h>
#include <errno.h>

#ifdef DEBUG
#include <jemalloc/jemalloc.h>
#endif

/** Thread safe cout class
 * Exemple of use:
 *    PrintThread{} << "Hello world!" << std::endl;
 */
class PrintThread : public std::ostringstream
{
public:
  PrintThread() = default;

  ~PrintThread()
  {
    std::lock_guard<std::mutex> guard(_mutexPrint);
    std::cout << this->str();
  }

private:
  static std::mutex _mutexPrint;
};

std::mutex PrintThread::_mutexPrint{};

/* msleep(): Sleep for the requested number of milliseconds. */
int msleep(long msec)
{
  struct timespec ts;
  int res;

  if (msec < 0)
  {
    errno = EINVAL;
    return -1;
  }

  ts.tv_sec = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;

  do
  {
    res = nanosleep(&ts, &ts);
  } while (res && errno == EINTR);

  return res;
}

#include <ipp.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "global.h"

#define HTTP_CHUNK 0x4000
//#define HTTP_CHUNK 0x100000

void send_chunk(uWS::HttpResponse<false> *res, const char *buf, size_t len, std::shared_ptr<std::atomic<bool>> aborted)
{
  if (buf == NULL || len == 0)
    return;

  size_t offset = 0;
  size_t remaining = len;

  while (remaining > 0)
  {
    size_t chunk_size = MIN(remaining, HTTP_CHUNK);

    bool status = false;
    bool sent = false;

    while (!sent)
    {
      // send the chunk
      if (*aborted.get() != true)
      {
        status = res->write(std::string_view(buf + offset, chunk_size));
        status = true; // a manual override whilst debugging transmission problems ...
      }
      else
        return;

      if (!status)
      {
        std::cout << "[send_chunk]::status: " << status << ", sleeping for 250ms" << std::endl;
        msleep(250); //sleep(1);
      }
      else
        sent = true;
    }

    offset += chunk_size;
    remaining -= chunk_size;
  }
}

std::atomic<bool> exiting(false);

#ifdef DEBUG
std::thread memory_thread;
#endif

#ifdef CLUSTER
zactor_t *speaker = NULL;
zactor_t *listener = NULL;
std::thread beacon_thread;
#endif

#include <curl/curl.h>
#include <sqlite3.h>

#ifndef LOCAL
#if defined(__APPLE__) && defined(__MACH__)
#include <libpq-fe.h>
#else
#include <libpq-fe.h>
//#include <pgsql/libpq-fe.h>
#endif

#define FITSHOME "/home"
#define JVO_HOST "localhost"
#define JVO_USER "jvo"
#endif

#include "fits.hpp"
#include "json.h"
#include "lttb.hpp"
#include "webql.h"
#include <fpzip.h>

#include "FITSRaster.hpp"
#include "contours.h"

#include "par_msquares.hpp"

struct SpectrumPoint
{
  float x = 0;
  float y = 0;
};

using PointLttb =
    LargestTriangleThreeBuckets<SpectrumPoint, float, &SpectrumPoint::x,
                                &SpectrumPoint::y>;

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::shared_mutex fits_mutex;
std::string home_dir;
std::string docs_root = "htdocs";
int server_port = SERVER_PORT;
sqlite3 *splat_db = NULL;

std::shared_ptr<FITS> get_dataset(std::string id)
{
  std::shared_lock<std::shared_mutex> lock(fits_mutex);

  auto item = DATASETS.find(id);

  if (item == DATASETS.end())
    return nullptr;
  else
    return item->second;
}

void insert_dataset(std::string id, std::shared_ptr<FITS> fits)
{
  std::lock_guard<std::shared_mutex> guard(fits_mutex);

  DATASETS.insert(std::pair(id, fits));
}

inline const char *check_null(const char *str)
{
  if (str != nullptr)
    return str;
  else
    return "\"\"";
};

void signalHandler(int signum)
{
  std::cout << "Interrupt signal (" << signum << ") received.\n";

  exiting = true;

  // stop any inter-node cluster communication
#ifdef CLUSTER
  if (speaker != NULL)
  {
    zstr_sendx(speaker, "SILENCE", NULL);

    const char *message = "JVO:>FITSWEBQL::LEAVE";
    const int interval = 1000; //[ms]
    zsock_send(speaker, "sbi", "PUBLISH", message, strlen(message), interval);

    zstr_sendx(speaker, "SILENCE", NULL);
    zactor_destroy(&speaker);
  }

  if (listener != NULL)
  {
    zstr_sendx(listener, "UNSUBSCRIBE", NULL);
    beacon_thread.join();
    zactor_destroy(&listener);
  }
#endif

#ifdef DEBUG
  memory_thread.join();
#endif

  // cleanup and close up stuff here
  {
    std::lock_guard<std::shared_mutex> guard(fits_mutex);
    DATASETS.clear();
  }

  curl_global_cleanup();

  if (splat_db != NULL)
  {
    sqlite3_close(splat_db);
    splat_db = NULL;
  }

  x265_cleanup();

  std::cout << "FITSWebQL shutdown completed." << std::endl;

  // terminate program
  exit(signum);

  // raise the original signal
  // raise(signum);
}

bool is_gzip(const char *filename)
{
  int fd = open(filename, O_RDONLY);

  if (fd == -1)
    return false;

  bool ok = true;
  uint8_t header[10];

  // try to read the first 10 bytes
  ssize_t bytes_read = read(fd, header, 10);

  // test for magick numbers and the deflate compression type
  if (bytes_read == 10)
  {
    if (header[0] != 0x1f || header[1] != 0x8b || header[2] != 0x08)
      ok = false;
  }
  else
    ok = false;

  close(fd);

  return ok;
}

// resource not found
void http_not_found(uWS::HttpResponse<false> *res)
{
  res->writeStatus("404 Not Found");
  res->end();
  // res->end("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
}

// server error
void http_internal_server_error(uWS::HttpResponse<false> *res)
{
  res->writeStatus("500 Internal Server Error");
  res->end();
  // res->end("HTTP/1.1 500 Internal Server Error\r\nContent-Length:
  // 0\r\n\r\n");
}

// request accepted but not ready yet
void http_accepted(uWS::HttpResponse<false> *res)
{
  res->writeStatus("202 Accepted");
  res->end();
  // res->end("HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
}

// functionality not implemented/not available
void http_not_implemented(uWS::HttpResponse<false> *res)
{
  res->writeStatus("501 Not Implemented");
  res->end();
  // res->end("HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n");
}

void get_spectrum(uWS::HttpResponse<false> *res, std::shared_ptr<FITS> fits,
                  std::shared_ptr<std::atomic<bool>> aborted)
{
  std::ostringstream json;

  fits->to_json(json);

  if (*aborted.get() == true)
  {
    printf("[get_spectrum] aborted http connection detected.\n");
    return;
  }

  if (json.tellp() > 0)
  {
    res->writeHeader("Content-Type", "application/json");
    res->writeHeader("Cache-Control", "no-cache");
    res->writeHeader("Cache-Control", "no-store");
    res->writeHeader("Pragma", "no-cache");
    res->end(json.str());
  }
  else
  {
    return http_not_implemented(res);
  }
}

struct MolecularStream
{
  bool first;
  bool compress;
  uWS::HttpResponse<false> *res;
  z_stream z;
  unsigned char out[CHUNK];
  FILE *fp;
};

static int sqlite_callback(void *userp, int argc, char **argv,
                           char **azColName)
{
  MolecularStream *stream = (MolecularStream *)userp;
  // static long counter = 0;
  // printf("sqlite_callback: %ld, argc: %d\n", counter++, argc);

  if (argc == 8)
  {
    /*printf("sqlite_callback::molecule:\t");
      for (int i = 0; i < argc; i++)
      printf("%s:%s\t", azColName[i], argv[i]);
      printf("\n");*/

    std::string json;

    if (stream->first)
    {
      stream->first = false;
      stream->res->writeHeader("Content-Type", "application/json");

      if (stream->compress)
        stream->res->writeHeader("Content-Encoding", "gzip");

      stream->res->writeHeader("Cache-Control", "no-cache");
      stream->res->writeHeader("Cache-Control", "no-store");
      stream->res->writeHeader("Pragma", "no-cache");

      json = "{\"molecules\" : [";
    }
    else
      json = ",";

    // json-encode a spectral line
    char *encoded;

    // species
    encoded = json_encode_string(check_null(argv[0]));
    json += "{\"species\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // name
    encoded = json_encode_string(check_null(argv[1]));
    json += "\"name\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // frequency
    json += "\"frequency\" : " + std::string(check_null(argv[2])) + ",";

    // quantum numbers
    encoded = json_encode_string(check_null(argv[3]));
    json += "\"quantum\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // cdms_intensity
    encoded = json_encode_string(check_null(argv[4]));
    json += "\"cdms\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // lovas_intensity
    encoded = json_encode_string(check_null(argv[5]));
    json += "\"lovas\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // E_L
    encoded = json_encode_string(check_null(argv[6]));
    json += "\"E_L\" : " + std::string(check_null(encoded)) + ",";
    if (encoded != NULL)
      free(encoded);

    // linelist
    encoded = json_encode_string(check_null(argv[7]));
    json += "\"list\" : " + std::string(check_null(encoded)) + "}";
    if (encoded != NULL)
      free(encoded);

    // printf("%s\n", json.c_str());

    if (stream->compress)
    {
      stream->z.avail_in = json.length();                // size of input
      stream->z.next_in = (unsigned char *)json.c_str(); // input char array

      do
      {
        stream->z.avail_out = CHUNK;      // size of output
        stream->z.next_out = stream->out; // output char array
        CALL_ZLIB(deflate(&stream->z, Z_NO_FLUSH));
        size_t have = CHUNK - stream->z.avail_out;

        if (have > 0)
        {
          // printf("ZLIB avail_out: %zu\n", have);
          if (stream->fp != NULL)
            fwrite((const char *)stream->out, sizeof(char), have, stream->fp);

          stream->res->write(std::string_view((const char *)stream->out, have));
        }
      } while (stream->z.avail_out == 0);
    }
    else
      stream->res->write(json);
  }

  return 0;
}

inline float get_screen_scale(int x)
{
  // return Math.floor(0.925*x) ;
  return floorf(0.9f * float(x));
}

inline float get_image_scale_square(int width, int height, int img_width,
                                    int img_height)
{
  float screen_dimension = get_screen_scale(MIN(width, height));
  float image_dimension = MAX(img_width, img_height);

  return screen_dimension / image_dimension;
}

inline float get_image_scale(int width, int height, int img_width,
                             int img_height)
{
  if (img_width == img_height)
    return get_image_scale_square(width, height, img_width, img_height);

  if (img_height < img_width)
  {
    float screen_dimension = 0.9f * float(height);
    float image_dimension = img_height;

    float scale = screen_dimension / image_dimension;

    float new_image_width = scale * img_width;

    if (new_image_width > 0.8f * float(width))
    {
      screen_dimension = 0.8f * float(width);
      image_dimension = img_width;
      scale = screen_dimension / image_dimension;
    }

    return scale;
  }

  if (img_width < img_height)
  {
    float screen_dimension = 0.8f * float(width);
    float image_dimension = img_width;

    float scale = screen_dimension / image_dimension;

    float new_image_height = scale * img_height;

    if (new_image_height > 0.9f * float(height))
    {
      screen_dimension = 0.9f * float(height);
      image_dimension = img_height;
      scale = screen_dimension / image_dimension;
    }

    return scale;
  }

  return 1.0f;
}

void true_image_dimensions(Ipp8u *alpha, long &width, long &height)
{
  long x1 = 0;
  long x2 = 0;
  long y1 = 0;
  long y2 = 0;

  long x, y;
  bool found_data;

  long linesize = width;
  size_t length = width * height;

  // find y1
  for (size_t i = 0; i < length; i++)
  {
    if (alpha[i] > 0)
    {
      y1 = (i / linesize);
      break;
    }
  }

  // find y2
  for (size_t i = length - 1; i >= 0; i--)
  {
    if (alpha[i] > 0)
    {
      y2 = (i / linesize);
      break;
    }
  }

  // find x1
  found_data = false;
  for (x = 0; x < width; x++)
  {
    for (y = y1; y <= y2; y++)
    {
      if (alpha[y * linesize + x] > 0)
      {
        x1 = x;
        found_data = true;
        break;
      }
    }

    if (found_data)
      break;
  }

  // find x2
  found_data = false;
  for (x = (width - 1); x >= 0; x--)
  {
    for (y = y1; y <= y2; y++)
    {
      if (alpha[y * linesize + x] > 0)
      {
        x2 = x;
        found_data = true;
        break;
      }
    }

    if (found_data)
      break;
  }

  printf("image bounding box:\tx1 = %ld, x2 = %ld, y1 = %ld, y2 = %ld\n", x1,
         x2, y1, y2);

  width = labs(x2 - x1) + 1;
  height = labs(y2 - y1) + 1;
}

void stream_image_spectrum(uWS::HttpResponse<false> *res,
                           std::shared_ptr<FITS> fits, int _width, int _height,
                           float compression_level, bool fetch_data,
                           std::shared_ptr<std::atomic<bool>> aborted)
{
  if (*aborted.get() == true)
  {
    printf("[stream_image_spectrum] aborted http connection detected.\n");
    return;
  }

  // in-memory output
  StdOSStream oss;
  std::string output;

  // calculate a new image size
  long true_width = fits->width;
  long true_height = fits->height;
  true_image_dimensions(fits->img_mask.get(), true_width, true_height);
  float scale = get_image_scale(_width, _height, true_width, true_height);

  if (scale < 1.0)
  {
    int img_width = floorf(scale * fits->width);
    int img_height = floorf(scale * fits->height);

    printf("FITS image scaling by %f; %ld x %ld --> %d x %d\n", scale,
           fits->width, fits->height, img_width, img_height);

    size_t plane_size = size_t(img_width) * size_t(img_height);

    // allocate {pixel_buf, mask_buf}
    std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size), ippsFree);
    std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size), ippsFree);
    std::shared_ptr<Ipp32f> mask_buf_32f(ippsMalloc_32f_L(plane_size),
                                         ippsFree);

    if (pixels_buf.get() != NULL && mask_buf.get() != NULL &&
        mask_buf_32f.get() != NULL)
    {
      // downsize float32 pixels and a mask
      IppiSize srcSize;
      srcSize.width = fits->width;
      srcSize.height = fits->height;
      Ipp32s srcStep = srcSize.width;

      IppiSize dstSize;
      dstSize.width = img_width;
      dstSize.height = img_height;
      Ipp32s dstStep = dstSize.width;

      IppStatus pixels_stat =
          tileResize32f_C1R(fits->img_pixels.get(), srcSize, srcStep,
                            pixels_buf.get(), dstSize, dstStep);

      IppStatus mask_stat =
          tileResize8u_C1R(fits->img_mask.get(), srcSize, srcStep,
                           mask_buf.get(), dstSize, dstStep);

      printf(" %d : %s, %d : %s\n", pixels_stat,
             ippGetStatusString(pixels_stat), mask_stat,
             ippGetStatusString(mask_stat));

      // compress the pixels + mask with OpenEXR
      if (pixels_stat == ippStsNoErr && mask_stat == ippStsNoErr)
      {
        // the mask should be filled-in manually based on NaN pixels
        // not anymore, NaN will be replaced by 0.0 due to unwanted cropping
        // by OpenEXR
        Ipp32f *pixels = pixels_buf.get();
        Ipp8u *src_mask = mask_buf.get();
        Ipp32f *mask = mask_buf_32f.get();

#pragma omp parallel for simd
        for (size_t i = 0; i < plane_size; i++)
          mask[i] = (src_mask[i] == 255)
                        ? 1.0f
                        : 0.0f; // std::isnan(pixels[i]) ? 0.0f : 1.0f;

        // export EXR in a YA format
        std::string filename =
            FITSCACHE + std::string("/") +
            boost::replace_all_copy(fits->dataset_id, "/", "_") +
            std::string("_resize.exr");

        try
        {
          Header header(img_width, img_height);
          header.compression() = DWAB_COMPRESSION;
          addDwaCompressionLevel(header, compression_level);
          header.channels().insert("Y", Channel(FLOAT));
          header.channels().insert("A", Channel(FLOAT));

          // OutputFile file(filename.c_str(), header);
          OutputFile file(oss, header);
          FrameBuffer frameBuffer;

          frameBuffer.insert("Y",
                             Slice(FLOAT, (char *)pixels, sizeof(Ipp32f) * 1,
                                   sizeof(Ipp32f) * img_width));

          frameBuffer.insert("A", Slice(FLOAT, (char *)mask, sizeof(Ipp32f) * 1,
                                        sizeof(Ipp32f) * img_width));

          file.setFrameBuffer(frameBuffer);
          file.writePixels(img_height);
        }
        catch (const std::exception &exc)
        {
          std::cerr << exc.what() << std::endl;
        }

        output = oss.str();
        std::cout << "[" << fits->dataset_id
                  << "]::downsize OpenEXR output: " << output.length()
                  << " bytes." << std::endl;
      }
    }
  }
  else
  {
    // mirror-flip the pixels_buf, compress with OpenEXR and transmit at
    // its original scale
    int img_width = fits->width;
    int img_height = fits->height;

    size_t plane_size = size_t(img_width) * size_t(img_height);

    // an array to hold a flipped image (its mirror image)
    /*std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
      ippsFree);*/

    // an alpha channel
    std::shared_ptr<Ipp32f> mask_buf(ippsMalloc_32f_L(plane_size), ippsFree);

    // copy and flip the image, fill-in the mask
    if (/*pixels_buf.get() != NULL &&*/ mask_buf.get() != NULL)
    {
      /*tileMirror32f_C1R(fits->img_pixels, pixels_buf.get(), img_width,
        img_height);*/

      // the mask should be filled-in manually based on NaN pixels
      Ipp32f *pixels = fits->img_pixels.get(); // pixels_buf.get();
      Ipp8u *_mask = fits->img_mask.get();
      Ipp32f *mask = mask_buf.get();

#pragma omp parallel for simd
      for (size_t i = 0; i < plane_size; i++)
        mask[i] = (_mask[i] == 255)
                      ? 1.0f
                      : 0.0f; // std::isnan(pixels[i]) ? 0.0f : 1.0f;

      // export the luma+mask to OpenEXR
      std::string filename =
          FITSCACHE + std::string("/") +
          boost::replace_all_copy(fits->dataset_id, "/", "_") +
          std::string("_mirror.exr");

      try
      {
        Header header(img_width, img_height);
        header.compression() = DWAB_COMPRESSION;
        addDwaCompressionLevel(header, compression_level);
        header.channels().insert("Y", Channel(FLOAT));
        header.channels().insert("A", Channel(FLOAT));

        // OutputFile file(filename.c_str(), header);
        OutputFile file(oss, header);
        FrameBuffer frameBuffer;

        frameBuffer.insert("Y", Slice(FLOAT, (char *)pixels, sizeof(Ipp32f) * 1,
                                      sizeof(Ipp32f) * img_width));

        frameBuffer.insert("A", Slice(FLOAT, (char *)mask, sizeof(Ipp32f) * 1,
                                      sizeof(Ipp32f) * img_width));

        file.setFrameBuffer(frameBuffer);
        file.writePixels(img_height);
      }
      catch (const std::exception &exc)
      {
        std::cerr << exc.what() << std::endl;
      }

      output = oss.str();
      std::cout << "[" << fits->dataset_id
                << "]::mirror OpenEXR output: " << output.length() << " bytes."
                << std::endl;
    }
  }

  if (output.length() > 0)
  {
    // send the image data/statistics to the web client
    {
      const char *ptr;

      // send image tone mapping statistics
      float tmp = 0.0f;
      uint32_t str_len = fits->flux.length();
      uint64_t img_len = output.length();

      ptr = (const char *)&str_len;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(str_len)));

      ptr = fits->flux.c_str();
      if (*aborted.get() != true)
        res->write(fits->flux);

      ptr = (const char *)&tmp;

      tmp = fits->min;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      tmp = fits->max;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      tmp = fits->median;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      tmp = fits->sensitivity;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      tmp = fits->ratio_sensitivity;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      tmp = fits->white;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      tmp = fits->black;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(tmp)));

      ptr = (const char *)&img_len;
      if (*aborted.get() != true)
        res->write(std::string_view(ptr, sizeof(img_len)));

      if (*aborted.get() != true)
        //res->write(output);
        send_chunk(res, (const char *)output.c_str(), output.length(), aborted);
    }

    // add compressed FITS data, a spectrum and a histogram
    if (fetch_data)
    {
      std::ostringstream json;
      fits->to_json(json);

      // LZ4-compress json
      Ipp8u *json_lz4 = NULL;
      uint32_t json_size = json.tellp();
      uint32_t compressed_size = 0;

      // LZ4-compress json data
      int worst_size = LZ4_compressBound(json_size);
      json_lz4 = ippsMalloc_8u_L(worst_size);

      if (json_lz4 != NULL)
      {
        // compress the header with LZ4
        compressed_size =
            LZ4_compress_HC((const char *)json.str().c_str(), (char *)json_lz4,
                            json_size, worst_size, LZ4HC_CLEVEL_MAX);

        printf("FITS::JSON size %d, LZ4-compressed: %d bytes.\n", json_size,
               compressed_size);

        // append json to the transmission queue

        const char *ptr = (const char *)&json_size;
        if (*aborted.get() != true)
          //res->write(std::string_view(ptr, sizeof(json_size)));
          send_chunk(res, (const char *)ptr, sizeof(json_size), aborted);

        if (*aborted.get() != true)
          //res->write(std::string_view((const char *)json_lz4, compressed_size));
          send_chunk(res, (const char *)json_lz4, compressed_size, aborted);

        ippsFree(json_lz4);
      }
    }
  }

  // end of chunked encoding
  if (*aborted.get() != true)
    res->end();
}

void stream_partial_fits(uWS::HttpResponse<false> *res, std::shared_ptr<FITS> fits, int px1, int px2, int py1, int py2, int start, int end, std::shared_ptr<std::atomic<bool>> aborted)
{
  int x1, x2, y1, y2;
  int length;

  if (*aborted.get() == true)
  {
    printf("[stream_partial_fits] aborted http connection detected.\n");
    return;
  }

  // sanity checks
  if (fits->bitpix != -32)
    goto jmp;

  if (fits->hdr_len == 0 || fits->header == NULL)
    goto jmp;

  if ((end < 0) || (start < 0) || (end > fits->depth - 1) || (start > fits->depth - 1))
    goto jmp;

  if (end < start)
  {
    int tmp = start;
    start = end;
    end = tmp;
  };

  // passed the sanity checks
  length = end - start + 1;

  if (px2 >= px1)
  {
    x1 = px1;
    x2 = px2;
  }
  else
  {
    x1 = px2;
    x2 = px1;
  };

  if (py2 >= py1)
  {
    y1 = py1;
    y2 = py2;
  }
  else
  {
    y1 = py2;
    y2 = py1;
  };

  x1 = MAX(0, x1);
  y1 = MAX(0, y1);
  x2 = MIN(fits->width, x2);
  y2 = MIN(fits->height, y2);

  std::cout << "[get_fits] " << fits->dataset_id << "\t" << x1 << "\t" << y1 << "\t" << x2 << "\t" << y2 << "\t" << start << "\t" << end << "\t" << length << std::endl;

  // stream FITS data plane-by-plane
  {
    int naxis = 4;
    long naxes[4];

    size_t partial_width = abs(x2 - x1);
    size_t partial_height = abs(y2 - y1);
    size_t partial_depth = length;
    size_t partial_size = partial_height * partial_width;

    naxes[0] = partial_width;
    naxes[1] = partial_height;
    naxes[2] = partial_depth;
    naxes[3] = 1;

    //hdu memory size = partial_size * partial_depth * (|bitpix|/8) rounded up to be a multiple of 2880
    //required memory = FITS Header Size + HDU Size
    size_t partial_data_size = partial_height * partial_width * partial_depth * abs(fits->bitpix / 8);
    size_t no_units = partial_data_size / FITS_CHUNK_LENGTH;

    if (partial_data_size % FITS_CHUNK_LENGTH > 0)
      no_units++;

    size_t required_memory = fits->hdr_len + no_units * FITS_CHUNK_LENGTH;

    printf("required memory for a partial download: %zu\n", required_memory);

    res->writeHeader("Cache-Control", "no-cache");
    res->writeHeader("Cache-Control", "no-store");
    res->writeHeader("Pragma", "no-cache");

    std::string filename = "attachment; filename=" + fits->dataset_id + "-subregion.fits ";
    res->writeHeader("Content-Type", "application/force-download");
    res->writeHeader("Content-Disposition", filename);
    res->writeHeader("Content-Transfer-Encoding", "binary");
    res->writeHeader("Accept-Ranges", "bytes");
    res->writeHeader("Content-Range", "bytes 0-" + std::to_string(required_memory - 1) + "/" + std::to_string(required_memory));

    char *header = (char *)malloc(fits->hdr_len);

    if (header != NULL)
    {
      memcpy(header, fits->header, fits->hdr_len);

      //parse the header
      size_t offset = 0;

      float crpix1 = 0.0f;
      float crpix2 = 0.0f;
      float crpix3 = 0.0f;

      int no_hu = fits->hdr_len / FITS_CHUNK_LENGTH;

      for (int i = 0; i < no_hu; i++)
      {
        for (offset = i * FITS_CHUNK_LENGTH; offset < (i + 1) * FITS_CHUNK_LENGTH; offset += FITS_LINE_LENGTH)
        {
          char *hdrLine = header + offset;

          if (strncmp(hdrLine, "NAXIS1  = ", 10) == 0)
            hdr_set_long_value(hdrLine + 10, naxes[0]);

          if (strncmp(hdrLine, "NAXIS2  = ", 10) == 0)
            hdr_set_long_value(hdrLine + 10, naxes[1]);

          if (strncmp(hdrLine, "NAXIS3  = ", 10) == 0)
            hdr_set_long_value(hdrLine + 10, naxes[2]);

          if (strncmp(hdrLine, "NAXIS4  = ", 10) == 0)
            hdr_set_long_value(hdrLine + 10, naxes[3]);

          if (strncmp(hdrLine, "CRPIX1  = ", 10) == 0)
          {
            crpix1 = hdr_get_double_value(hdrLine + 10);
            hdr_set_double_value(hdrLine + 10, crpix1 - float(x1));
          };

          if (strncmp(hdrLine, "CRPIX2  = ", 10) == 0)
          {
            crpix2 = hdr_get_double_value(hdrLine + 10);
            hdr_set_double_value(hdrLine + 10, crpix2 - float(y1));
          };

          if (strncmp(hdrLine, "CRPIX3  = ", 10) == 0)
          {
            crpix3 = hdr_get_double_value(hdrLine + 10);
            hdr_set_double_value(hdrLine + 10, crpix3 - float(start));
          };
        };
      }

      // send the modified FITS header in one chunk
      if (*aborted.get() != true)
        //res->write(std::string_view((const char *)header, fits->hdr_len));
        send_chunk(res, (const char *)header, fits->hdr_len, aborted);

      free(header);
    }

    // now the 2D planes
    const size_t plane_size = fits->width * fits->height;
    const size_t frame_size = plane_size * abs(fits->bitpix / 8);

    auto pixels = std::shared_ptr<Ipp32f>(ippsMalloc_32f_L(plane_size), [=](Ipp32f *ptr) {
      if (ptr != NULL)
        ippsFree(ptr);
    });

    if (!pixels)
      goto jmp;

    auto dest = std::shared_ptr<Ipp32f>(ippsMalloc_32f_L(partial_size), [=](Ipp32f *ptr) {
      if (ptr != NULL)
        ippsFree(ptr);
    });

    if (!dest)
      goto jmp;

    Ipp32f *pixels_buf = pixels.get();
    Ipp32f *dest_buf = dest.get();

    if (fits->fits_file_desc != -1)
    {
      size_t offset = fits->hdr_len;
      std::cout << "cutting out data from an uncompressed FITS file" << std::endl;

      for (size_t frame = start; frame <= end; frame++)
      {
        //std::cout << "slicing through frame " << frame << std::endl;
        ssize_t bytes_read = 0;

        if (pixels_buf != nullptr)
          bytes_read = pread(fits->fits_file_desc, pixels_buf, frame_size, offset + frame_size * frame);

        if (bytes_read != frame_size)
        {
          fprintf(stderr,
                  "%s::<frame::%zu>::CRITICAL: only read %zd out of requested "
                  "%zd bytes.\n",
                  fits->dataset_id.c_str(), frame, bytes_read, frame_size);
          goto jmp;
        }

        // copy the partial data to the destination buffer
        size_t dst_offset = 0;
        for (int y = y1; y < y2; y++)
        //for (int y = y2 - 1; y >= y1; y--) // invert the Y-axis
        {
#pragma simd
          for (int x = x1; x < x2; x++)
            //((uint32_t *)dest_buf)[(y - y1) * partial_width + (x - x1)] = ((uint32_t *)pixels_buf)[y * fits->width + x];
            ((uint32_t *)dest_buf)[dst_offset++] = ((uint32_t *)pixels_buf)[y * fits->width + x];
        };

        // send the chunk
        if (*aborted.get() != true)
          send_chunk(res, (const char *)dest_buf, partial_size * sizeof(Ipp32f), aborted);
        else
          break;
      }
    }

    if (fits->compressed_fits_stream != NULL)
    {
      size_t offset = fits->hdr_len + start * plane_size;
      std::cout << "cutting out data from a gz-compressed FITS file" << std::endl;

      // lock the file mutex at the beginning and keep it locked throughout
      std::lock_guard<std::mutex> guard(fits->file_mtx);

      // preload the initial <offset> number of bytes from the compressed stream
      gzseek(fits->compressed_fits_stream, offset, SEEK_SET);

      for (size_t frame = start; frame <= end; frame++)
      {
        //std::cout << "slicing through frame " << frame << std::endl;
        ssize_t bytes_read = 0;

        if (pixels_buf != NULL)
          bytes_read = gzread(fits->compressed_fits_stream, pixels_buf, frame_size);

        if (bytes_read != frame_size)
        {
          fprintf(stderr,
                  "%s::CRITICAL: read less than %zd bytes from the FITS "
                  "data unit\n",
                  fits->dataset_id.c_str(), bytes_read);
          goto jmp;
        }

        // copy the partial data to the destination buffer
        size_t dst_offset = 0;
        for (int y = y1; y < y2; y++)
        //for (int y = y2 - 1; y >= y1; y--) // invert the Y-axis
        {
#pragma simd
          for (int x = x1; x < x2; x++)
            //((uint32_t *)dest_buf)[(y - y1) * partial_width + (x - x1)] = ((uint32_t *)pixels_buf)[y * fits->width + x];
            ((uint32_t *)dest_buf)[dst_offset++] = ((uint32_t *)pixels_buf)[y * fits->width + x];
        };

        // send the chunk
        if (*aborted.get() != true)
          send_chunk(res, (const char *)dest_buf, partial_size * sizeof(Ipp32f), aborted);
        else
          break;
      }
    }

    // send the 2880-bytes '\0' padding that might be needed
    size_t padding_size = required_memory - (fits->hdr_len + partial_data_size);

    if (padding_size > 0)
    {
      std::cout << "padding the FITS file with " << padding_size << " bytes." << std::endl;
      char *padding = (char *)malloc(padding_size);

      if (padding != NULL)
      {
        memset(padding, '\0', padding_size);

        if (*aborted.get() != true)
          //res->write(std::string_view((const char *)padding, padding_size));
          send_chunk(res, (const char *)padding, padding_size, aborted);

        free(padding);
      }
    }
  }

jmp:
  // end of chunked encoding
  if (*aborted.get() != true)
    res->end();
}

void stream_molecules(uWS::HttpResponse<false> *res, double freq_start,
                      double freq_end, bool compress,
                      std::shared_ptr<std::atomic<bool>> aborted)
{
  if (*aborted.get() == true)
  {
    printf("[stream_molecules] aborted http connection detected.\n");
    return;
  }

  if (splat_db == NULL)
    return http_internal_server_error(res);

  char strSQL[256];
  int rc;
  char *zErrMsg = 0;

  snprintf(strSQL, 256,
           "SELECT * FROM lines WHERE frequency>=%f AND frequency<=%f;",
           freq_start, freq_end);
  printf("%s\n", strSQL);

  struct MolecularStream stream;
  stream.first = true;
  stream.compress = compress;
  stream.res = res;
  stream.fp = NULL; // fopen("molecules.txt.gz", "w");

  if (compress)
  {
    stream.z.zalloc = Z_NULL;
    stream.z.zfree = Z_NULL;
    stream.z.opaque = Z_NULL;
    stream.z.next_in = Z_NULL;
    stream.z.avail_in = 0;

    CALL_ZLIB(deflateInit2(&stream.z, Z_BEST_COMPRESSION, Z_DEFLATED,
                           _windowBits | GZIP_ENCODING, 9, Z_DEFAULT_STRATEGY));
  }

  rc = sqlite3_exec(splat_db, strSQL, sqlite_callback, &stream, &zErrMsg);

  if (rc != SQLITE_OK)
  {
    fprintf(stderr, "SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
    return http_internal_server_error(res);
  }

  std::string chunk_data;

  if (stream.first)
    chunk_data = "{\"molecules\" : []}";
  else
    chunk_data = "]}";

  if (compress)
  {
    stream.z.avail_in = chunk_data.length();
    stream.z.next_in = (unsigned char *)chunk_data.c_str();

    do
    {
      stream.z.avail_out = CHUNK;     // size of output
      stream.z.next_out = stream.out; // output char array
      CALL_ZLIB(deflate(&stream.z, Z_FINISH));
      size_t have = CHUNK - stream.z.avail_out;

      if (have > 0)
      {
        // printf("Z_FINISH avail_out: %zu\n", have);
        if (stream.fp != NULL)
          fwrite((const char *)stream.out, sizeof(char), have, stream.fp);

        if (*aborted.get() != true)
          //stream.res->write(std::string_view((const char *)stream.out, have));
          send_chunk(stream.res, (const char *)stream.out, have, aborted);
      }
    } while (stream.z.avail_out == 0);

    CALL_ZLIB(deflateEnd(&stream.z));

    if (stream.fp != NULL)
      fclose(stream.fp);
  }
  else if (*aborted.get() != true)
    //res->write(chunk_data);
    send_chunk(res, (const char *)chunk_data.c_str(), chunk_data.length(), aborted);

  // end of chunked encoding
  if (*aborted.get() != true)
    res->end();
}

void get_directory(uWS::HttpResponse<false> *res, std::string dir)
{
  printf("get_directory(%s)\n", dir.c_str());

  struct dirent **namelist = NULL;
  int i, n;

  n = scandir(dir.c_str(), &namelist, 0, alphasort);

  std::ostringstream json;

  char *encoded = json_encode_string(dir.c_str());

  json << "{\"location\" : " << check_null(encoded) << ", \"contents\" : [";

  if (encoded != NULL)
    free(encoded);

  bool has_contents = false;

  if (n < 0)
  {
    perror("scandir");

    json << "]}";
  }
  else
  {
    for (i = 0; i < n; i++)
    {
      // printf("%s\n", namelist[i]->d_name);

      char pathname[1024];

      sprintf(pathname, "%s/%s", dir.c_str(), check_null(namelist[i]->d_name));

      struct stat64 sbuf;

      int err = stat64(pathname, &sbuf);

      if (err == 0)
      {
        char last_modified[255];

        struct tm lm;
        localtime_r(&sbuf.st_mtime, &lm);
        strftime(last_modified, sizeof(last_modified) - 1,
                 "%a, %d %b %Y %H:%M:%S %Z", &lm);

        size_t filesize = sbuf.st_size;

        if (S_ISDIR(sbuf.st_mode) && namelist[i]->d_name[0] != '.')
        {
          char *encoded = json_encode_string(check_null(namelist[i]->d_name));

          json << "{\"type\" : \"dir\", \"name\" : " << check_null(encoded)
               << ", \"last_modified\" : \"" << last_modified << "\"},";
          has_contents = true;

          if (encoded != NULL)
            free(encoded);
        }

        if (S_ISREG(sbuf.st_mode))
        {
          const std::string filename = std::string(namelist[i]->d_name);
          const std::string lower_filename =
              boost::algorithm::to_lower_copy(filename);

          // if(!strcasecmp(get_filename_ext(check_null(namelist[i]->d_name)),
          // "fits"))
          if (boost::algorithm::ends_with(lower_filename, ".fits") ||
              boost::algorithm::ends_with(lower_filename, ".fits.gz"))
          {
            char *encoded = json_encode_string(check_null(namelist[i]->d_name));

            json << "{\"type\" : \"file\", \"name\" : " << check_null(encoded)
                 << ", \"size\" : " << filesize << ", \"last_modified\" : \""
                 << last_modified << "\"},";
            has_contents = true;

            if (encoded != NULL)
              free(encoded);
          }
        }
      }
      else
        perror("stat64");

      free(namelist[i]);
    };

    // overwrite the the last ',' with a list closing character
    if (has_contents)
      json.seekp(-1, std::ios_base::end);

    json << "]}";
  };

  if (namelist != NULL)
    free(namelist);

  res->writeHeader("Content-Type", "application/json");
  res->writeHeader("Cache-Control", "no-cache");
  res->writeHeader("Cache-Control", "no-store");
  res->writeHeader("Pragma", "no-cache");
  res->end(json.str());
}

void get_home_directory(uWS::HttpResponse<false> *res)
{
  if (home_dir != "")
    return get_directory(res, home_dir);
  else
    return http_not_found(res);
}

void include_file(std::string &html, std::string filename)
{
  int fd = -1;
  void *buffer = NULL;

  struct stat64 st;
  stat64(filename.c_str(), &st);
  long size = st.st_size;

  fd = open(filename.c_str(), O_RDONLY);
  if (fd != -1)
  {
    buffer = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (buffer != MAP_FAILED)
    {
      html.append((const char *)buffer, size);

      if (munmap(buffer, size) == -1)
        perror("un-mapping error");
    }
    else
      perror("error mapping a file");

    close(fd);
  };
}

void serve_file(uWS::HttpResponse<false> *res, std::string uri)
{
  std::string resource;

  // strip the leading '/'
  if (uri[0] == '/')
    resource = uri.substr(1);
  else
    resource = uri;

  // strip '?' from the requested file name
  size_t pos = resource.find("?");

  if (pos != std::string::npos)
    resource = resource.substr(0, pos);

  std::cout << "serving " << resource << std::endl;

  // mmap a disk resource
  int fd = -1;
  void *buffer = NULL;

  struct stat64 st;
  stat64(resource.c_str(), &st);
  long size = st.st_size;

  fd = open(resource.c_str(), O_RDONLY);

  if (fd != -1)
  {
    buffer = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (buffer != MAP_FAILED)
    {
      // detect mime-types
      size_t pos = resource.find_last_of(".");

      if (pos != std::string::npos)
      {
        std::string ext = resource.substr(pos + 1, std::string::npos);

        if (ext == "htm" || ext == "html")
          res->writeHeader("Content-Type", "text/html");

        if (ext == "txt")
          res->writeHeader("Content-Type", "text/plain");

        if (ext == "js")
          res->writeHeader("Content-Type", "application/javascript");

        if (ext == "ico")
          res->writeHeader("Content-Type", "image/x-icon");

        if (ext == "png")
          res->writeHeader("Content-Type", "image/png");

        if (ext == "gif")
          res->writeHeader("Content-Type", "image/gif");

        if (ext == "webp")
          res->writeHeader("Content-Type", "image/webp");

        if (ext == "jpg" || ext == "jpeg")
          res->writeHeader("Content-Type", "image/jpeg");

        if (ext == "bpg")
          res->writeHeader("Content-Type", "image/bpg");

        if (ext == "mp4")
          res->writeHeader("Content-Type", "video/mp4");

        if (ext == "hevc")
          res->writeHeader("Content-Type", "video/hevc");

        if (ext == "css")
          res->writeHeader("Content-Type", "text/css");

        if (ext == "pdf")
          res->writeHeader("Content-Type", "application/pdf");

        if (ext == "svg")
          res->writeHeader("Content-Type", "image/svg+xml");

        if (ext == "wasm")
          res->writeHeader("Content-Type", "application/wasm");
      }

      res->end(std::string_view((const char *)buffer, size));

      if (munmap(buffer, size) == -1)
        perror("un-mapping error");
    }
    else
    {
      perror("error mapping a file");
      http_not_found(res);
    }

    close(fd);
  }
  else
    http_not_found(res);
}

void http_fits_response(uWS::HttpResponse<false> *res, std::string root,
                        std::vector<std::string> datasets, std::string url, bool composite,
                        bool has_fits)
{
  int va_count = url.empty() ? datasets.size() : 1;

  std::string html =
      "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
  html.append(
      "<link href=\"https://fonts.googleapis.com/css?family=Inconsolata\" "
      "rel=\"stylesheet\"/>\n");
  html.append(
      "<link href=\"https://fonts.googleapis.com/css?family=Material+Icons\" "
      "rel=\"stylesheet\"/>\n");
  html.append("<script src=\"https://d3js.org/d3.v5.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/reconnecting-websocket.min.js\"></script>\n");
  html.append("<script "
              "src=\"//cdnjs.cloudflare.com/ajax/libs/numeral.js/2.0.6/"
              "numeral.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/ra_dec_conversion.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/sylvester.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/shortcut.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/colourmaps.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/lz4.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/marchingsquares-isocontours.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/marchingsquares-isobands.min.js\"></script>\n");

  // hevc wasm decoder
  /*html.append("<script "
    "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
    "fitswebql/hevc_" WASM_STRING ".js\"></script>\n");
    html.append(R"(<script>
    Module.onRuntimeInitialized = async _ => {
    api = {
    hevc_init: Module.cwrap('hevc_init', '', []),
    hevc_destroy: Module.cwrap('hevc_destroy', '', []),
    hevc_decode_nal_unit: Module.cwrap('hevc_decode_nal_unit',
    'number', ['number', 'number', 'number', 'number', 'number', 'number',
    'number', 'string']),
    };
    };
    </script>)");*/

  // OpenEXR WASM decoder
  html.append("<script "
              "src=\"exr." WASM_VERSION ".js\"></script>\n");
  /*html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/FITSWebQL@master/" +
              docs_root +
              "/"
              "fitswebql/exr." WASM_VERSION ".min.js\"></script>\n");*/
  html.append(R"(
    <script>
    Module.ready
      .then( status => console.log( status ))
      .catch(e => console.error(e)) 
  </script>
  )");

  // bootstrap
  html.append(
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, "
      "user-scalable=no, minimum-scale=1, maximum-scale=1\">\n");
  html.append("<link rel=\"stylesheet\" "
              "href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/"
              "bootstrap.min.css\">\n");
  html.append("<script "
              "src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/"
              "jquery.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/"
              "bootstrap.min.js\"></script>\n");

  // GLSL vertex shader
  html.append("<script id=\"vertex-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/vertex-shader.vert");
  html.append("</script>\n");

  html.append(
      "<script id=\"legend-vertex-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/legend-vertex-shader.vert");
  html.append("</script>\n");

  // GLSL fragment shaders
  html.append("<script id=\"common-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/common-shader.frag");
  html.append("</script>\n");

  html.append(
      "<script id=\"legend-common-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/legend-common-shader.frag");
  html.append("</script>\n");

  // tone mappings
  html.append("<script id=\"ratio-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/ratio-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"logistic-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/logistic-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"square-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/square-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"legacy-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/legacy-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"linear-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/linear-shader.frag");
  html.append("</script>\n");

  // colourmaps
  html.append("<script id=\"greyscale-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/greyscale-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"negative-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/negative-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"red-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/red-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"green-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/green-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"blue-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/blue-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"hot-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/hot-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"rainbow-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/rainbow-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"parula-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/parula-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"inferno-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/inferno-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"magma-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/magma-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"plasma-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/plasma-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"viridis-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/viridis-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"cubehelix-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/cubehelix-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"jet-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/jet-shader.frag");
  html.append("</script>\n");

  html.append("<script id=\"haxby-shader\" type=\"x-shader/x-vertex\">\n");
  include_file(html, docs_root + "/fitswebql/haxby-shader.frag");
  html.append("</script>\n");

  // FITSWebQL main JavaScript + CSS
  html.append("<script src=\"fitswebqlse.js?" VERSION_STRING "\"></script>\n");
  html.append("<link rel=\"stylesheet\" href=\"fitswebqlse.css?" VERSION_STRING
              "\"/>\n");

  // HTML content
  html.append("<title>FITSWebQL</title></head><body>\n");
  html.append("<div id='votable' style='width: 0; height: 0;' data-va_count='" +
              std::to_string(va_count) + "' ");

  if (!url.empty())
  {
    boost::uuids::name_generator_sha1 gen(boost::uuids::ns::url());
    boost::uuids::uuid unique_id = gen(url);
    std::cout << "boost.org uuid in url namespace, sha1 version: " << unique_id << std::endl;
    std::string data_id = boost::lexical_cast<std::string>(unique_id);

    html.append("data-datasetId='" + data_id + "' ");
  }
  else if (datasets.size() == 1)
    html.append("data-datasetId='" + datasets[0] + "' ");
  else
  {
    for (unsigned int i = 0; i < datasets.size(); i++)
      html.append("data-datasetId" + std::to_string(i + 1) + "='" +
                  datasets[i] + "' ");

    if (composite && datasets.size() <= 3)
      html.append("data-composite='1' ");
  }

  html.append("data-root-path='/" + root + "/' data-server-version='" +
              VERSION_STRING + "' data-server-string='" + SERVER_STRING +
#ifdef LOCAL
              "' data-server-mode='" + "LOCAL" +
#else
              "' data-server-mode='" + "SERVER" +
#endif
              "' data-has-fits='" + std::to_string(has_fits) + "'></div>\n");

#ifdef PRODUCTION
  html.append(R"(<script>
        var WS_SOCKET = 'wss://';
        </script>)");
#else
  html.append(R"(<script>
        var WS_SOCKET = 'ws://';
        </script>)");
#endif

  // the page entry point
  html.append(R"(<script>
        const golden_ratio = 1.6180339887;
        var ALMAWS = null ;
        var wsVideo = null ;
        var wsConn = null ;
        var firstTime = true ;
        var has_image = false ;         
        var PROGRESS_VARIABLE = 0.0 ;
        var PROGRESS_INFO = '' ;      
        var RESTFRQ = 0.0 ;
        var USER_SELFRQ = 0.0 ;
        var USER_DELTAV = 0.0 ;
        var ROOT_PATH = '/fitswebql/' ;
        var idleResize = -1;
        window.onresize = resizeMe;
        window.onbeforeunload = function() {            
            if(wsConn != null)
            {
                for(let i=0;i<va_count;i++)
                    wsConn[i].close();
            }

            if(wsVideo != null)
                wsVideo.close();
        };
        mainRenderer();
    </script>)");

  html.append("</body></html>");

  res->end(html);
}

#ifndef LOCAL
PGconn *jvo_db_connect(std::string db)
{
  PGconn *jvo_db = NULL;

  std::string conn_str =
      "dbname=" + db + " host=" + JVO_HOST + " user=" + JVO_USER;

  jvo_db = PQconnectdb(conn_str.c_str());

  if (PQstatus(jvo_db) != CONNECTION_OK)
  {
    fprintf(stderr, "PostgreSQL connection failed: %s\n",
            PQerrorMessage(jvo_db));
    PQfinish(jvo_db);
    jvo_db = NULL;
  }
  else
    printf("PostgreSQL connection successful.\n");

  return jvo_db;
}

std::string get_jvo_path(PGconn *jvo_db, std::string db, std::string table,
                         std::string data_id)
{
  std::string path;

  std::string sql_str =
      "SELECT path FROM " + table + " WHERE data_id = '" + data_id + "';";

  PGresult *res = PQexec(jvo_db, sql_str.c_str());
  int status = PQresultStatus(res);

  if (PQresultStatus(res) == PGRES_TUPLES_OK)
  {
    path = std::string(FITSHOME) + "/" + db + "/";

    size_t pos = table.find(".");

    if (pos == std::string::npos)
      path += std::string((const char *)PQgetvalue(res, 0, 0));
    else
      path += boost::algorithm::to_upper_copy(table.substr(0, pos)) + "/" +
              std::string((const char *)PQgetvalue(res, 0, 0));
  }

  PQclear(res);

  return path;
}
#endif

void execute_fits(uWS::HttpResponse<false> *res, std::string root,
                  std::string url, std::string dir, std::string ext, std::string db,
                  std::string table, std::vector<std::string> datasets,
                  bool composite, std::string flux)
{
  bool has_fits = true;
  int va_count = datasets.size();

#ifndef LOCAL
  PGconn *jvo_db = NULL;

  if (db != "")
    jvo_db = jvo_db_connect(db);
#endif

  if (!url.empty())
  {
    // make up a datasetid based on the URL converted into a UUID
    boost::uuids::name_generator_sha1 gen(boost::uuids::ns::url());
    boost::uuids::uuid unique_id = gen(url);
    std::cout << "boost.org uuid in url namespace, sha1 version: " << unique_id << std::endl;
    std::string data_id = boost::lexical_cast<std::string>(unique_id);

    auto item = get_dataset(data_id);

    if (item == nullptr)
    {
      // set has_fits to false and load the FITS dataset
      has_fits = false;
      std::shared_ptr<FITS> fits(new FITS(data_id, flux));

      insert_dataset(data_id, fits);

      // download FITS data from a URL in a separate thread
      std::thread(&FITS::from_url, fits, url, flux, 1).detach();
    }
    else
    {
      has_fits = has_fits && item->has_data;
      item->update_timestamp();
    }
  }
  else
    for (auto const &data_id : datasets)
    {
      auto item = get_dataset(data_id);

      if (item == nullptr)
      {
        // set has_fits to false and load the FITS dataset
        has_fits = false;
        std::shared_ptr<FITS> fits(new FITS(data_id, flux));

        insert_dataset(data_id, fits);

        std::string path;

        if (dir != "" && ext != "")
          path = dir + "/" + data_id + "." + ext;

#ifndef LOCAL
        if (jvo_db != NULL && table != "")
          path = get_jvo_path(jvo_db, db, table, data_id);
#endif

        if (path != "")
        {
          bool is_compressed = is_gzip(path.c_str());
          /*bool is_compressed = false;
          std::string lower_path = boost::algorithm::to_lower_copy(path);
          if (boost::algorithm::ends_with(lower_path, ".gz"))
          is_compressed = is_gzip(path.c_str());*/

          // load FITS data in a separate thread
          std::thread(&FITS::from_path, fits, path, is_compressed, flux, va_count,
                      true)
              .detach();
        }
        else
        {
          // the last resort
          std::string _url = std::string("http://") + JVO_FITS_SERVER +
                             ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB +
                             "&table=cube&data_id=" + data_id + "_00_00_00";

          // download FITS data from a URL in a separate thread
          std::thread(&FITS::from_url, fits, _url, flux, va_count).detach();
        }
      }
      else
      {
        has_fits = has_fits && item->has_data;
        item->update_timestamp();
      }
    }

#ifndef LOCAL
  if (jvo_db != NULL)
    PQfinish(jvo_db);
#endif

  std::cout << "has_fits: " << has_fits << std::endl;

  return http_fits_response(res, root, datasets, url, composite, has_fits);
}

void ipp_init()
{
  const IppLibraryVersion *lib;
  IppStatus status;
  Ipp64u mask, emask;

  /* Init IPP library */
  ippInit();
  /* Get IPP library version info */
  lib = ippGetLibVersion();
  printf("%s %s\n", lib->Name, lib->Version);

  /* Get CPU features and features enabled with selected library level */
  status = ippGetCpuFeatures(&mask, 0);
  if (ippStsNoErr == status)
  {
    emask = ippGetEnabledCpuFeatures();
    printf("Features supported by CPU\tby IPP\n");
    printf("-----------------------------------------\n");
    printf("  ippCPUID_MMX        = ");
    printf("%c\t%c\t", (mask & ippCPUID_MMX) ? 'Y' : 'N',
           (emask & ippCPUID_MMX) ? 'Y' : 'N');
    printf("Intel(R) Architecture MMX technology supported\n");
    printf("  ippCPUID_SSE        = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE) ? 'Y' : 'N',
           (emask & ippCPUID_SSE) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions\n");
    printf("  ippCPUID_SSE2       = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE2) ? 'Y' : 'N',
           (emask & ippCPUID_SSE2) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 2\n");
    printf("  ippCPUID_SSE3       = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE3) ? 'Y' : 'N',
           (emask & ippCPUID_SSE3) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 3\n");
    printf("  ippCPUID_SSSE3      = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSSE3) ? 'Y' : 'N',
           (emask & ippCPUID_SSSE3) ? 'Y' : 'N');
    printf("Intel(R) Supplemental Streaming SIMD Extensions 3\n");
    printf("  ippCPUID_MOVBE      = ");
    printf("%c\t%c\t", (mask & ippCPUID_MOVBE) ? 'Y' : 'N',
           (emask & ippCPUID_MOVBE) ? 'Y' : 'N');
    printf("The processor supports MOVBE instruction\n");
    printf("  ippCPUID_SSE41      = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE41) ? 'Y' : 'N',
           (emask & ippCPUID_SSE41) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 4.1\n");
    printf("  ippCPUID_SSE42      = ");
    printf("%c\t%c\t", (mask & ippCPUID_SSE42) ? 'Y' : 'N',
           (emask & ippCPUID_SSE42) ? 'Y' : 'N');
    printf("Intel(R) Streaming SIMD Extensions 4.2\n");
    printf("  ippCPUID_AVX        = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX) ? 'Y' : 'N',
           (emask & ippCPUID_AVX) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions instruction set\n");
    printf("  ippAVX_ENABLEDBYOS  = ");
    printf("%c\t%c\t", (mask & ippAVX_ENABLEDBYOS) ? 'Y' : 'N',
           (emask & ippAVX_ENABLEDBYOS) ? 'Y' : 'N');
    printf("The operating system supports Intel(R) AVX\n");
    printf("  ippCPUID_AES        = ");
    printf("%c\t%c\t", (mask & ippCPUID_AES) ? 'Y' : 'N',
           (emask & ippCPUID_AES) ? 'Y' : 'N');
    printf("Intel(R) AES instruction\n");
    printf("  ippCPUID_SHA        = ");
    printf("%c\t%c\t", (mask & ippCPUID_SHA) ? 'Y' : 'N',
           (emask & ippCPUID_SHA) ? 'Y' : 'N');
    printf("Intel(R) SHA new instructions\n");
    printf("  ippCPUID_CLMUL      = ");
    printf("%c\t%c\t", (mask & ippCPUID_CLMUL) ? 'Y' : 'N',
           (emask & ippCPUID_CLMUL) ? 'Y' : 'N');
    printf("PCLMULQDQ instruction\n");
    printf("  ippCPUID_RDRAND     = ");
    printf("%c\t%c\t", (mask & ippCPUID_RDRAND) ? 'Y' : 'N',
           (emask & ippCPUID_RDRAND) ? 'Y' : 'N');
    printf("Read Random Number instructions\n");
    printf("  ippCPUID_F16C       = ");
    printf("%c\t%c\t", (mask & ippCPUID_F16C) ? 'Y' : 'N',
           (emask & ippCPUID_F16C) ? 'Y' : 'N');
    printf("Float16 instructions\n");
    printf("  ippCPUID_AVX2       = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX2) ? 'Y' : 'N',
           (emask & ippCPUID_AVX2) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions 2 instruction set\n");
    printf("  ippCPUID_AVX512F    = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX512F) ? 'Y' : 'N',
           (emask & ippCPUID_AVX512F) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions 3.1 instruction set\n");
    printf("  ippCPUID_AVX512CD   = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX512CD) ? 'Y' : 'N',
           (emask & ippCPUID_AVX512CD) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions CD (Conflict Detection) "
           "instruction set\n");
    printf("  ippCPUID_AVX512ER   = ");
    printf("%c\t%c\t", (mask & ippCPUID_AVX512ER) ? 'Y' : 'N',
           (emask & ippCPUID_AVX512ER) ? 'Y' : 'N');
    printf("Intel(R) Advanced Vector Extensions ER instruction set\n");
    printf("  ippCPUID_ADCOX      = ");
    printf("%c\t%c\t", (mask & ippCPUID_ADCOX) ? 'Y' : 'N',
           (emask & ippCPUID_ADCOX) ? 'Y' : 'N');
    printf("ADCX and ADOX instructions\n");
    printf("  ippCPUID_RDSEED     = ");
    printf("%c\t%c\t", (mask & ippCPUID_RDSEED) ? 'Y' : 'N',
           (emask & ippCPUID_RDSEED) ? 'Y' : 'N');
    printf("The RDSEED instruction\n");
    printf("  ippCPUID_PREFETCHW  = ");
    printf("%c\t%c\t", (mask & ippCPUID_PREFETCHW) ? 'Y' : 'N',
           (emask & ippCPUID_PREFETCHW) ? 'Y' : 'N');
    printf("The PREFETCHW instruction\n");
    printf("  ippCPUID_KNC        = ");
    printf("%c\t%c\t", (mask & ippCPUID_KNC) ? 'Y' : 'N',
           (emask & ippCPUID_KNC) ? 'Y' : 'N');
    printf("Intel(R) Xeon Phi(TM) Coprocessor instruction set\n");
  }
}

int main(int argc, char *argv[])
{
#ifdef DEBUG
  system_clock::time_point offset = system_clock::now();
  FILE *fp = fopen("memory_usage.csv", "w");

  if (fp != NULL)
    fprintf(fp,
            "\"elapsed time "
            "[ms]\",\"stats.allocated\",\"stats.active\",\"stats.mapped\"\n");

  // track/log memory usage
  memory_thread = std::thread([offset, fp]() {
    while (!exiting)
    {
      // memory statistics using jemalloc
      uint64_t epoch = 1;
      size_t sz = sizeof(epoch);
      mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
      mallctl("epoch", &epoch, &sz, &epoch, sz);

      size_t allocated, active, mapped;
      sz = sizeof(size_t);
      mallctl("stats.allocated", &allocated, &sz, NULL, 0);
      mallctl("stats.active", &active, &sz, NULL, 0);
      mallctl("stats.mapped", &mapped, &sz, NULL, 0);

      // printf("allocated/active/mapped: %zu/%zu/%zu [MB]\n", allocated / (1024
      // * 1024), active / (1024 * 1024), mapped / (1024 * 1024));

      if (fp != NULL)
      {
        duration<double, std::milli> elapsed = system_clock::now() - offset;
        fprintf(fp, "%f,%zu,%zu,%zu\n", elapsed.count(), allocated, active,
                mapped);
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (fp != NULL)
      fclose(fp);

    printf("memory tracking thread terminated.\n");
  });
#endif

#ifdef CLUSTER
  setenv("ZSYS_SIGHANDLER", "false", 1);
  // LAN cluster node auto-discovery
  beacon_thread = std::thread([]() {
    speaker = zactor_new(zbeacon, NULL);
    if (speaker == NULL)
      return;

    zstr_send(speaker, "VERBOSE");
    zsock_send(speaker, "si", "CONFIGURE", BEACON_PORT);
    char *my_hostname = zstr_recv(speaker);
    if (my_hostname != NULL)
    {
      const char *message = "JVO:>FITSWEBQL::ENTER";
      const int interval = 1000; //[ms]
      zsock_send(speaker, "sbi", "PUBLISH", message, strlen(message), interval);
    }

    listener = zactor_new(zbeacon, NULL);
    if (listener == NULL)
      return;

    zstr_send(listener, "VERBOSE");
    zsock_send(listener, "si", "CONFIGURE", BEACON_PORT);
    char *hostname = zstr_recv(listener);
    if (hostname != NULL)
      free(hostname);
    else
      return;

    zsock_send(listener, "sb", "SUBSCRIBE", "", 0);
    zsock_set_rcvtimeo(listener, 500);

    while (!exiting)
    {
      char *ipaddress = zstr_recv(listener);
      if (ipaddress != NULL)
      {
        zframe_t *content = zframe_recv(listener);
        std::string_view message = std::string_view(
            (const char *)zframe_data(content), zframe_size(content));

        // ENTER
        if (message.find("ENTER") != std::string::npos)
        {
          if (strcmp(my_hostname, ipaddress) != 0)
          {
            std::string node = std::string(ipaddress);

            if (!cluster_contains_node(node))
            {
              PrintThread{} << "found a new peer @ " << ipaddress << ": "
                            << message << std::endl;
              cluster_insert_node(node);
            }
          }
        }

        // LEAVE
        if (message.find("LEAVE") != std::string::npos)
        {
          if (strcmp(my_hostname, ipaddress) != 0)
          {
            std::string node = std::string(ipaddress);

            if (cluster_contains_node(node))
            {
              PrintThread{} << ipaddress << " is leaving: " << message
                            << std::endl;
              cluster_erase_node(node);
            }
          }
        }

        zframe_destroy(&content);
        zstr_free(&ipaddress);
      }
    }

    if (my_hostname != NULL)
      free(my_hostname);
  });
#endif

  ipp_init();
  curl_global_init(CURL_GLOBAL_ALL);

  if (ILMTHREAD_NAMESPACE::supportsThreads())
  {
    int omp_threads = omp_get_max_threads();
    OPENEXR_IMF_NAMESPACE::setGlobalThreadCount(omp_threads);
    std::cout << "[OpenEXR] number of threads: "
              << OPENEXR_IMF_NAMESPACE::globalThreadCount() << std::endl;
  }

  int rc = sqlite3_open_v2("splatalogue_v3.db", &splat_db,
                           SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

  if (rc)
  {
    fprintf(stderr, "Can't open local splatalogue database: %s\n",
            sqlite3_errmsg(splat_db));
    sqlite3_close(splat_db);
    splat_db = NULL;
  }

  struct passwd *passwdEnt = getpwuid(getuid());
  home_dir = passwdEnt->pw_dir;

  // register signal SIGINT and signal handler
  signal(SIGINT, signalHandler);

  // a one-time signal handler
  /*struct sigaction action;
    action.sa_handler = signalHandler;
    action.sa_flags = SA_RESETHAND;

    if (sigaction(SIGINT, &action, NULL) == -1)
    perror("Failed to install signal handler for SIGINT");*/

  // parse local command-line options
  if (argc > 2)
  {
    for (int i = 1; i < argc - 1; i++)
    {
      const char *key = argv[i];
      const char *value = argv[i + 1];

      if (!strcmp(key, "--port"))
        server_port = atoi(value);

      if (!strcmp(key, "--home"))
        home_dir = std::string(value);
    }
  }

  std::cout << SERVER_STRING << " (" << VERSION_STRING << ")" << std::endl;
  std::cout << "Browser URL: http://localhost:" << server_port << std::endl;
  std::cout << "*** To quit FITSWebQL press Ctrl-C from the command-line "
               "terminal or send SIGINT. ***"
            << std::endl;

#if defined(__APPLE__) && defined(__MACH__)
  int no_threads = 1;
#else
  int no_threads = MIN(MAX(std::thread::hardware_concurrency() / 2, 1), 4);
#endif

  std::vector<std::thread *> threads(no_threads);

  std::transform(threads.begin(), threads.end(), threads.begin(), [](std::thread *t) {
    return new std::thread([]() {
      uWS::App()
          .get("/",
               [](auto *res, auto *req) {
                 std::string_view uri = req->getUrl();
                 std::cout << "HTTP request for " << uri << std::endl;

        // root
#ifdef LOCAL
                 return serve_file(res, "htdocs/local.html");
#else
                                                                                        return serve_file(res, "htdocs/test.html");
#endif
               })
          .get("/favicon.ico",
               [](auto *res, auto *req) {
                 return serve_file(res, "htdocs/favicon.ico");
               })
          .get("/htdocs/*",
               [](auto *res, auto *req) {
                 std::string_view uri = req->getUrl();

                 // default handler
                 return serve_file(res, std::string(uri));
               })
          .get("/get_directory",
               [](auto *res, auto *req) {
                 std::string dir;

                 std::string_view query = req->getQuery();

                 std::cout << "query: (" << query << ")" << std::endl;

                 std::vector<std::string> params;
                 boost::split(params, query, [](char c) { return c == '&'; });

                 for (auto const &s : params)
                 {
                   // find '='
                   size_t pos = s.find("=");

                   if (pos != std::string::npos)
                   {
                     std::string key = s.substr(0, pos);
                     std::string value = s.substr(pos + 1, std::string::npos);

                     if (key == "dir")
                     {
                       CURL *curl = curl_easy_init();

                       char *str = curl_easy_unescape(curl, value.c_str(),
                                                      value.length(), NULL);
                       dir = std::string(str);
                       curl_free(str);

                       curl_easy_cleanup(curl);
                     }
                   }
                 }

                 if (dir != "")
                   return get_directory(res, dir);
                 else
                   return get_home_directory(res);
               })
          .post("/:root/heartbeat/*",
                [](auto *res, auto *req) {
                  std::string_view uri = req->getUrl();

                  size_t pos = uri.find_last_of("/");

                  if (pos != std::string::npos)
                  {
                    std::string_view timestamp =
                        uri.substr(pos + 1, std::string::npos);
                    res->end(timestamp);
                    return;
                  }

                  // default response
                  res->end("N/A");
                  return;
                })
          .post(
              "/:root/progress/*",
              [](auto *res, auto *req) {
                std::string_view uri = req->getUrl();

                size_t pos = uri.find_last_of("/");

                if (pos != std::string::npos)
                {
                  std::string datasetid =
                      std::string(uri.substr(pos + 1, std::string::npos));

                  // process the response
                  // std::cout << "progress(" << datasetid << ")" <<
                  // std::endl;

                  auto fits = get_dataset(datasetid);

                  if (fits == nullptr)
                    return http_not_found(res);
                  else
                  {
                    if (fits->has_error)
                      return http_not_found(res);
                    else
                    {
                      // make json
                      std::ostringstream json;
                      bool valid = false;
                      {
                        std::shared_lock<std::shared_mutex> lock(
                            fits->progress_mtx);

                        json << "{\"total\" : " << fits->progress.total << ",";
                        json << "\"running\" : " << fits->progress.running
                             << ",";
                        json << "\"elapsed\" : "
                             << (std::isnan(fits->progress.elapsed)
                                     ? "null"
                                     : std::to_string(fits->progress.elapsed))
                             << "}";

                        if (fits->progress.total > 0)
                          valid = true;
                      }

                      if (valid)
                      {
                        if (json.tellp() > 0)
                        {
                          res->writeHeader("Content-Type", "application/json");
                          res->writeHeader("Cache-Control", "no-cache");
                          res->writeHeader("Cache-Control", "no-store");
                          res->writeHeader("Pragma", "no-cache");
                          res->end(json.str());
                          return;
                        }
                        else
                          return http_not_implemented(res);
                      }
                      else
                        return http_accepted(res);
                    }
                  }
                }

                // default response
                return http_not_found(res);
              })
          .get("/:root/*",
               [](auto *res, auto *req) {
                 std::string_view uri = req->getUrl();
                 std::string_view root = req->getParameter(0);

                 std::cout << "HTTP root path(" << root << "), request for "
                           << uri << std::endl;

                 if (uri.find("/image_spectrum") != std::string::npos)
                 {
                   std::string_view query = req->getQuery();
                   std::cout << "query: (" << query << ")" << std::endl;

                   std::string datasetid;
                   int width = 0;
                   int height = 0;
                   float quality = 45;
                   bool fetch_data = false;

                   std::vector<std::string> params;
                   boost::split(params, query, [](char c) { return c == '&'; });

                   CURL *curl = curl_easy_init();

                   for (auto const &s : params)
                   {
                     // find '='
                     size_t pos = s.find("=");

                     if (pos != std::string::npos)
                     {
                       std::string key = s.substr(0, pos);
                       std::string value = s.substr(pos + 1, std::string::npos);

                       if (key.find("dataset") != std::string::npos)
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         datasetid = std::string(str);
                         curl_free(str);
                       }

                       if (key.find("width") != std::string::npos)
                       {
                         width = std::stoi(value);
                       }

                       if (key.find("height") != std::string::npos)
                       {
                         height = std::stoi(value);
                       }

                       if (key.find("quality") != std::string::npos)
                       {
                         try
                         {
                           quality = std::stof(value);
                         }
                         catch (std::invalid_argument &exc)
                         {
                         }
                       }

                       if (key.find("fetch_data") != std::string::npos)
                       {
                         if (value == "true")
                           fetch_data = true;
                       }
                     }
                   }

                   curl_easy_cleanup(curl);

                   // process the response
                   std::cout << "get_image_spectrum(" << datasetid
                             << "::" << width << "::" << height
                             << "::" << quality
                             << "::" << (fetch_data ? "true" : "false") << ")"
                             << std::endl;

                   auto fits = get_dataset(datasetid);

                   if (fits == nullptr)
                     return http_not_found(res);
                   else
                   {
                     if (fits->has_error)
                       return http_not_found(res);
                     else
                     {
                       std::shared_ptr<std::atomic<bool>> aborted =
                           std::make_shared<std::atomic<bool>>(false);

                       res->onAborted([aborted]() {
                         std::cout << "get_spectrum aborted\n";

                         // invalidate res (pass the aborted event to the
                         // get_spectrum() thread
                         *aborted.get() = true;
                       });

                       std::shared_ptr<std::atomic<bool>> writable =
                           std::make_shared<std::atomic<bool>>(true);

                       res->onWritable([writable](int offset) {
                         std::cout << "get_spectrum is writable, offset = " << offset << "\n";
                         *writable.get() = true;
                         return true;
                       });

                       std::thread([res, fits, width, height, quality,
                                    fetch_data, aborted]() {
                         std::unique_lock<std::mutex> data_lock(fits->data_mtx);
                         while (!fits->processed_data)
                           fits->data_cv.wait(data_lock);

                         if (!fits->has_data)
                         {
                           if (*aborted.get() != true)
                             http_not_found(res);
                         }
                         else
                           stream_image_spectrum(res, fits, width, height,
                                                 quality, fetch_data, aborted);
                       }).detach();
                       return;
                     }
                   }
                 }

                 if (uri.find("/get_molecules") != std::string::npos)
                 {
                   // handle the accepted keywords
                   bool compress = false;
                   auto encoding = req->getHeader("accept-encoding");

                   if (encoding != "")
                   {
                     std::string_view value = encoding;
                     size_t pos = value.find("gzip"); // gzip or deflate

                     if (pos != std::string::npos)
                       compress = true;

                     std::cout << "Accept-Encoding:" << value
                               << "; compression support "
                               << (compress ? "" : "not ") << "found."
                               << std::endl;
                   }

                   std::string_view query = req->getQuery();
                   // std::cout << "query: (" << query << ")" << std::endl;

                   std::string datasetid;
                   double freq_start = 0.0;
                   double freq_end = 0.0;

                   std::vector<std::string> params;
                   boost::split(params, query, [](char c) { return c == '&'; });

                   CURL *curl = curl_easy_init();

                   for (auto const &s : params)
                   {
                     // find '='
                     size_t pos = s.find("=");

                     if (pos != std::string::npos)
                     {
                       std::string key = s.substr(0, pos);
                       std::string value = s.substr(pos + 1, std::string::npos);

                       if (key.find("dataset") != std::string::npos)
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         datasetid = std::string(str);
                         curl_free(str);
                       }

                       if (key.find("freq_start") != std::string::npos)
                         freq_start = std::stod(value) / 1.0E9; //[Hz -> GHz]

                       if (key.find("freq_end") != std::string::npos)
                         freq_end = std::stod(value) / 1.0E9; //[Hz -> GHz]
                     }
                   }

                   curl_easy_cleanup(curl);

                   if (FPzero(freq_start) || FPzero(freq_end))
                   {
                     // get the frequency range from the FITS header
                     auto fits = get_dataset(datasetid);

                     if (fits == nullptr)
                       return http_not_found(res);
                     else
                     {
                       if (fits->has_error)
                         return http_not_found(res);

                       std::unique_lock<std::mutex> header_lck(
                           fits->header_mtx);
                       while (!fits->processed_header)
                         fits->header_cv.wait(header_lck);

                       if (!fits->has_header)
                         // return http_accepted(res);
                         return http_not_found(res);

                       if (fits->depth <= 1 || !fits->has_frequency)
                         return http_not_implemented(res);

                       // extract the freq. range
                       fits->get_frequency_range(freq_start, freq_end);
                     }
                   }

                   // process the response
                   std::cout << "get_molecules(" << datasetid << ","
                             << freq_start << "GHz," << freq_end << "GHz)"
                             << std::endl;

                   if (!FPzero(freq_start) && !FPzero(freq_end))
                   {
                     std::shared_ptr<std::atomic<bool>> aborted =
                         std::make_shared<std::atomic<bool>>(false /*or true*/);

                     res->onAborted([aborted]() {
                       std::cout << "get_molecules aborted\n";

                       // invalidate res (pass the aborted event to the
                       // stream_molecules() thread
                       *aborted.get() = true;
                     });

                     std::shared_ptr<std::atomic<bool>> writable =
                         std::make_shared<std::atomic<bool>>(true);

                     res->onWritable([writable](int offset) {
                       std::cout << "get_molecules is writable, offset = " << offset << "\n";
                       *writable.get() = true;
                       return true;
                     });

                     std::thread([res, freq_start, freq_end, compress,
                                  aborted]() {
                       stream_molecules(res, freq_start, freq_end, compress,
                                        aborted);
                     }).detach();
                     return;
                   }
                   else
                     return http_not_implemented(res);
                 }

                 if (uri.find("/get_fits") != std::string::npos)
                 {
                   std::string_view query = req->getQuery();
                   std::cout << "query: (" << query << ")" << std::endl;

                   std::string datasetid;
                   double frame_start = 0.0;
                   double frame_end = 0.0;
                   double ref_freq = 0.0;
                   int x1 = -1;
                   int x2 = -1;
                   int y1 = -1;
                   int y2 = -1;

                   std::vector<std::string> params;
                   boost::split(params, query, [](char c) { return c == '&'; });

                   CURL *curl = curl_easy_init();

                   for (auto const &s : params)
                   {
                     // find '='
                     size_t pos = s.find("=");

                     if (pos != std::string::npos)
                     {
                       std::string key = s.substr(0, pos);
                       std::string value = s.substr(pos + 1, std::string::npos);

                       if (key.find("dataset") != std::string::npos)
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         datasetid = std::string(str);
                         curl_free(str);
                       }

                       if (key.find("x1") != std::string::npos)
                       {
                         x1 = std::stoi(value);
                       }

                       if (key.find("x2") != std::string::npos)
                       {
                         x2 = std::stoi(value);
                       }

                       if (key.find("y1") != std::string::npos)
                       {
                         y1 = std::stoi(value);
                       }

                       if (key.find("y2") != std::string::npos)
                       {
                         y2 = std::stoi(value);
                       }

                       if (key.find("frame_start") != std::string::npos)
                         frame_start = std::stod(value);

                       if (key.find("frame_end") != std::string::npos)
                         frame_end = std::stod(value);

                       if (key.find("ref_freq") != std::string::npos)
                         ref_freq = std::stod(value);
                     }
                   }

                   curl_easy_cleanup(curl);

                   // process the response
                   std::cout << "get_fits(" << datasetid << "::X in [" << x1 << "," << x2 << "], Y in [" << y1 << "," << y2 << "]"
                             << ", frame_start = " << frame_start << ", frame_end = " << frame_end << ", ref_freq = " << ref_freq << ")" << std::endl;

                   auto fits = get_dataset(datasetid);

                   if (fits == nullptr)
                     return http_not_found(res);
                   else
                   {
                     if (fits->has_error)
                       return http_not_found(res);
                     else
                     {
                       std::shared_ptr<std::atomic<bool>> aborted =
                           std::make_shared<std::atomic<bool>>(false);

                       res->onAborted([aborted]() {
                         std::cout << "get_fits aborted\n";

                         // invalidate res (pass the aborted event to the get_fits() thread
                         *aborted.get() = true;
                       });

                       std::shared_ptr<std::atomic<bool>> writable =
                           std::make_shared<std::atomic<bool>>(true);

                       res->onWritable([writable](int offset) {
                         std::cout << "get_fits is writable, offset = " << offset << "\n";
                         *writable.get() = true;
                         return true;
                       });

                       std::thread([res, fits, x1, x2, y1, y2, frame_start, frame_end, ref_freq, aborted]() {
                         std::unique_lock<std::mutex> data_lock(fits->data_mtx);
                         while (!fits->processed_data)
                           fits->data_cv.wait(data_lock);

                         if (!fits->has_data)
                         {
                           if (*aborted.get() != true)
                             http_not_found(res);
                         }
                         else
                         {
                           int start, end;

                           fits->update_timestamp();
                           fits->get_spectrum_range(frame_start, frame_end, ref_freq, start, end);
                           stream_partial_fits(res, fits, x1, x2, y1, y2, start, end, aborted);
                         }
                       }).detach();
                       return;
                     }
                   }
                 }

                 /*if (uri.find("/get_image") != std::string::npos) {
                   std::string_view query = req->getQuery();
                   // std::cout << "query: (" << query << ")" << std::endl;

                   std::string datasetid;

                   std::vector<std::string> params;
                   boost::split(params, query,
                   [](char c) { return c == '&'; });

                   CURL *curl = curl_easy_init();

                   for (auto const &s : params) {
                   // find '='
                   size_t pos = s.find("=");

                   if (pos != std::string::npos) {
                   std::string key = s.substr(0, pos);
                   std::string value =
                   s.substr(pos + 1, std::string::npos);

                   if (key.find("dataset") != std::string::npos) {
                   char *str = curl_easy_unescape(
                   curl, value.c_str(), value.length(), NULL);
                   datasetid = std::string(str);
                   curl_free(str);
                   }
                   }
                   }

                   curl_easy_cleanup(curl);

                   std::shared_lock<std::shared_mutex> lock(fits_mutex);
                   auto item = DATASETS.find(datasetid);
                   lock.unlock();

                   if (item == DATASETS.end())
                   return http_not_found(res);
                   else {
                   auto fits = item->second;

                   if (fits->has_error)
                   return http_not_found(res);
                   else
                   return http_accepted(res);
                   }
                   }*/

                 // FITSWebQL entry
                 if (uri.find("FITSWebQL.html") != std::string::npos)
                 {
                   std::string_view query = req->getQuery();
                   std::cout << "query: (" << query << ")" << std::endl;

                   std::string url;
                   std::vector<std::string> datasets;
                   std::string dir, ext, db, table, flux;
                   bool composite = false;

                   std::vector<std::string> params;
                   boost::split(params, query, [](char c) { return c == '&'; });

                   CURL *curl = curl_easy_init();

                   for (auto const &s : params)
                   {
                     // find '='
                     size_t pos = s.find("=");

                     if (pos != std::string::npos)
                     {
                       std::string key = s.substr(0, pos);
                       std::string value = s.substr(pos + 1, std::string::npos);

                       if (key == "url")
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         url = std::string(str);
                         curl_free(str);
                       }

                       if (key.find("dataset") != std::string::npos)
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         datasets.push_back(std::string(str));
                         curl_free(str);
                       }

                       if (key.find("filename") != std::string::npos)
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         datasets.push_back(std::string(str));
                         curl_free(str);
                       }

                       if (key == "dir")
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         dir = std::string(str);
                         curl_free(str);
                       }

                       if (key == "ext")
                       {
                         char *str = curl_easy_unescape(curl, value.c_str(),
                                                        value.length(), NULL);
                         ext = std::string(str);
                         curl_free(str);
                       }

                       if (key == "db")
                         db = value;

                       if (key == "table")
                         table = value;

                       if (key == "flux")
                       {
                         // validate the flux value
                         std::set<std::string> valid_values;
                         valid_values.insert("linear");
                         valid_values.insert("logistic");
                         valid_values.insert("ratio");
                         valid_values.insert("square");
                         valid_values.insert("legacy");

                         if (valid_values.find(value) != valid_values.end())
                           flux = value;
                       }

                       if (key == "view")
                       {
                         if (value.find("composite") != std::string::npos)
                           composite = true;
                       }
                     }
                   }

                   curl_easy_cleanup(curl);

                   // sane defaults
                   {
                     if (db.find("hsc") != std::string::npos)
                     {
                       flux = "ratio";
                     }

                     if (table.find("fugin") != std::string::npos)
                       flux = "logistic";
                   }

                   std::cout << "url:" << url << ", dir:" << dir << ", ext:" << ext
                             << ", db:" << db << ", table:" << table
                             << ", composite:" << composite << ", flux:" << flux
                             << ", ";
                   for (auto const &dataset : datasets)
                     std::cout << dataset << " ";
                   std::cout << std::endl;

                   if (datasets.size() == 0 && url.empty())
                   {
                     const std::string not_found =
                         "ERROR: please specify an external download URL or at least one dataset in "
                         "the URL parameters list.";
                     return res->end(not_found);
                   }
                   else
                     return execute_fits(res, std::string(root), url, dir, ext, db,
                                         table, datasets, composite, flux);
                 };

                 // default handler
                 return serve_file(res, "htdocs" + std::string(uri));
               })
          .ws<UserData>(
              "/:root/websocket/*",
              {/* Settings */
               .compression = uWS::SHARED_COMPRESSOR,
               /* Handlers */
               .upgrade =
                   [](auto *res, auto *req, auto *context) {
                     std::string_view url = req->getUrl();
                     PrintThread{} << "[µWS] upgrade " << url << std::endl;

                     std::vector<std::string> datasetid;

                     size_t pos = url.find_last_of("/");

                     if (pos != std::string::npos)
                     {
                       std::string_view tmp = url.substr(pos + 1);
                       CURL *curl = curl_easy_init();
                       char *str = curl_easy_unescape(curl, tmp.data(),
                                                      tmp.length(), NULL);
                       std::string plain = std::string(str);
                       curl_free(str);
                       curl_easy_cleanup(curl);
                       boost::split(datasetid, plain,
                                    [](char c) { return c == ';'; });

                       for (auto const &s : datasetid)
                       {
                         PrintThread{} << "datasetid: " << s << std::endl;
                       }
                     }

                     if (datasetid.size() > 0)
                       res->template upgrade<UserData>(
                           {.ptr = new UserSession(
                                boost::uuids::random_generator()(),
                                system_clock::now() -
                                    duration_cast<system_clock::duration>(
                                        duration<double>(uWS_PROGRESS_TIMEOUT)),
                                datasetid[0], datasetid)},
                           req->getHeader("sec-websocket-key"),
                           req->getHeader("sec-websocket-protocol"),
                           req->getHeader("sec-websocket-extensions"), context);
                     else
                       http_internal_server_error(res);
                   },
               .open =
                   [](auto *ws) {
                     struct UserData *user =
                         (struct UserData *)ws->getUserData();

                     if (user == NULL)
                       return;

                     if (user->ptr == NULL)
                       return;

                     std::string primary_id = user->ptr->primary_id;
                     PrintThread{} << "[µWS] open for " << primary_id
                                   << std::endl;

                     // launch a separate thread
                     std::thread([primary_id, ws]() {
                       std::lock_guard<std::shared_mutex> guard(
                           m_progress_mutex);
                       TWebSocketList connections = m_progress[primary_id];
                       connections.insert(ws);
                       m_progress[primary_id] = connections;
                     }).detach();
                   },
               .message =
                   [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                     if (message.find("[heartbeat]") != std::string::npos)
                     {
                       ws->send(message, opCode);
                       return;
                     }
                     else
                     {
                       PrintThread{} << "[µWS::message] " << message
                                     << std::endl;
                     }

                     // ignore messages if there is no primary
                     // datasetid available
                     struct UserData *user =
                         (struct UserData *)ws->getUserData();

                     if (user == NULL)
                       return;

                     if (user->ptr == NULL)
                       return;

                     std::string datasetid = user->ptr->primary_id;

                     if (message.find("[kalman_reset]") != std::string::npos)
                     {
                       int seq = -1;

                       std::string_view query;
                       size_t pos = message.find_first_of(" ");

                       if (pos != std::string::npos)
                         query = message.substr(pos + 1, std::string::npos);

                       std::vector<std::string> params;
                       boost::split(params, query,
                                    [](char c) { return c == '&'; });

                       for (auto const &s : params)
                       {
                         // find '='
                         size_t pos = s.find("=");

                         if (pos != std::string::npos)
                         {
                           std::string key = s.substr(0, pos);
                           std::string value =
                               s.substr(pos + 1, std::string::npos);

                           if (key.find("seq") != std::string::npos)
                             seq = std::stoi(value);
                         }
                       }

                       if (seq > -1)
                       {
                         if (user->ptr->active)
                         {
                           // gain unique access
                           std::lock_guard<std::shared_mutex> unique_access(
                               user->ptr->mtx);

                           // remove any previous Kalman Filters
                           user->ptr->kal_x.reset();
                           user->ptr->kal_y.reset();

                           int last_seq = user->ptr->last_seq;

                           if (seq > last_seq)
                             user->ptr->last_seq = seq;
                         }
                       }
                     }

                     // [init_video]
                     if (message.find("[init_video]") != std::string::npos)
                     {
                       auto now = system_clock::now();
                       user->ptr->ts = now;

                       int seq = -1;
                       int fps = 10;
                       int bitrate = 1000; // start slow
                       int _width = -1;
                       int _height = -1;
                       double frame = 0;
                       double ref_freq = 0;
                       float timestamp = 0;
                       std::string flux;

                       std::string_view query;
                       size_t pos = message.find_first_of(" ");

                       if (pos != std::string::npos)
                         query = message.substr(pos + 1, std::string::npos);

                       std::vector<std::string> params;
                       boost::split(params, query,
                                    [](char c) { return c == '&'; });

                       for (auto const &s : params)
                       {
                         // find '='
                         size_t pos = s.find("=");

                         if (pos != std::string::npos)
                         {
                           std::string key = s.substr(0, pos);
                           std::string value =
                               s.substr(pos + 1, std::string::npos);

                           if (key.find("seq") != std::string::npos)
                             seq = std::stoi(value);

                           if (key.find("width") != std::string::npos)
                             _width = std::stoi(value);

                           if (key.find("height") != std::string::npos)
                             _height = std::stoi(value);

                           if (key.find("bitrate") != std::string::npos)
                             bitrate = std::stoi(value);

                           if (key.find("fps") != std::string::npos)
                             fps = std::stoi(value);

                           if (key.find("frame") != std::string::npos)
                             frame = std::stod(value);

                           if (key.find("ref_freq") != std::string::npos)
                             ref_freq = std::stod(value);

                           if (key.find("flux") != std::string::npos)
                             flux = value;

                           if (key.find("timestamp") != std::string::npos)
                             timestamp = std::stof(value);
                         }
                       }

                       // process the parameters

                       auto fits = get_dataset(datasetid);

                       if (fits != nullptr)
                       {
                         if (!fits->has_error && fits->has_data)
                         {
                           fits->update_timestamp();

                           if (!user->ptr->active)
                             return;

                           // set up the x265 stream
                           user->ptr->flux = flux;
                           user->ptr->fps = fps;
                           user->ptr->bitrate = bitrate;

                           // gain unique access
                           std::lock_guard<std::shared_mutex> unique_access(
                               user->ptr->video_mtx);
                           user->ptr->last_video_seq = seq;
                           user->ptr->last_frame_idx = -1;

                           // copy over the default mask
                           if (!user->ptr->img_mask)
                             user->ptr->img_mask = fits->img_mask;

                           // calculate the scale based on the user image
                           if (!user->ptr->img_mask)
                             return;

                           // calculate a new image size
                           long true_width = fits->width;
                           long true_height = fits->height;
                           true_image_dimensions(user->ptr->img_mask.get(),
                                                 true_width, true_height);
                           float scale = get_image_scale(
                               _width, _height, true_width, true_height);

                           int img_width = fits->width;
                           int img_height = fits->height;

                           // problems downscaling 4-padded buffers ???
                           if (scale < 1.0)
                           {
                             img_width = floorf(scale * fits->width);
                             img_height = floorf(scale * fits->height);
                           }

                           user->ptr->width = img_width;
                           user->ptr->height = img_height;

                           int _padded_width = img_width;
                           if (img_width % CELLSIZE > 0)
                             _padded_width +=
                                 CELLSIZE - img_width % CELLSIZE;

                           int _padded_height = img_height;
                           if (img_height % CELLSIZE > 0)
                             _padded_height +=
                                 CELLSIZE - img_height % CELLSIZE;

                           if (user != NULL)
                             if (user->ptr != NULL)
                               if (user->ptr->active)
                               {
                                 std::lock_guard<std::shared_mutex>
                                     unique_access(user->ptr->mtx);
                                 std::string resp =
                                     "{\"type\" : \"init_video\", \"width\" : " +
                                     std::to_string(img_width) + ", \"height\" : " +
                                     std::to_string(img_height) + ", \"padded_width\" : " +
                                     std::to_string(_padded_width) + ", \"padded_height\" : " +
                                     std::to_string(_padded_height) + "}";
                                 ws->send(resp, opCode);
                               }

                           // get the video frame index
                           int frame_idx;

                           fits->get_spectrum_range(frame, frame, ref_freq,
                                                    frame_idx, frame_idx);

                           std::cout << "[uWS::init_video]::" << datasetid
                                     << "\tfps = " << fps
                                     << "\tbitrate = " << bitrate
                                     << "\tflux = " << flux
                                     << "\tscale = " << scale
                                     << "\tframe = " << frame_idx << std::endl;

                           user->ptr->kal_z = std::shared_ptr<KalmanFilter>(
                               new KalmanFilter(frame, true));

                           // alloc HEVC params
                           x265_param *param = x265_param_alloc();
                           if (param == NULL)
                             return;

                           //x265_param_default_preset(param, "ultrafast", "zerolatency");
                           x265_param_default_preset(param, "superfast", "zerolatency");
                           //x265_param_default_preset(param, "medium", "zerolatency");

                           // HEVC config
                           param->fpsNum = fps;
                           param->fpsDenom = 1;
                           param->bRepeatHeaders = 1;
                           param->internalCsp = X265_CSP_I444;

                           param->internalBitDepth = 8;
                           param->sourceWidth = img_width;
                           param->sourceHeight = img_height;

                           // constant bitrate
                           param->rc.rateControlMode = X265_RC_CRF;
                           param->rc.bitrate = bitrate;

                           user->ptr->params = std::shared_ptr<x265_param>(
                               param, x265_param_free);

                           // HEVC encoder
                           user->ptr->encoder = std::shared_ptr<x265_encoder>(
                               x265_encoder_open(param), x265_encoder_close);
                           if (!user->ptr->encoder)
                             return;

                           // HEVC picture
                           x265_picture *picture = x265_picture_alloc();
                           if (picture == NULL)
                             return;

                           x265_picture_init(param, picture);

                           // allocate a dummy B channel
                           const size_t frame_size = _padded_width * _padded_height;
                           Ipp8u *B_buf = ippsMalloc_8u_L(frame_size);

                           if (B_buf != NULL)
                             memset(B_buf, 128, frame_size);

                           picture->planes[0] = NULL;
                           picture->planes[1] = NULL;
                           picture->planes[2] = B_buf;

                           picture->stride[0] = 0;
                           picture->stride[1] = 0;
                           picture->stride[2] = _padded_width;

                           user->ptr->picture = std::shared_ptr<x265_picture>(
                               picture, [=](x265_picture *ptr) {
                                 if (ptr != NULL)
                                 {
                                   // deallocate RGB planes
                                   for (int i = 0; i < 3; i++)
                                     if (ptr->planes[i] != NULL)
                                       ippsFree(ptr->planes[i]);

                                   // finally free the picture
                                   x265_picture_free(ptr);
                                 }
                               });

                           // start a video creation event loop
                           user->ptr->streaming = true;
                         }
                       }
                     }

                     // [end_video]
                     if (message.find("[end_video]") != std::string::npos)
                     {
                       auto now = system_clock::now();
                       user->ptr->ts = now;
                       user->ptr->streaming = false;
                     }

                     // [video]
                     if (message.find("[video]") != std::string::npos)
                     {
                       auto now = system_clock::now();
                       duration<double, std::milli> deltat =
                           now - user->ptr->ts;
                       user->ptr->ts = now;

                       int seq = -1;
                       int fps = 10;
                       int bitrate = 1000; // start slow
                       double frame = 0;
                       double ref_freq = 0;
                       float timestamp = 0;
                       bool keyframe = false; // is it a keyframe?

                       std::string_view query;
                       size_t pos = message.find_first_of(" ");

                       if (pos != std::string::npos)
                         query = message.substr(pos + 1, std::string::npos);

                       std::vector<std::string> params;
                       boost::split(params, query,
                                    [](char c) { return c == '&'; });

                       for (auto const &s : params)
                       {
                         // find '='
                         size_t pos = s.find("=");

                         if (pos != std::string::npos)
                         {
                           std::string key = s.substr(0, pos);
                           std::string value =
                               s.substr(pos + 1, std::string::npos);

                           if (key.find("seq") != std::string::npos)
                             seq = std::stoi(value);

                           if (key.find("bitrate") != std::string::npos)
                             bitrate = std::stoi(value);

                           if (key.find("fps") != std::string::npos)
                             fps = std::stoi(value);

                           if (key.find("frame") != std::string::npos)
                             frame = std::stod(value);

                           if (key.find("ref_freq") != std::string::npos)
                             ref_freq = std::stod(value);

                           if (key.find("key") != std::string::npos)
                           {
                             if (value == "true")
                               keyframe = true;
                           }

                           if (key.find("timestamp") != std::string::npos)
                             timestamp = std::stof(value);
                         }
                       }

                       // process the video frame in a separate thread

                       auto fits = get_dataset(datasetid);

                       if (fits != nullptr)
                       {
                         if (!fits->has_error && fits->has_data)
                         {
                           // launch a separate thread
                           boost::thread *video_thread = new boost::thread(
                               [fits, ws, user, frame, ref_freq, keyframe,
                                timestamp, seq, fps, bitrate, deltat]() {
                                 if (user == NULL)
                                   return;

                                 if (user->ptr == NULL)
                                   return;

                                 if (!user->ptr->active)
                                   return;

                                 fits->update_timestamp();

                                 //{
                                 // gain unique
                                 // access
                                 std::lock_guard<std::shared_mutex>
                                     unique_access(user->ptr->video_mtx);

                                 int last_seq = user->ptr->last_video_seq;

                                 if (last_seq > seq)
                                 {
                                   printf("skippin"
                                          "g an "
                                          "old "
                                          "frame "
                                          "(%d < "
                                          "%d)\n",
                                          seq, last_seq);
                                   return;
                                 }
                                 else
                                   printf("processing video frame #%d\n", seq);

                                 user->ptr->last_video_seq = seq;
                                 //}

                                 double _frame = frame;

                                 if (user->ptr->kal_z)
                                 {
                                   KalmanFilter *kal_z = user->ptr->kal_z.get();

                                   // update the frame
                                   kal_z->update(frame, deltat.count());

                                   // predict the frame deltat [ms] ahead
                                   _frame =
                                       kal_z->predict(frame, deltat.count());
                                 }

                                 if (user->ptr->encoder && user->ptr->params)
                                 {
                                   // adaptive bitrate
                                   user->ptr->params.get()->rc.bitrate =
                                       bitrate;

                                   auto ret = x265_encoder_reconfig(
                                       user->ptr->encoder.get(),
                                       user->ptr->params.get());

                                   if (ret < 0)
                                     printf(
                                         "x265: error changing the bitrate\n");
                                 }

                                 // get the video frame index
                                 int frame_idx;

                                 fits->get_spectrum_range(_frame, _frame,
                                                          ref_freq, frame_idx,
                                                          frame_idx);

                                 if (frame_idx == user->ptr->last_frame_idx)
                                 {
                                   printf("skipping a repeat video frame.\n");
                                   return;
                                 }
                                 else
                                   user->ptr->last_frame_idx = frame_idx;

                                 auto start_t = steady_clock::now();

                                 auto [padded_width, padded_height, _luma,
                                       _mask, has_luma] =
                                     fits->get_video_frame(frame_idx,
                                                           user->ptr->flux);

                                 if (!has_luma)
                                 {
                                   printf("%s::unrecognised flux '%s'\n",
                                          fits->dataset_id.c_str(),
                                          user->ptr->flux.c_str());
                                   return;
                                 }

                                 if (_luma && _mask)
                                 {
                                   int img_width = user->ptr->width;
                                   int img_height = user->ptr->height;

                                   // optional downscaling (if needed)
                                   if (img_width < fits->width ||
                                       img_height < fits->height)
                                   {
                                     int _padded_width = img_width;
                                     if (img_width % CELLSIZE > 0)
                                       _padded_width +=
                                           CELLSIZE - img_width % CELLSIZE;

                                     int _padded_height = img_height;
                                     if (img_height % CELLSIZE > 0)
                                       _padded_height +=
                                           CELLSIZE - img_height % CELLSIZE;

                                     printf("downscaling the video frame to %d "
                                            "x %d; after padding (%d x %d)\n",
                                            img_width, img_height,
                                            _padded_width, _padded_height);

                                     const size_t frame_size =
                                         _padded_width * _padded_height;
                                     // size_t plane_size = size_t(img_width) *
                                     // size_t(img_height);

                                     // allocate {pixel_buf, mask_buf}
                                     std::shared_ptr<Ipp8u> pixels_buf(
                                         ippsMalloc_8u_L(frame_size), ippsFree);
                                     std::shared_ptr<Ipp8u> mask_buf(
                                         ippsMalloc_8u_L(frame_size), ippsFree);

                                     if (pixels_buf && mask_buf)
                                     {
                                       /*memset(pixels_buf.get(), 0,
                                       frame_size); memset(mask_buf.get(), 0,
                                       frame_size);*/

                                       // downsize uint8_t pixels and a mask
                                       IppiSize srcSize;
                                       srcSize.width = fits->width;
                                       srcSize.height = fits->height;
                                       Ipp32s srcStep = padded_width;

                                       IppiSize dstSize;
                                       dstSize.width = img_width;
                                       dstSize.height = img_height;
                                       Ipp32s dstStep = _padded_width;

                                       IppStatus pixels_stat = tileResize8u_C1R(
                                           _luma.get(), srcSize, srcStep,
                                           pixels_buf.get(), dstSize, dstStep);

                                       IppStatus mask_stat = tileResize8u_C1R(
                                           _mask.get(), srcSize, srcStep,
                                           mask_buf.get(), dstSize, dstStep);

                                       printf(" %d : %s, %d : %s\n",
                                              pixels_stat,
                                              ippGetStatusString(pixels_stat),
                                              mask_stat,
                                              ippGetStatusString(mask_stat));

                                       _luma = std::move(pixels_buf);
                                       _mask = std::move(mask_buf);

                                       padded_width = _padded_width;
                                       padded_height = _padded_height;
                                     }
                                   }

                                   // contour lines (optional)
                                   // 1. run Marching Squares on _pixels
                                   // 2. overlay raster contour lines over _r,
                                   // _g, _b uses CONREC for now
                                   if (keyframe)
                                   {
                                     auto _start_t = steady_clock::now();

                                     FITSRaster raster(_luma, img_width,
                                                       img_height,
                                                       padded_width);
                                     CContourMap contours;
                                     contours.generate_levels(
                                         fits->frame_min[frame_idx],
                                         fits->frame_max[frame_idx], 5);

                                     contours.contour(&raster);
                                     // contours.consolidate(); // consolidating
                                     // segments takes up far too much time ...

                                     auto _end_t = steady_clock::now();

                                     double elapsedSeconds =
                                         ((_end_t - _start_t).count()) *
                                         steady_clock::period::num /
                                         static_cast<double>(
                                             steady_clock::period::den);
                                     double elapsedMilliseconds =
                                         1000.0 * elapsedSeconds;

                                     // contours.dump();

                                     printf("%s::contouring the video frame "
                                            "(CONREC); elapsed time %f [ms]\n",
                                            fits->dataset_id.c_str(),
                                            elapsedMilliseconds);
                                   }

                                   int contour_levels = 5;

                                   //if (!keyframe)
                                   if (false) // disabled, Marching Squares to be debugged ...
                                   {
                                     auto _start_t = steady_clock::now();

                                     int pixel_max = 255;
                                     int pixel_min = 0;
                                     float delta =
                                         float(pixel_max - pixel_min) /
                                         float(contour_levels + 1);

#pragma omp parallel for
                                     for (int i = 0; i < contour_levels; i++)
                                     {
                                       uint32_t threshold =
                                           pixel_min + (i + 1) * delta;
                                       printf("par_msquares: i = %d, threshold "
                                              "= %d\n",
                                              i, threshold);

                                       // needs to be called multiple times with
                                       // different colour thresholds
                                       par_msquares_meshlist *mesh_list =
                                           par_msquares_color(
                                               _luma.get(), padded_width,
                                               padded_height, CELLSIZE,
                                               threshold, 1, 0);
                                       par_msquares_mesh const *mesh =
                                           par_msquares_get_mesh(mesh_list, 0);
                                       // par_msquares_boundary*
                                       // par_msquares_extract_boundary(mesh);

                                       /*float *pt = mesh->points;
                                     for (int i = 0; i < mesh->npoints; i++)
                                     {
                                       printf("v %f %f\n", pt[0], pt[1]);
                                       pt += mesh->dim;
                                     };*/
                                     }

                                     auto _end_t = steady_clock::now();

                                     double elapsedSeconds =
                                         ((_end_t - _start_t).count()) *
                                         steady_clock::period::num /
                                         static_cast<double>(
                                             steady_clock::period::den);
                                     double elapsedMilliseconds =
                                         1000.0 * elapsedSeconds;

                                     printf("%s::contouring the video frame "
                                            "(Marching Squares); elapsed time "
                                            "%f [ms]\n",
                                            fits->dataset_id.c_str(),
                                            elapsedMilliseconds);
                                   }

                                   // HEVC-encode _luma as R, _mask as G, blank out B
                                   // set the R and G planes
                                   if (user->ptr->picture && user->ptr->encoder)
                                   {
                                     x265_picture *picture = user->ptr->picture.get();
                                     x265_encoder *encoder = user->ptr->encoder.get();

                                     picture->planes[0] = _luma.get();
                                     picture->planes[1] = _mask.get();

                                     picture->stride[0] = padded_width;
                                     picture->stride[1] = padded_width;

                                     // RGB-encode
                                     x265_nal *pNals = NULL;
                                     uint32_t iNal = 0;

                                     int ret = x265_encoder_encode(encoder, &pNals, &iNal, picture, NULL);
                                     printf("[x265_encode]::ret = %d, #frames = %d\n", ret, iNal);

                                     auto end_t = steady_clock::now();

                                     double elapsedSeconds =
                                         ((end_t - start_t).count()) *
                                         steady_clock::period::num /
                                         static_cast<double>(
                                             steady_clock::period::den);
                                     double elapsedMilliseconds =
                                         1000.0 * elapsedSeconds;

                                     for (unsigned int i = 0; i < iNal; i++)
                                     {
                                       std::cout << "NAL unit #" << (i + 1) << " [type: " << pNals[i].type << ", size: " << pNals[i].sizeBytes << " bytes]." << std::endl;

                                       // send a binary WebSocket frame (msg_id = 5)
                                       std::cout << "[uWS] sending the video frame NAL unit" << std::endl;

                                       size_t bufferSize = sizeof(float) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(float) + pNals[i].sizeBytes;

                                       char *buffer = (char *)malloc(bufferSize);

                                       if (buffer != NULL)
                                       {
                                         float ts = timestamp;
                                         uint32_t id = seq;
                                         uint32_t msg_type = 5;
                                         float elapsed = elapsedMilliseconds;

                                         size_t offset = 0;

                                         memcpy(buffer + offset, &ts,
                                                sizeof(float));
                                         offset += sizeof(float);

                                         memcpy(buffer + offset, &id,
                                                sizeof(uint32_t));
                                         offset += sizeof(uint32_t);

                                         memcpy(buffer + offset, &msg_type,
                                                sizeof(uint32_t));
                                         offset += sizeof(uint32_t);

                                         memcpy(buffer + offset, &elapsed,
                                                sizeof(float));
                                         offset += sizeof(float);

                                         memcpy(buffer + offset, pNals[i].payload, pNals[i].sizeBytes);
                                         offset += pNals[i].sizeBytes;

                                         // send the buffer
                                         if (user != NULL)
                                           if (user->ptr != NULL)
                                             if (user->ptr->active)
                                             {
                                               std::lock_guard<std::shared_mutex>
                                                   unique_access(user->ptr->mtx);
                                               ws->send(std::string_view(
                                                   buffer,
                                                   offset)); // by default
                                                             // uWS::OpCode::BINARY
                                             }

                                         free(buffer);
                                       }
                                     }

                                     // done with the planes
                                     picture->planes[0] = NULL;
                                     picture->planes[1] = NULL;

                                     picture->stride[0] = 0;
                                     picture->stride[1] = 0;
                                   }
                                 }

                                 auto end_t = steady_clock::now();

                                 double elapsedSeconds =
                                     ((end_t - start_t).count()) *
                                     steady_clock::period::num /
                                     static_cast<double>(
                                         steady_clock::period::den);
                                 double elapsedMilliseconds =
                                     1000.0 * elapsedSeconds;

                                 std::cout
                                     << "[uWS::video]::" << fits->dataset_id
                                     << "\tfps = " << fps
                                     << "\tbitrate = " << bitrate
                                     << "\t_frame = " << _frame
                                     << "\tframe_idx = " << frame_idx
                                     << "\telapsed = " << elapsedMilliseconds
                                     << " [ms]" << std::endl;
                               });

                           user->ptr->active_threads.add_thread(video_thread);
                         }
                       }
                     }

                     if (message.find("[image]") != std::string::npos)
                     {
                       auto now = system_clock::now();
                       user->ptr->ts = now;

                       float quality = 45;
                       int view_width = -1;
                       int view_height = -1;
                       double frame_start = 0;
                       double frame_end = 0;
                       double ref_freq = 0;
                       float timestamp = 0;

                       std::string_view query;
                       size_t pos = message.find_first_of(" ");

                       if (pos != std::string::npos)
                         query = message.substr(pos + 1, std::string::npos);

                       std::vector<std::string> params;
                       boost::split(params, query,
                                    [](char c) { return c == '&'; });

                       for (auto const &s : params)
                       {
                         // find '='
                         size_t pos = s.find("=");

                         if (pos != std::string::npos)
                         {
                           std::string key = s.substr(0, pos);
                           std::string value =
                               s.substr(pos + 1, std::string::npos);

                           if (key.find("quality") != std::string::npos)
                             quality = std::stof(value);

                           if (key.find("width") != std::string::npos)
                             view_width = std::stoi(value);

                           if (key.find("height") != std::string::npos)
                             view_height = std::stoi(value);

                           if (key.find("frame_start") != std::string::npos)
                             frame_start = std::stod(value);

                           if (key.find("frame_end") != std::string::npos)
                             frame_end = std::stod(value);

                           if (key.find("ref_freq") != std::string::npos)
                             ref_freq = std::stod(value);

                           if (key.find("timestamp") != std::string::npos)
                             timestamp = std::stof(value);
                         }
                       }

                       // process the response
                       /*std::cout << "query(" << datasetid <<
                         "::" << quality
                         << "::" << frame_start << "::" <<
                         frame_end
                         << "::" << ref_freq
                         << "::view <" << view_width << " x " <<
                         view_height
                         << ">::" << timestamp << ")" <<
                         std::endl;*/

                       auto fits = get_dataset(datasetid);

                       if (fits != nullptr)
                       {
                         if (!fits->has_error && fits->has_data)
                         {
                           // launch a separate thread
                           boost::thread *image_thread = new boost::thread(
                               [fits, ws, user, frame_start, frame_end,
                                ref_freq, quality, view_width, view_height,
                                timestamp]() {
                                 if (user == NULL)
                                   return;

                                 if (user->ptr == NULL)
                                   return;

                                 if (!user->ptr->active)
                                   return;

                                 fits->update_timestamp();

                                 int start, end;
                                 double elapsedMilliseconds = 0.0;

                                 fits->get_spectrum_range(frame_start,
                                                          frame_end, ref_freq,
                                                          start, end);

                                 // make image/spectrum/histogram (get a FITS
                                 // sub-cube)
                                 if (fits->depth > 1)
                                 {
                                   auto start_t = steady_clock::now();

                                   auto [_img_pixels, _img_mask, mean_spectrum,
                                         integrated_spectrum] =
                                       fits->get_cube(start, end);

                                   if (_img_pixels && _img_mask)
                                   {
                                     auto [min, max, median, black, white,
                                           sensitivity, ratio_sensitivity] =
                                         fits->make_cube_statistics(
                                             _img_pixels, _img_mask,
                                             user->ptr->hist);

                                     // replace NaNs with 0.0
                                     auto _pixels = _img_pixels.get();
                                     auto _mask = _img_mask.get();
                                     const size_t plane_size =
                                         fits->width * fits->height;

#pragma omp parallel for simd
                                     for (size_t i = 0; i < plane_size; i++)
                                       if (_mask[i] == 0)
                                         _pixels[i] = 0.0f;

                                     user->ptr->min = min;
                                     user->ptr->max = max;
                                     user->ptr->median = median;
                                     user->ptr->black = black;
                                     user->ptr->white = white;
                                     user->ptr->sensitivity = sensitivity;
                                     user->ptr->ratio_sensitivity =
                                         ratio_sensitivity;
                                   }

                                   auto end_t = steady_clock::now();

                                   double elapsedSeconds =
                                       ((end_t - start_t).count()) *
                                       steady_clock::period::num /
                                       static_cast<double>(
                                           steady_clock::period::den);
                                   elapsedMilliseconds +=
                                       1000.0 * elapsedSeconds;

                                   // set the new user {pixels,mask}
                                   if (_img_pixels)
                                     user->ptr->img_pixels = _img_pixels;

                                   if (_img_mask)
                                     user->ptr->img_mask = _img_mask;

                                   // send the updated mean_spectrum and
                                   // integrated_spectrum via WebSockets
                                   if ((mean_spectrum.size() > 0) &&
                                       (integrated_spectrum.size() > 0) &&
                                       (mean_spectrum.size() ==
                                        integrated_spectrum.size()))
                                   {
                                     std::cout << "[uWS] sending the "
                                                  "mean/integrated spectra"
                                               << std::endl;

                                     size_t bufferSize =
                                         sizeof(float) + sizeof(uint32_t) +
                                         sizeof(uint32_t) + sizeof(float) +
                                         sizeof(uint32_t) +
                                         mean_spectrum.size() * sizeof(float) +
                                         integrated_spectrum.size() *
                                             sizeof(float);

                                     char *buffer = (char *)malloc(bufferSize);

                                     if (buffer != NULL)
                                     {
                                       float ts = timestamp;
                                       uint32_t id = 0;
                                       uint32_t msg_type =
                                           3; // 0 - spectrum, 1 - viewport, 2 -
                                              // cube image + statistics, 3 -
                                              // full spectrum refresh
                                       uint32_t len = mean_spectrum.size();

                                       size_t offset = 0;

                                       memcpy(buffer + offset, &ts,
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset, &id,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       memcpy(buffer + offset, &msg_type,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       memcpy(buffer + offset, &len,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       memcpy(buffer + offset,
                                              mean_spectrum.data(),
                                              mean_spectrum.size() *
                                                  sizeof(float));
                                       offset +=
                                           mean_spectrum.size() * sizeof(float);

                                       memcpy(buffer + offset,
                                              integrated_spectrum.data(),
                                              integrated_spectrum.size() *
                                                  sizeof(float));
                                       offset += integrated_spectrum.size() *
                                                 sizeof(float);

                                       // send the buffer
                                       if (user != NULL)
                                         if (user->ptr != NULL)
                                           if (user->ptr->active)
                                           {
                                             std::lock_guard<std::shared_mutex>
                                                 unique_access(user->ptr->mtx);
                                             ws->send(std::string_view(
                                                 buffer,
                                                 offset)); // by default
                                                           // uWS::OpCode::BINARY
                                           }

                                       free(buffer);
                                     }
                                   }

                                   // send the updated image + statistics +
                                   // histogram via WebSockets
                                   if (_img_pixels && _img_mask)
                                   {
                                     // export the image pixels/mask to OpenEXR

                                     // in-memory output
                                     StdOSStream oss;
                                     std::string output;

                                     // calculate a new image size
                                     long true_width = fits->width;
                                     long true_height = fits->height;
                                     true_image_dimensions(_img_mask.get(),
                                                           true_width,
                                                           true_height);
                                     float scale = get_image_scale(
                                         view_width, view_height, true_width,
                                         true_height);

                                     if (scale < 1.0)
                                     {
                                       int img_width =
                                           floorf(scale * fits->width);
                                       int img_height =
                                           floorf(scale * fits->height);

                                       printf("FITS image scaling by %f; %ld x "
                                              "%ld --> %d x %d\n",
                                              scale, fits->width, fits->height,
                                              img_width, img_height);

                                       size_t plane_size = size_t(img_width) *
                                                           size_t(img_height);

                                       // allocate {pixel_buf, mask_buf}
                                       std::shared_ptr<Ipp32f> pixels_buf(
                                           ippsMalloc_32f_L(plane_size),
                                           ippsFree);
                                       std::shared_ptr<Ipp8u> mask_buf(
                                           ippsMalloc_8u_L(plane_size),
                                           ippsFree);
                                       std::shared_ptr<Ipp32f> mask_buf_32f(
                                           ippsMalloc_32f_L(plane_size),
                                           ippsFree);

                                       if (pixels_buf.get() != NULL &&
                                           mask_buf.get() != NULL &&
                                           mask_buf_32f.get() != NULL)
                                       {
                                         // downsize float32 pixels and a mask
                                         IppiSize srcSize;
                                         srcSize.width = fits->width;
                                         srcSize.height = fits->height;
                                         Ipp32s srcStep = srcSize.width;

                                         IppiSize dstSize;
                                         dstSize.width = img_width;
                                         dstSize.height = img_height;
                                         Ipp32s dstStep = dstSize.width;

                                         IppStatus pixels_stat =
                                             tileResize32f_C1R(
                                                 _img_pixels.get(), srcSize,
                                                 srcStep, pixels_buf.get(),
                                                 dstSize, dstStep);

                                         IppStatus mask_stat = tileResize8u_C1R(
                                             _img_mask.get(), srcSize, srcStep,
                                             mask_buf.get(), dstSize, dstStep);

                                         printf(" %d : %s, %d : %s\n",
                                                pixels_stat,
                                                ippGetStatusString(pixels_stat),
                                                mask_stat,
                                                ippGetStatusString(mask_stat));

                                         // compress the pixels + mask with
                                         // OpenEXR
                                         if (pixels_stat == ippStsNoErr &&
                                             mask_stat == ippStsNoErr)
                                         {
                                           // the mask should be filled-in
                                           // manually based on NaN pixels not
                                           // anymore, NaN will be replaced by
                                           // 0.0 due to unwanted cropping by
                                           // OpenEXR
                                           Ipp32f *pixels = pixels_buf.get();
                                           Ipp8u *src_mask = mask_buf.get();
                                           Ipp32f *mask = mask_buf_32f.get();

#pragma omp parallel for simd
                                           for (size_t i = 0; i < plane_size;
                                                i++)
                                             mask[i] =
                                                 (src_mask[i] == 255)
                                                     ? 1.0f
                                                     : 0.0f; // std::isnan(pixels[i])
                                                             // ? 0.0f : 1.0f;

                                           // export EXR in a YA format

                                           try
                                           {
                                             Header header(img_width,
                                                           img_height);
                                             header.compression() =
                                                 DWAB_COMPRESSION;
                                             addDwaCompressionLevel(header,
                                                                    quality);
                                             header.channels().insert(
                                                 "Y", Channel(FLOAT));
                                             header.channels().insert(
                                                 "A", Channel(FLOAT));

                                             OutputFile file(oss, header);
                                             FrameBuffer frameBuffer;

                                             frameBuffer.insert(
                                                 "Y",
                                                 Slice(FLOAT, (char *)pixels,
                                                       sizeof(Ipp32f) * 1,
                                                       sizeof(Ipp32f) *
                                                           img_width));

                                             frameBuffer.insert(
                                                 "A", Slice(FLOAT, (char *)mask,
                                                            sizeof(Ipp32f) * 1,
                                                            sizeof(Ipp32f) *
                                                                img_width));

                                             file.setFrameBuffer(frameBuffer);
                                             file.writePixels(img_height);
                                           }
                                           catch (const std::exception &exc)
                                           {
                                             std::cerr << exc.what()
                                                       << std::endl;
                                           }

                                           output = oss.str();
                                           std::cout << "[" << fits->dataset_id
                                                     << "]::"
                                                        "down"
                                                        "size"
                                                        " Ope"
                                                        "nEXR"
                                                        " out"
                                                        "put:"
                                                        " "
                                                     << output.length()
                                                     << " byt"
                                                        "es."
                                                     << std::endl;
                                         }
                                       }
                                     }
                                     else
                                     {
                                       // mirror-flip
                                       // the
                                       // pixels_buf,
                                       // compress
                                       // with
                                       // OpenEXR
                                       // and
                                       // transmit
                                       // at
                                       // its
                                       // original
                                       // scale
                                       int img_width = fits->width;
                                       int img_height = fits->height;

                                       size_t plane_size = size_t(img_width) *
                                                           size_t(img_height);

                                       // an
                                       // array
                                       // to
                                       // hold
                                       // a
                                       // flipped
                                       // image
                                       // (its
                                       // mirror
                                       // image)
                                       /*std::shared_ptr<Ipp32f>
                                             pixels_buf(ippsMalloc_32f_L(plane_size),
                                             ippsFree);*/

                                       // an
                                       // alpha
                                       // channel
                                       std::shared_ptr<Ipp32f> mask_buf(
                                           ippsMalloc_32f_L(plane_size),
                                           ippsFree);

                                       // copy
                                       // and
                                       // flip
                                       // the
                                       // image,
                                       // fill-in
                                       // the
                                       // mask
                                       if (/*pixels_buf.get()
                                                  != NULL
                                                  &&*/
                                           mask_buf.get() != NULL)
                                       {
                                         /*tileMirror32f_C1R(fits->img_pixels,
                                               pixels_buf.get(),
                                               img_width,
                                               img_height);*/

                                         // the
                                         // mask
                                         // should
                                         // be
                                         // filled-in
                                         // manually
                                         // based
                                         // on
                                         // NaN
                                         // pixels
                                         Ipp32f *pixels =
                                             _img_pixels
                                                 .get(); // pixels_buf.get();
                                         Ipp8u *_mask = _img_mask.get();
                                         Ipp32f *mask = mask_buf.get();

#pragma omp parallel for simd
                                         for (size_t i = 0; i < plane_size; i++)
                                           mask[i] =
                                               (_mask[i] == 255)
                                                   ? 1.0f
                                                   : 0.0f; // std::isnan(pixels[i])
                                                           // ? 0.0f : 1.0f;

                                         // export
                                         // the
                                         // luma+mask
                                         // to
                                         // OpenEXR

                                         try
                                         {
                                           Header header(img_width, img_height);
                                           header.compression() =
                                               DWAB_COMPRESSION;
                                           addDwaCompressionLevel(header,
                                                                  quality);
                                           header.channels().insert(
                                               "Y", Channel(FLOAT));
                                           header.channels().insert(
                                               "A", Channel(FLOAT));

                                           OutputFile file(oss, header);
                                           FrameBuffer frameBuffer;

                                           frameBuffer.insert(
                                               "Y", Slice(FLOAT, (char *)pixels,
                                                          sizeof(Ipp32f) * 1,
                                                          sizeof(Ipp32f) *
                                                              img_width));

                                           frameBuffer.insert(
                                               "A", Slice(FLOAT, (char *)mask,
                                                          sizeof(Ipp32f) * 1,
                                                          sizeof(Ipp32f) *
                                                              img_width));

                                           file.setFrameBuffer(frameBuffer);
                                           file.writePixels(img_height);
                                         }
                                         catch (const std::exception &exc)
                                         {
                                           std::cerr << exc.what() << std::endl;
                                         }

                                         output = oss.str();
                                         std::cout
                                             << "[" << fits->dataset_id
                                             << "]::mirror OpenEXR output: "
                                             << output.length() << " bytes."
                                             << std::endl;
                                       }
                                     }

                                     std::cout << "[uWS] sending the cube "
                                                  "image + statistics"
                                               << std::endl;

                                     size_t bufferSize =
                                         sizeof(float) + 2 * sizeof(uint32_t);
                                     bufferSize += 7 * sizeof(float) +
                                                   sizeof(uint32_t) +
                                                   NBINS * sizeof(uint32_t);
                                     // append the image frame too
                                     bufferSize += output.length();

                                     char *buffer = (char *)malloc(bufferSize);

                                     if (buffer != NULL)
                                     {
                                       float ts = timestamp;
                                       uint32_t id = 0;
                                       uint32_t msg_type =
                                           2; // 0 - spectrum, 1 - viewport, 2 -
                                              // cube image + statistics, 3 -
                                              // full spectrum refresh
                                       uint32_t len = NBINS;

                                       size_t offset = 0;

                                       memcpy(buffer + offset, &ts,
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset, &id,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       memcpy(buffer + offset, &msg_type,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       // tone mapping (7 floats)
                                       memcpy(buffer + offset,
                                              &(user->ptr->min), sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset,
                                              &(user->ptr->max), sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset,
                                              &(user->ptr->median),
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset,
                                              &(user->ptr->black),
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset,
                                              &(user->ptr->white),
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset,
                                              &(user->ptr->sensitivity),
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset,
                                              &(user->ptr->ratio_sensitivity),
                                              sizeof(float));
                                       offset += sizeof(float);

                                       // the histogram length
                                       memcpy(buffer + offset, &len,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       // the histogram bins
                                       memcpy(buffer + offset, user->ptr->hist,
                                              NBINS * sizeof(uint32_t));
                                       offset += NBINS * sizeof(uint32_t);

                                       // the OpenEXR image frame
                                       if (output.length() > 0)
                                       {
                                         memcpy(buffer + offset, output.c_str(),
                                                output.length());
                                         offset += output.length();
                                       }

                                       // send the buffer
                                       if (user != NULL)
                                         if (user->ptr != NULL)
                                           if (user->ptr->active)
                                           {
                                             std::lock_guard<std::shared_mutex>
                                                 unique_access(user->ptr->mtx);
                                             ws->send(std::string_view(
                                                 buffer,
                                                 offset)); // by default
                                                           // uWS::OpCode::BINARY
                                           }

                                       free(buffer);
                                     }
                                   }
                                 }
                               });

                           user->ptr->active_threads.add_thread(image_thread);
                         }
                       }
                     }

                     if (message.find("[realtime_image_spectrum]") !=
                         std::string::npos)
                     {
                       // get deltat (no need to lock the mutex at
                       // this point)
                       auto now = system_clock::now();
                       duration<double, std::milli> deltat =
                           now - user->ptr->ts;
                       user->ptr->ts = now;

                       int seq = -1;
                       int dx = 0;
                       float quality = 45;
                       bool image_update = false;
                       int x1 = -1;
                       int x2 = -1;
                       int y1 = -1;
                       int y2 = -1;
                       int view_width = -1;
                       int view_height = -1;
                       double frame_start = 0;
                       double frame_end = 0;
                       double ref_freq = 0;
                       float timestamp = 0;
                       intensity_mode intensity = mean;
                       beam_shape beam = square;

                       std::string_view query;
                       size_t pos = message.find_first_of(" ");

                       if (pos != std::string::npos)
                         query = message.substr(pos + 1, std::string::npos);

                       std::vector<std::string> params;
                       boost::split(params, query,
                                    [](char c) { return c == '&'; });

                       for (auto const &s : params)
                       {
                         // find '='
                         size_t pos = s.find("=");

                         if (pos != std::string::npos)
                         {
                           std::string key = s.substr(0, pos);
                           std::string value =
                               s.substr(pos + 1, std::string::npos);

                           if (key.find("seq") != std::string::npos)
                             seq = std::stoi(value);

                           if (key.find("dx") != std::string::npos)
                             dx = std::stoi(value);

                           if (key.find("quality") != std::string::npos)
                             quality = std::stof(value);

                           if (key.find("image") != std::string::npos)
                           {
                             if (value == "true")
                               image_update = true;
                           }

                           if (key.find("width") != std::string::npos)
                             view_width = std::stoi(value);

                           if (key.find("height") != std::string::npos)
                             view_height = std::stoi(value);

                           if (key.find("x1") != std::string::npos)
                             x1 = std::stoi(value);

                           if (key.find("x2") != std::string::npos)
                             x2 = std::stoi(value);

                           if (key.find("y1") != std::string::npos)
                             y1 = std::stoi(value);

                           if (key.find("y2") != std::string::npos)
                             y2 = std::stoi(value);

                           if (key.find("frame_start") != std::string::npos)
                             frame_start = std::stod(value);

                           if (key.find("frame_end") != std::string::npos)
                             frame_end = std::stod(value);

                           if (key.find("ref_freq") != std::string::npos)
                             ref_freq = std::stod(value);

                           if (key.find("timestamp") != std::string::npos)
                             timestamp = std::stof(value);

                           if (key.find("beam") != std::string::npos)
                             beam = (strcasecmp("circle", value.c_str()) == 0)
                                        ? circle
                                        : square;

                           if (key.find("intensity") != std::string::npos)
                             intensity =
                                 (strcasecmp("integrated", value.c_str()) == 0)
                                     ? integrated
                                     : mean;
                         }
                       }

                       // process the response
                       /*std::cout << "query(" << datasetid <<
                         "::" << dx <<
                         "::" << quality
                         << "::" << (image_update ? "true" :
                         "false") << "::<X:> "
                         << x1 << ".." << x2 << ",Y:> " << y1 <<
                         ".." << y2
                         << ">::" << frame_start << "::" <<
                         frame_end
                         << "::" << ref_freq
                         << "::view <" << view_width << " x " <<
                         view_height
                         << ">::" << (beam == circle ? "circle" :
                         "square")
                         << "::" << (intensity == integrated ?
                         "integrated" : "mean")
                         << "::" << seq << "::" << timestamp <<
                         ")"
                         << std::endl;*/

                       auto fits = get_dataset(datasetid);

                       if (fits != nullptr)
                       {
                         if (!fits->has_error && fits->has_data)
                         {
                           // launch a separate thread
                           boost::thread *spectrum_thread = new boost::thread(
                               [fits, ws, user, frame_start, frame_end,
                                ref_freq, image_update, quality, dx, x1, x2, y1,
                                y2, view_width, view_height, intensity, beam,
                                timestamp, seq, deltat]() {
                                 if (user == NULL)
                                   return;

                                 if (user->ptr == NULL)
                                   return;

                                 if (!user->ptr->active)
                                   return;

                                 fits->update_timestamp();

                                 {
                                   // gain unique
                                   // access
                                   std::lock_guard<std::shared_mutex>
                                       unique_access(user->ptr->mtx);

                                   int last_seq = user->ptr->last_seq;

                                   if (last_seq > seq)
                                   {
                                     printf("skippin"
                                            "g an "
                                            "old "
                                            "frame "
                                            "(%d < "
                                            "%d)\n",
                                            seq, last_seq);
                                     return;
                                   }

                                   user->ptr->last_seq = seq;
                                 }

                                 // copy over the
                                 // default
                                 // {pixels,mask}
                                 // plus
                                 // statistics
                                 {
                                   if (!user->ptr->img_pixels)
                                     user->ptr->img_pixels = fits->img_pixels;

                                   if (!user->ptr->img_mask)
                                     user->ptr->img_mask = fits->img_mask;

                                   user->ptr->min = fits->min;
                                   user->ptr->max = fits->max;
                                   user->ptr->median = fits->median;
                                   user->ptr->black = fits->black;
                                   user->ptr->white = fits->white;
                                   user->ptr->sensitivity = fits->sensitivity;
                                   user->ptr->ratio_sensitivity =
                                       fits->ratio_sensitivity;

                                   for (auto i = 0; i < NBINS; i++)
                                     user->ptr->hist[i] = fits->hist[i];
                                 }

                                 int start, end;
                                 double elapsedMilliseconds;

                                 fits->get_spectrum_range(frame_start,
                                                          frame_end, ref_freq,
                                                          start, end);

                                 // send the
                                 // compressed
                                 // viewport
                                 if (image_update && view_width > 0 &&
                                     view_height > 0)
                                 {
                                   auto start_t = steady_clock::now();

                                   Ipp32f *img_pixels =
                                       user->ptr->img_pixels.get();
                                   Ipp8u *img_mask = user->ptr->img_mask.get();

                                   const int dimx = abs(x2 - x1 + 1);
                                   const int dimy = abs(y2 - y1 + 1);

                                   size_t native_size =
                                       size_t(dimx) * size_t(dimy);
                                   std::shared_ptr<Ipp32f> view_pixels(
                                       ippsMalloc_32f_L(native_size), ippsFree);
                                   std::shared_ptr<Ipp32f> view_mask(
                                       ippsMalloc_32f_L(native_size), ippsFree);

                                   size_t dst_offset = 0;
                                   Ipp32f *_pixels = view_pixels.get();
                                   Ipp32f *_mask = view_mask.get();

                                   // the loop
                                   // could be
                                   // parallelised
                                   for (int j = y1; j <= y2; j++)
                                   {
                                     size_t src_offset = j * fits->width;

                                     for (int i = x1; i <= x2; i++)
                                     {
                                       // a dark
                                       // (inactive)
                                       // pixel by
                                       // default
                                       Ipp32f pixel = 0.0f;
                                       Ipp32f mask = 0.0f;

                                       if ((i >= 0) && (i < fits->width) &&
                                           (j >= 0) && (j < fits->height))
                                       {
                                         pixel = img_pixels[src_offset + i];
                                         mask =
                                             (img_mask[src_offset + i] == 255)
                                                 ? 1.0f
                                                 : 0.0f;
                                       }

                                       _pixels[dst_offset] = pixel;
                                       _mask[dst_offset] = mask;
                                       dst_offset++;
                                     }
                                   }

                                   assert(dst_offset == native_size);

                                   // downsize
                                   // when
                                   // necessary to
                                   // view_width
                                   // x
                                   // view_height
                                   size_t viewport_size =
                                       size_t(view_width) * size_t(view_height);

                                   if (native_size > viewport_size)
                                   {
                                     printf("downsiz"
                                            "ing "
                                            "viewpor"
                                            "t %d x "
                                            "%d "
                                            "--> %d "
                                            "x %d\n",
                                            dimx, dimy, view_width,
                                            view_height);

                                     std::shared_ptr<Ipp32f> pixels_buf(
                                         ippsMalloc_32f_L(viewport_size),
                                         ippsFree);
                                     std::shared_ptr<Ipp32f> mask_buf(
                                         ippsMalloc_32f_L(viewport_size),
                                         ippsFree);

                                     if (pixels_buf.get() != NULL &&
                                         mask_buf.get() != NULL)
                                     {
                                       // downsize
                                       // float32
                                       // pixels
                                       // and a
                                       // mask
                                       IppiSize srcSize;
                                       srcSize.width = dimx;
                                       srcSize.height = dimy;
                                       Ipp32s srcStep = srcSize.width;

                                       IppiSize dstSize;
                                       dstSize.width = view_width;
                                       dstSize.height = view_height;
                                       Ipp32s dstStep = dstSize.width;

                                       IppStatus pixels_stat =
                                           tileResize32f_C1R(_pixels, srcSize,
                                                             srcStep,
                                                             pixels_buf.get(),
                                                             dstSize, dstStep);

                                       IppStatus mask_stat = tileResize32f_C1R(
                                           _mask, srcSize, srcStep,
                                           mask_buf.get(), dstSize, dstStep);

                                       printf(" %d "
                                              ": "
                                              "%s, "
                                              "%d : "
                                              "%s\n",
                                              pixels_stat,
                                              ippGetStatusString(pixels_stat),
                                              mask_stat,
                                              ippGetStatusString(mask_stat));

                                       // export
                                       // EXR in a
                                       // YA
                                       // format
                                       if (pixels_stat == ippStsNoErr &&
                                           mask_stat == ippStsNoErr)
                                       {
                                         // in-memory
                                         // output
                                         StdOSStream oss;

                                         try
                                         {
                                           Header header(view_width,
                                                         view_height);
                                           header.compression() =
                                               DWAB_COMPRESSION;
                                           addDwaCompressionLevel(header,
                                                                  quality);
                                           header.channels().insert(
                                               "Y", Channel(FLOAT));
                                           header.channels().insert(
                                               "A", Channel(FLOAT));

                                           // OutputFile
                                           // file(filename.c_str(),
                                           // header);
                                           OutputFile file(oss, header);
                                           FrameBuffer frameBuffer;

                                           frameBuffer.insert(
                                               "Y",
                                               Slice(FLOAT,
                                                     (char *)pixels_buf.get(),
                                                     sizeof(Ipp32f) * 1,
                                                     sizeof(Ipp32f) *
                                                         view_width));

                                           frameBuffer.insert(
                                               "A",
                                               Slice(FLOAT,
                                                     (char *)mask_buf.get(),
                                                     sizeof(Ipp32f) * 1,
                                                     sizeof(Ipp32f) *
                                                         view_width));

                                           file.setFrameBuffer(frameBuffer);
                                           file.writePixels(view_height);
                                         }
                                         catch (const std::exception &exc)
                                         {
                                           std::cerr << exc.what() << std::endl;
                                         }

                                         std::string output = oss.str();
                                         std::cout << "[" << fits->dataset_id
                                                   << "]::viewport OpenEXR "
                                                      "output: "
                                                   << output.length()
                                                   << " bytes." << std::endl;

                                         auto end_t = steady_clock::now();

                                         double elapsedSeconds =
                                             ((end_t - start_t).count()) *
                                             steady_clock::period::num /
                                             static_cast<double>(
                                                 steady_clock::period::den);
                                         double elapsedMs =
                                             1000.0 * elapsedSeconds;

                                         std::cout << "downsizing/compressing "
                                                      "the viewport elapsed "
                                                      "time: "
                                                   << elapsedMs << " [ms]"
                                                   << std::endl;

                                         // send
                                         // the
                                         // viewport
                                         if (output.length() > 0)
                                         {
                                           size_t bufferSize =
                                               sizeof(float) +
                                               sizeof(uint32_t) +
                                               sizeof(uint32_t) +
                                               output.length();
                                           char *buffer =
                                               (char *)malloc(bufferSize);

                                           if (buffer != NULL)
                                           {
                                             float ts = timestamp;
                                             uint32_t id = seq;
                                             uint32_t msg_type =
                                                 1; // 0 - spectrum, 1 -
                                             // viewport,
                                             // 2
                                             // -
                                             // image,
                                             // 3
                                             // -
                                             // full
                                             // spectrum
                                             // refresh,
                                             // 4
                                             // -
                                             // histogram
                                             size_t offset = 0;

                                             memcpy(buffer + offset, &ts,
                                                    sizeof(float));
                                             offset += sizeof(float);

                                             memcpy(buffer + offset, &id,
                                                    sizeof(uint32_t));
                                             offset += sizeof(uint32_t);

                                             memcpy(buffer + offset, &msg_type,
                                                    sizeof(uint32_t));
                                             offset += sizeof(uint32_t);

                                             memcpy(buffer + offset,
                                                    output.c_str(),
                                                    output.length());
                                             offset += output.length();

                                             if (user != NULL)
                                               if (user->ptr != NULL)
                                                 if (user->ptr->active)
                                                 {
                                                   std::lock_guard<
                                                       std::shared_mutex>
                                                       unique_access(
                                                           user->ptr->mtx);
                                                   if (seq ==
                                                       user->ptr->last_seq)
                                                     ws->send(std::string_view(
                                                         buffer,
                                                         offset)); // by default
                                                   // uWS::OpCode::BINARY
                                                 }

                                             free(buffer);
                                           }
                                         }
                                       }
                                     }
                                   }
                                   else
                                   // no
                                   // re-scaling
                                   // needed
                                   {
                                     // export the
                                     // luma+mask
                                     // to OpenEXR

                                     // in-memory
                                     // output
                                     StdOSStream oss;

                                     try
                                     {
                                       Header header(dimx, dimy);
                                       header.compression() = DWAB_COMPRESSION;
                                       addDwaCompressionLevel(header, quality);
                                       header.channels().insert("Y",
                                                                Channel(FLOAT));
                                       header.channels().insert("A",
                                                                Channel(FLOAT));

                                       OutputFile file(oss, header);
                                       FrameBuffer frameBuffer;

                                       frameBuffer.insert(
                                           "Y", Slice(FLOAT, (char *)_pixels,
                                                      sizeof(Ipp32f) * 1,
                                                      sizeof(Ipp32f) * dimx));

                                       frameBuffer.insert(
                                           "A", Slice(FLOAT, (char *)_mask,
                                                      sizeof(Ipp32f) * 1,
                                                      sizeof(Ipp32f) * dimx));

                                       file.setFrameBuffer(frameBuffer);
                                       file.writePixels(dimy);
                                     }
                                     catch (const std::exception &exc)
                                     {
                                       std::cerr << exc.what() << std::endl;
                                     }

                                     std::string output = oss.str();
                                     std::cout << "[" << fits->dataset_id
                                               << "]::"
                                                  "view"
                                                  "port"
                                                  " Ope"
                                                  "nEXR"
                                                  " out"
                                                  "put:"
                                                  " "
                                               << output.length()
                                               << " byt"
                                                  "es."
                                               << std::endl;

                                     auto end_t = steady_clock::now();

                                     double elapsedSeconds =
                                         ((end_t - start_t).count()) *
                                         steady_clock::period::num /
                                         static_cast<double>(
                                             steady_clock::period::den);
                                     double elapsedMs = 1000.0 * elapsedSeconds;

                                     std::cout << "comp"
                                                  "ress"
                                                  "ing "
                                                  "the "
                                                  "view"
                                                  "port"
                                                  " ela"
                                                  "psed"
                                                  " tim"
                                                  "e: "
                                               << elapsedMs
                                               << " ["
                                                  "ms]"
                                               << std::endl;

                                     // send the
                                     // viewport
                                     if (output.length() > 0)
                                     {
                                       size_t bufferSize =
                                           sizeof(float) + sizeof(uint32_t) +
                                           sizeof(uint32_t) + output.length();
                                       char *buffer =
                                           (char *)malloc(bufferSize);

                                       if (buffer != NULL)
                                       {
                                         float ts = timestamp;
                                         uint32_t id = seq;
                                         uint32_t msg_type =
                                             1; // 0 - spectrum, 1 -
                                         // viewport,
                                         // 2 -
                                         // image,
                                         // 3 -
                                         // full
                                         // spectrum
                                         // refresh,
                                         // 4
                                         // -
                                         // histogram
                                         size_t offset = 0;

                                         memcpy(buffer + offset, &ts,
                                                sizeof(float));
                                         offset += sizeof(float);

                                         memcpy(buffer + offset, &id,
                                                sizeof(uint32_t));
                                         offset += sizeof(uint32_t);

                                         memcpy(buffer + offset, &msg_type,
                                                sizeof(uint32_t));
                                         offset += sizeof(uint32_t);

                                         memcpy(buffer + offset, output.c_str(),
                                                output.length());
                                         offset += output.length();

                                         if (user != NULL)
                                           if (user->ptr != NULL)
                                             if (user->ptr->active)
                                             {
                                               std::lock_guard<
                                                   std::shared_mutex>
                                                   unique_access(
                                                       user->ptr->mtx);
                                               if (seq == user->ptr->last_seq)
                                                 ws->send(std::string_view(
                                                     buffer,
                                                     offset)); // by default
                                               // uWS::OpCode::BINARY
                                             }

                                         free(buffer);
                                       }
                                     }
                                   }
                                 }

                                 // calculate a
                                 // viewport
                                 // spectrum
                                 if (fits->depth > 1)
                                 {
                                   auto start_watch = steady_clock::now();

                                   std::vector<float> spectrum =
                                       fits->get_spectrum(ws, start, end, x1, y1,
                                                          x2, y2, intensity,
                                                          beam,
                                                          elapsedMilliseconds);

                                   std::cout << "spectr"
                                                "um "
                                                "length"
                                                " = "
                                             << spectrum.size()
                                             << " elaps"
                                                "ed "
                                                "time: "
                                             << elapsedMilliseconds << " [ms]"
                                             << std::endl;

                                   unsigned int dst_len = dx / 2;

                                   if (spectrum.size() > dst_len)
                                   {
                                     auto start_t = steady_clock::now();

                                     SpectrumPoint in[spectrum.size()];

                                     for (unsigned int i = 0;
                                          i < spectrum.size(); i++)
                                     {
                                       in[i].x = i;
                                       in[i].y = spectrum[i];
                                     }

                                     SpectrumPoint out[dst_len];

                                     PointLttb::Downsample(in, spectrum.size(),
                                                           out, dst_len);

                                     spectrum.resize(dst_len);

                                     for (unsigned int i = 0; i < dst_len; i++)
                                       spectrum[i] = out[i].y;

                                     auto end_t = steady_clock::now();

                                     double elapsedSeconds =
                                         ((end_t - start_t).count()) *
                                         steady_clock::period::num /
                                         static_cast<double>(
                                             steady_clock::period::den);
                                     double elapsedMs = 1000.0 * elapsedSeconds;

                                     std::cout << "down"
                                                  "samp"
                                                  "ling"
                                                  " the"
                                                  " "
                                                  "spec"
                                                  "trum"
                                                  " wit"
                                                  "h "
                                                  "'lar"
                                                  "gest"
                                                  "Tria"
                                                  "ngle"
                                                  "Thre"
                                                  "eBuc"
                                                  "kets"
                                                  "', "
                                                  "elap"
                                                  "sed "
                                                  "time"
                                                  ": "
                                               << elapsedMs
                                               << " ["
                                                  "ms]"
                                               << std::endl;

                                     elapsedMilliseconds += elapsedMs;
                                   }

                                   // send the
                                   // spectrum
                                   if (spectrum.size() > 0)
                                   {
                                     // compress
                                     // spectrum
                                     // with fpzip
                                     uint32_t spec_len = spectrum.size();
                                     size_t bufbytes =
                                         1024 + spec_len * sizeof(float);
                                     size_t outbytes = 0;
                                     bool success = false;

                                     void *compressed = malloc(bufbytes);

                                     if (compressed != NULL)
                                     {
                                       int prec = image_update
                                                      ? 24
                                                      : 16; // use a higher
                                       // precision
                                       // for
                                       // still
                                       // updates,
                                       // and
                                       // fewer
                                       // bytes
                                       // for
                                       // dynamic
                                       // spectra

                                       /* compress
                                        * to
                                        * memory
                                        */
                                       FPZ *fpz = fpzip_write_to_buffer(
                                           compressed, bufbytes);
                                       fpz->type = FPZIP_TYPE_FLOAT;
                                       fpz->prec = prec;
                                       fpz->nx = spec_len;
                                       fpz->ny = 1;
                                       fpz->nz = 1;
                                       fpz->nf = 1;

                                       /* write
                                        * header
                                        */
                                       if (!fpzip_write_header(fpz))
                                         fprintf(stderr,
                                                 "can"
                                                 "not"
                                                 " wr"
                                                 "ite"
                                                 " he"
                                                 "ade"
                                                 "r: "
                                                 "%s"
                                                 "\n",
                                                 fpzip_errstr[fpzip_errno]);
                                       else
                                       {
                                         outbytes =
                                             fpzip_write(fpz, spectrum.data());

                                         if (!outbytes)
                                           fprintf(stderr,
                                                   "c"
                                                   "o"
                                                   "m"
                                                   "p"
                                                   "r"
                                                   "e"
                                                   "s"
                                                   "s"
                                                   "i"
                                                   "o"
                                                   "n"
                                                   " "
                                                   "f"
                                                   "a"
                                                   "i"
                                                   "l"
                                                   "e"
                                                   "d"
                                                   ":"
                                                   " "
                                                   "%"
                                                   "s"
                                                   "\n",
                                                   fpzip_errstr[fpzip_errno]);
                                         else
                                           success = true;
                                       }

                                       fpzip_write_close(fpz);
                                     }

                                     if (success)
                                       std::cout << "FP"
                                                    "ZI"
                                                    "P-"
                                                    "co"
                                                    "mp"
                                                    "re"
                                                    "ss"
                                                    "ed"
                                                    " s"
                                                    "pe"
                                                    "ct"
                                                    "ru"
                                                    "m:"
                                                    " "
                                                 << outbytes
                                                 << " b"
                                                    "yt"
                                                    "es"
                                                    ", "
                                                    "or"
                                                    "ig"
                                                    "in"
                                                    "al"
                                                    " s"
                                                    "iz"
                                                    "e "
                                                 << spec_len * sizeof(float)
                                                 << " b"
                                                    "yt"
                                                    "es"
                                                    "."
                                                 << std::endl;
                                     // end-of-compression

                                     size_t bufferSize =
                                         sizeof(float) + sizeof(float) +
                                         sizeof(uint32_t) + sizeof(uint32_t) +
                                         outbytes; //+ spectrum.size() *
                                     // sizeof(float);
                                     char *buffer = (char *)malloc(bufferSize);

                                     // construct
                                     // a message
                                     if (buffer != NULL && success)
                                     {
                                       auto end_watch = steady_clock::now();

                                       double elapsedSeconds =
                                           ((end_watch - start_watch).count()) *
                                           steady_clock::period::num /
                                           static_cast<double>(
                                               steady_clock::period::den);
                                       double elapsedMs =
                                           1000.0 * elapsedSeconds;

                                       float ts = timestamp;
                                       uint32_t id = seq;
                                       uint32_t msg_type =
                                           0; // 0 - spectrum, 1 - viewport,
                                       // 2 -
                                       // image, 3
                                       // - full
                                       // spectrum
                                       // refresh,
                                       // 4 -
                                       // histogram
                                       float elapsed = elapsedMs;

                                       size_t offset = 0;

                                       memcpy(buffer + offset, &ts,
                                              sizeof(float));
                                       offset += sizeof(float);

                                       memcpy(buffer + offset, &id,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       memcpy(buffer + offset, &msg_type,
                                              sizeof(uint32_t));
                                       offset += sizeof(uint32_t);

                                       memcpy(buffer + offset, &elapsed,
                                              sizeof(float));
                                       offset += sizeof(float);

                                       /*memcpy(buffer
                                         + offset,
                                         spectrum.data(),
                                         spectrum.size()
                                         *
                                         sizeof(float));
                                         offset +=
                                         spectrum.size()
                                         * sizeof(float);*/
                                       memcpy(buffer + offset, compressed,
                                              outbytes);
                                       offset += outbytes;

                                       if (user != NULL)
                                         if (user->ptr != NULL)
                                           if (user->ptr->active)
                                           {
                                             std::lock_guard<std::shared_mutex>
                                                 unique_access(user->ptr->mtx);
                                             if (seq == user->ptr->last_seq)
                                               ws->send(std::string_view(
                                                   buffer,
                                                   offset)); // by default
                                             // uWS::OpCode::BINARY
                                           }
                                     }

                                     if (buffer != NULL)
                                       free(buffer);

                                     if (compressed != NULL)
                                       free(compressed);
                                   }

                                   if (user == NULL)
                                     return;

                                   if (user->ptr == NULL)
                                     return;

                                   if (!user->ptr->active)
                                     return;

                                   // track the
                                   // mouse with
                                   // the Kalman
                                   // Filter
                                   double pos_x = 0.5 * double(x1 + x2);
                                   double pos_y = 0.5 * double(y1 + y2);

                                   if (user->ptr->kal_x && user->ptr->kal_y)
                                   {
                                     KalmanFilter *kal_x =
                                         user->ptr->kal_x.get();
                                     KalmanFilter *kal_y =
                                         user->ptr->kal_y.get();

                                     /* update the
                                      * x and y
                                      * positions
                                      */
                                     kal_x->update(pos_x, deltat.count());
                                     kal_y->update(pos_y, deltat.count());

                                     // predict
                                     // the
                                     // positions
                                     // one second
                                     // ahead
                                     double look_ahead = 1000.0; // [ms]
                                     double pred_x =
                                         kal_x->predict(pos_x, look_ahead);
                                     double pred_y =
                                         kal_y->predict(pos_y, look_ahead);

                                     double dx = pred_x - pos_x;
                                     double dy = pred_y - pos_y;

#ifdef DEBUG
                                     printf("[%s]::"
                                            "KalmanF"
                                            "ilter: "
                                            "X: %f, "
                                            "Y: "
                                            "%f,"
                                            "\tpredi"
                                            "cted "
                                            "after "
                                            "1s X*: "
                                            "%f, "
                                            "Y*: "
                                            "%f, dx "
                                            ": %f, "
                                            "dy : "
                                            "%f\n",
                                            fits->dataset_id.c_str(), pos_x,
                                            pos_y, pred_x, pred_y, dx, dy);
#endif

                                     // pre-empt
                                     // cache
                                     int _x1 = x1 + dx;
                                     int _y1 = y1 + dy;
                                     int _x2 = x2 + dx;
                                     int _y2 = y2 + dy;

                                     fits->preempt_cache(start, end, _x1, _y1,
                                                         _x2, _y2);
                                   }
                                   else
                                   {
                                     if (!user->ptr->kal_x)
                                       user->ptr->kal_x =
                                           std::shared_ptr<KalmanFilter>(
                                               new KalmanFilter(pos_x, false));

                                     if (!user->ptr->kal_y)
                                       user->ptr->kal_y =
                                           std::shared_ptr<KalmanFilter>(
                                               new KalmanFilter(pos_y, false));
                                   }
                                 }

                                 // remove itself
                                 // from the list
                                 // of active
                                 // threads
                                 /*std::lock_guard<std::shared_mutex>
                                   unique_session(user->ptr->mtx);
                                   user->ptr->active_threads.erase(tid);*/
                               }); //.detach();

                           user->ptr->active_threads.add_thread(
                               spectrum_thread);
                         }
                       }
                     }

                     /*if (message.find("image/") !=
                       std::string::npos) { int width, height;

                       sscanf(std::string(message).c_str(),
                       "image/%d/%d", &width, &height);

                       PrintThread{}  << datasetid <<
                       "::get_image::<" << width << "x" << height
                       << ">" << std::endl;

                       if(width != 0 && height != 0) {
                       std::shared_lock<std::shared_mutex>
                       lock(fits_mutex); auto item =
                       DATASETS.find(datasetid); lock.unlock();

                       if (item == DATASETS.end()) {
                       std::string error = "[error] " + datasetid
                       +
                       "::not found"; ws->send(error, opCode);
                       return;
                       }
                       else {
                       auto fits = item->second;
                       if (fits->has_error) {
                       std::string error = "[error] " + datasetid
                       +
                       "::cannot be read"; ws->send(error,
                       opCode); return;
                       }
                       else {
                       std::unique_lock<std::mutex>
                       data_lock(fits->data_mtx); while
                       (!fits->processed_data)
                       fits->data_cv.wait(data_lock);
                       data_lock.unlock();

                       if (!fits->has_data) {
                       std::string error = "[error] " + datasetid
                       +
                       "::image not found"; ws->send(error,
                       opCode); return;
                       }
                       else {
                       //make an image based on the pixels and
                       mask std::string msg = "[ok] " + datasetid
                       +
                       "::image OK"; ws->send(msg, opCode); return
                       ;
                       }
                       }
                       }
                       }
                       }*/
                   },
               .close =
                   [](auto *ws, int code, std::string_view message) {
                     struct UserData *user =
                         (struct UserData *)ws->getUserData();

                     if (user != NULL)
                     {
                       if (user->ptr != NULL)
                       {
                         user->ptr->active = false;

                         PrintThread{} << "[µWS] closing a session "
                                       << user->ptr->session_id << " for "
                                       << user->ptr->primary_id << std::endl;

                         auto primary_id = user->ptr->primary_id;

                         // launch a separate thread
                         std::thread([primary_id, ws]() {
                           std::lock_guard<std::shared_mutex> guard(
                               m_progress_mutex);
                           TWebSocketList connections = m_progress[primary_id];
                           connections.erase(ws);
                           m_progress[primary_id] = connections;

                           // check if it is the last connection
                           // for this dataset
                           if (connections.size() == 0)
                             m_progress.erase(primary_id);
                         }).detach();

                         // join on all active threads
                         user->ptr->active_threads.join_all();

                         delete user->ptr;
                       }
                       else
                         PrintThread{} << "[µWS] close " << message
                                       << std::endl;
                     }
                     else
                       PrintThread{} << "[µWS] close " << message << std::endl;
                   }})
          .listen(server_port,
                  [](auto *token) {
                    if (token)
                    {
                      PrintThread{} << "Thread " << std::this_thread::get_id()
                                    << " listening on port " << server_port
                                    << std::endl;
                    }
                    else
                    {
                      PrintThread{} << "Thread " << std::this_thread::get_id()
                                    << " failed to listen on port "
                                    << server_port << std::endl;
                    }
                  })
          .run();
    });
  });

  std::for_each(threads.begin(), threads.end(),
                [](std::thread *t) { t->join(); });

  printf("uWS HTTP/WebSockets server shutdown completed.\n");
}
