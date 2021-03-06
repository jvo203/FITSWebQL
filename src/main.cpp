#define VERSION_MAJOR 5
#define VERSION_MINOR 0
#define VERSION_SUB 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define BEACON_PORT 50000
#define HTTPS_PORT 8080

#define SERVER_STRING \
  "FITSWebQL v" STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_SUB)

#define WASM_VERSION "20.05.08.0"
#define VERSION_STRING "SV2020-05-27.0"

#define PROGRESS_TIMEOUT 250 /*[ms]*/

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

#include <deque>
#include <filesystem>
#include <iostream>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <dirent.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <sqlite3.h>

#ifndef LOCAL
//#include <pgsql/libpq-fe.h>
#include <libpq-fe.h>
#endif

#include "fits.hpp"
#include "global.h"
#include "json.h"

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

#ifdef CLUSTER
zactor_t *speaker = NULL;
zactor_t *listener = NULL;
std::thread beacon_thread;
std::atomic<bool> exiting(false);
#endif

sqlite3 *splat_db = NULL;

#include <zlib.h>

/* CHUNK is the size of the memory chunk used by the zlib routines. */

#define CHUNK 0x4000
#define windowBits 15
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

struct TransmitQueue
{
  boost::lockfree::spsc_queue<uint8_t> q{10 * CHUNK};
  std::deque<uint8_t> fifo;
  std::mutex mtx;
  std::atomic<bool> eof;

  TransmitQueue() { eof = false; }
};

struct MolecularStream
{
  bool first;
  bool compress;
  struct TransmitQueue *queue;
  z_stream z;
  unsigned char out[CHUNK];
  FILE *fp;
};

std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;
std::shared_mutex fits_mutex;

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

inline const char *check_null(const char *str)
{
  if (str != NULL)
    return str;
  else
    return ""; //"\"\"" ;
};

#include <nghttp2/asio_http2_server.h>

using namespace nghttp2::asio_http2;
using namespace nghttp2::asio_http2::server;

http2 *http2_server;
std::string docs_root = "htdocs2";
std::string home_dir;

void http_not_found(const response *res)
{
  res->write_head(404);
  res->end("Not Found");
}

void http_not_implemented(const response *res)
{
  res->write_head(501);
  res->end("Not Implemented");
}

void http_accepted(const response *res)
{
  res->write_head(202);
  res->end("Accepted");
}

void http_internal_server_error(const response *res)
{
  res->write_head(500);
  res->end("Internal Server Error");
}

#ifdef LOCAL
void serve_directory(const response *res, std::string dir)
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

  header_map mime;
  mime.insert(std::pair<std::string, header_value>(
      "Content-Type", {"application/json", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  res->write_head(200, mime);
  res->end(json.str());
}
#endif

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

    if (buffer != NULL)
      html.append((const char *)buffer, size);
    else
      perror("error mapping a file");

    if (munmap(buffer, size) == -1)
      perror("un-mapping error");

    close(fd);
  };
}

static int sqlite_callback(void *userp, int argc, char **argv,
                           char **azColName)
{
  MolecularStream *stream = (MolecularStream *)userp;

  if (argc == 8)
  {
    std::string json;

    if (stream->first)
    {
      stream->first = false;
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

          /*size_t pushed =
              stream->queue->q.push((const uint8_t *)stream->out, have);
          if (pushed != have)
            fprintf(stderr, "error appending to spsc_queue\n");*/

          std::lock_guard<std::mutex> guard(stream->queue->mtx);
          stream->queue->fifo.insert(stream->queue->fifo.end(), stream->out,
                                     stream->out + have);
        }
      } while (stream->z.avail_out == 0);
    }
    else
    {
      /*size_t pushed =
          stream->queue->q.push((const uint8_t *)json.c_str(), json.size());
      if (pushed != json.size())
        fprintf(stderr, "error appending to spsc_queue\n");*/

      std::lock_guard<std::mutex> guard(stream->queue->mtx);
      stream->queue->fifo.insert(stream->queue->fifo.end(), json.c_str(),
                                 json.c_str() + json.size());
    }
  }

  return 0;
}

generator_cb progress_generator(std::shared_ptr<FITS> fits)
{
  return [fits](uint8_t *buf, size_t len,
                uint32_t *data_flags) -> generator_cb::result_type {
    ssize_t n = 0;
    std::ostringstream data;
    bool eof = false;

    // check if running == total
    {
      std::shared_lock<std::shared_mutex> lock(fits->progress_mtx);

      if (fits->progress.total > 0 &&
          fits->progress.total == fits->progress.running)
        eof = true;
    }

    // if not, sleep for <PROGRESS_TIMEOUT> milliseconds
    if (!eof)
    {
      struct timespec ts;
      ts.tv_sec = PROGRESS_TIMEOUT / 1000;
      ts.tv_nsec = (PROGRESS_TIMEOUT % 1000) * 1000000;
      nanosleep(&ts, NULL);
    }

    // lock a read-only progress mutex
    {
      std::shared_lock<std::shared_mutex> lock(fits->progress_mtx);

      // make a json response
      if (fits->progress.total > 0)
      {
        data << "data:";
        data << "{ \"total\" : " << fits->progress.total << ", ";
        data << "\"running\" : " << fits->progress.running << ", ";
        data << "\"elapsed\" : " << fits->progress.elapsed << " }";
        data << "\n\n";
      }
    }

    // printf("sending progress notification: %s\n", data.str().c_str());

    // send it
    size_t size = data.str().size();

    if (size > 0 && size < len)
    {
      memcpy(buf, data.str().c_str(), size);
      n = size;
    }
    else if (size > len)
      eof = true;

    if (eof)
    {
      printf("closing the event stream.\n");
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }

    return n;
  };
}

generator_cb stream_generator(struct TransmitQueue *queue)
{
  return [queue](uint8_t *buf, size_t len,
                 uint32_t *data_flags) -> generator_cb::result_type {
    ssize_t n = 0;

    /*if (queue->q.read_available() > 0) {
      printf("queue length: %zu buffer length: %zu bytes.\n",
             queue->q.read_available(), len);

      n = queue->q.pop(buf, len);
    }*/

    std::unique_lock<std::mutex> lock(queue->mtx);

    if (queue->fifo.size() > 0)
    {
      size_t size = std::min(len, queue->fifo.size());

      // printf("queue length: %zu buffer length: %zu bytes.\n",
      //       queue->fifo.size(), len);

      // pop elements up to <len>
      std::copy(queue->fifo.begin(), queue->fifo.begin() + size, buf);
      queue->fifo.erase(queue->fifo.begin(), queue->fifo.begin() + size);
      n = size;
    }

    // if (queue->eof && queue->q.read_available() == 0) {
    if (queue->eof && queue->fifo.empty())
    {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;

      lock.release();
      free(queue);
    }

    return n;
  };
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

void stream_realtime_image_spectrum(const response *res,
                                    std::shared_ptr<FITS> fits, int dx,
                                    float quality, bool image_update, int x1,
                                    int x2, int y1, int y2, double frame_start,
                                    double frame_end, double ref_freq,
                                    beam_shape beam, intensity_mode intensity,
                                    int seq, float timestamp, std::string session_id)
{
  header_map mime;
  mime.insert(std::pair<std::string, header_value>(
      "Content-Type", {"application/octet-stream", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  res->write_head(200, mime);

  struct TransmitQueue *queue = new TransmitQueue();

  res->end(stream_generator(queue));

  // launch a separate spectrum/viewport thread
  std::thread([queue, fits, dx, quality, image_update, x1, x2, y1, y2,
               frame_start, frame_end, ref_freq, beam, intensity, seq,
               timestamp, session_id]() {
    float compression_level = quality; // 100.0f; // default is 45.0f

    auto session = get_session(session_id);

    if (session == nullptr)
    {
      std::shared_ptr<struct UserSession> _session(new UserSession());
      _session->last_seq = -1;

      insert_session(session_id, _session);
      session = _session;
    }

    struct UserSession *session_ptr = session.get();

    // gain unique access
    std::lock_guard<std::shared_mutex> unique_session(session->mtx);

    // check the sequence numbers (skip older requests)
    int last_seq = session_ptr->last_seq;
    if (seq < last_seq)
    {
      printf("skipping an old frame (%d < %d)\n", seq, last_seq);

      // end the response early by sending nothing
      std::lock_guard<std::mutex> guard(queue->mtx);

      // end of chunked encoding
      queue->eof = true;
      return;
    }

    session_ptr->last_seq = seq;

    session->ts = system_clock::now();

    int start, end;
    double elapsedMilliseconds;

    fits->get_spectrum_range(frame_start, frame_end, ref_freq, start, end);

    if (image_update)
    {
      std::lock_guard<std::mutex> guard(queue->mtx);
      // append the compressed viewport
    }

    // calculate a viewport spectrum
    if (fits->depth > 1)
    {
      std::vector<float> spectrum = fits->get_spectrum(
          start, end, x1, y1, x2, y2, intensity, beam, elapsedMilliseconds);

      std::cout << "spectrum length = " << spectrum.size()
                << " elapsed time: " << elapsedMilliseconds << " [ms]"
                << std::endl;

      // append the spectrum to the HTTP/2 response queue
      if (spectrum.size() > 0)
      {
        float ts = timestamp;
        uint32_t id = seq;
        uint32_t msg_type = 0;
        float elapsed = elapsedMilliseconds;

        std::lock_guard<std::mutex> guard(queue->mtx);
        const char *ptr;

        ptr = (const char *)&ts;
        queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(float));

        ptr = (const char *)&id;
        queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

        ptr = (const char *)&msg_type;
        queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

        ptr = (const char *)&elapsed;
        queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(float));

        ptr = (const char *)spectrum.data();
        queue->fifo.insert(queue->fifo.end(), ptr,
                           ptr + spectrum.size() * sizeof(float));
      }
    }

    std::lock_guard<std::mutex> guard(queue->mtx);
    printf("[stream_realtime_image_spectrum] number of remaining bytes: %zu\n",
           queue->fifo.size());

    // end of chunked encoding
    queue->eof = true;
  }).detach();
}

void stream_image_spectrum(const response *res, std::shared_ptr<FITS> fits,
                           int _width, int _height, float quality,
                           bool fetch_data)
{
  header_map mime;
  mime.insert(std::pair<std::string, header_value>(
      "Content-Type", {"application/octet-stream", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  res->write_head(200, mime);

  struct TransmitQueue *queue = new TransmitQueue();

  res->end(stream_generator(queue));

  // launch a separate image thread
  std::thread([queue, fits, _width, _height, quality, fetch_data]() {
    float compression_level = quality; // 100.0f; // default is 45.0f

    // calculate a new image size
    long true_width = fits->width;
    long true_height = fits->height;
    true_image_dimensions(fits->img_mask, true_width, true_height);
    float scale = get_image_scale(_width, _height, true_width, true_height);

    if (scale < 1.0)
    {
      int img_width = roundf(scale * fits->width);
      int img_height = roundf(scale * fits->height);

      printf("FITS image scaling by %f; %ld x %ld --> %d x %d\n", scale,
             fits->width, fits->height, img_width, img_height);

      size_t plane_size = size_t(img_width) * size_t(img_height);

      // allocate {pixel_buf, mask_buf}
      std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
                                         ippsFree);
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
            tileResize32f_C1R(fits->img_pixels, srcSize, srcStep,
                              pixels_buf.get(), dstSize, dstStep);

        IppStatus mask_stat = tileResize8u_C1R(
            fits->img_mask, srcSize, srcStep, mask_buf.get(), dstSize, dstStep);

        printf(" %d : %s, %d : %s\n", pixels_stat,
               ippGetStatusString(mask_stat), pixels_stat,
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

          // in-memory output
          StdOSStream oss;

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

            frameBuffer.insert("A",
                               Slice(FLOAT, (char *)mask, sizeof(Ipp32f) * 1,
                                     sizeof(Ipp32f) * img_width));

            file.setFrameBuffer(frameBuffer);
            file.writePixels(img_height);
          }
          catch (const std::exception &exc)
          {
            std::cerr << exc.what() << std::endl;
          }

          std::string output = oss.str();
          std::cout << "[" << fits->dataset_id
                    << "]::downsize OpenEXR output: " << output.length()
                    << " bytes." << std::endl;

          // send the data to the web client
          {
            /*uint32_t id_length = 3;
            const char id[] = {'E', 'X', 'R'};
            uint32_t js_width = img_width;
            uint32_t js_height = img_height;
            uint64_t length = output.length();*/

            std::lock_guard<std::mutex> guard(queue->mtx);
            const char *ptr;

            /*ptr = (const char *)&id_length;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

            ptr = id;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + id_length);

            ptr = (const char *)&js_width;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

            ptr = (const char *)&js_height;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

            ptr = (const char *)&length;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr +
            sizeof(uint64_t));*/

            // send image tone mapping statistics
            float tmp = 0.0f;
            uint32_t str_len = fits->flux.length();
            uint64_t img_len = output.length();

            ptr = (const char *)&str_len;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));
            ptr = fits->flux.c_str();
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + str_len);

            ptr = (const char *)&tmp;

            tmp = fits->min;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            tmp = fits->max;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            tmp = fits->median;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            tmp = fits->sensitivity;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            tmp = fits->ratio_sensitivity;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            tmp = fits->white;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            tmp = fits->black;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

            ptr = (const char *)&img_len;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint64_t));

            ptr = output.c_str();
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + output.length());
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
              compressed_size = LZ4_compress_HC(
                  (const char *)json.str().c_str(), (char *)json_lz4, json_size,
                  worst_size, LZ4HC_CLEVEL_MAX);

              printf("FITS::JSON size %d, LZ4-compressed: %d bytes.\n",
                     json_size, compressed_size);

              // append json to the trasmission queue
              std::lock_guard<std::mutex> guard(queue->mtx);

              const char *ptr = (const char *)&json_size;
              queue->fifo.insert(queue->fifo.end(), ptr,
                                 ptr + sizeof(uint32_t));
              queue->fifo.insert(queue->fifo.end(), json_lz4,
                                 json_lz4 + compressed_size);

              ippsFree(json_lz4);
            }
          }
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
        Ipp32f *pixels = fits->img_pixels; // pixels_buf.get();
        Ipp8u *_mask = fits->img_mask;
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

        // in-memory output
        StdOSStream oss;

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

        std::string output = oss.str();
        std::cout << "[" << fits->dataset_id
                  << "]::mirror OpenEXR output: " << output.length()
                  << " bytes." << std::endl;

        // send the data to the web client
        {
          /*uint32_t id_length = 3;
          const char id[] = {'E', 'X', 'R'};
          uint32_t js_width = img_width;
          uint32_t js_height = img_height;
          uint64_t length = output.length();*/

          std::lock_guard<std::mutex> guard(queue->mtx);
          const char *ptr;

          /*ptr = (const char *)&id_length;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

          ptr = id;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + id_length);

          ptr = (const char *)&js_width;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

          ptr = (const char *)&js_height;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));

          ptr = (const char *)&length;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint64_t));*/

          // send image tone mapping statistics
          float tmp = 0.0f;
          uint32_t str_len = fits->flux.length();
          uint64_t img_len = output.length();

          ptr = (const char *)&str_len;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));
          ptr = fits->flux.c_str();
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + str_len);

          ptr = (const char *)&tmp;

          tmp = fits->min;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          tmp = fits->max;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          tmp = fits->median;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          tmp = fits->sensitivity;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          tmp = fits->ratio_sensitivity;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          tmp = fits->white;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          tmp = fits->black;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(tmp));

          ptr = (const char *)&img_len;
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint64_t));

          ptr = output.c_str();
          queue->fifo.insert(queue->fifo.end(), ptr, ptr + output.length());
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
            compressed_size = LZ4_compress_HC((const char *)json.str().c_str(),
                                              (char *)json_lz4, json_size,
                                              worst_size, LZ4HC_CLEVEL_MAX);

            printf("FITS::JSON size %d, LZ4-compressed: %d bytes.\n", json_size,
                   compressed_size);

            // append json to the trasmission queue
            std::lock_guard<std::mutex> guard(queue->mtx);

            const char *ptr = (const char *)&json_size;
            queue->fifo.insert(queue->fifo.end(), ptr, ptr + sizeof(uint32_t));
            queue->fifo.insert(queue->fifo.end(), json_lz4,
                               json_lz4 + compressed_size);

            ippsFree(json_lz4);
          }
        }
      }
    }

    std::lock_guard<std::mutex> guard(queue->mtx);
    printf("[stream_image_spectrum] number of remaining bytes: %zu\n",
           queue->fifo.size());

    // end of chunked encoding
    queue->eof = true;
  }).detach();
}

void stream_molecules(const response *res, double freq_start, double freq_end,
                      bool compress)
{
  if (splat_db == NULL)
    return http_internal_server_error(res);

  header_map mime;
  mime.insert(std::pair<std::string, header_value>(
      "Content-Type", {"application/json", false}));
  mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                   {"no-cache", false}));

  // append the compression mime
  if (compress)
    mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                     {"gzip", false}));
  res->write_head(200, mime);

  struct TransmitQueue *queue = new TransmitQueue();

  res->end(stream_generator(queue));

  // launch a separate molecules thread
  std::thread([queue, compress, freq_start, freq_end]() {
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
    stream.queue = queue;
    stream.fp = NULL;

    if (compress)
    {
      stream.z.zalloc = Z_NULL;
      stream.z.zfree = Z_NULL;
      stream.z.opaque = Z_NULL;
      stream.z.next_in = Z_NULL;
      stream.z.avail_in = 0;

      CALL_ZLIB(deflateInit2(&stream.z, Z_BEST_COMPRESSION, Z_DEFLATED,
                             windowBits | GZIP_ENCODING, 9,
                             Z_DEFAULT_STRATEGY));
    }

    rc = sqlite3_exec(splat_db, strSQL, sqlite_callback, &stream, &zErrMsg);

    if (rc != SQLITE_OK)
    {
      fprintf(stderr, "SQL error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
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

          /*size_t pushed =
              stream.queue->q.push((const uint8_t *)stream.out, have);
          if (pushed != have)
            fprintf(stderr, "error appending to spsc_queue\n");*/

          std::lock_guard<std::mutex> guard(stream.queue->mtx);
          stream.queue->fifo.insert(stream.queue->fifo.end(), stream.out,
                                    stream.out + have);
        }
      } while (stream.z.avail_out == 0);

      CALL_ZLIB(deflateEnd(&stream.z));

      if (stream.fp != NULL)
        fclose(stream.fp);
    }
    else
    {
      /*size_t pushed = stream.queue->q.push((const uint8_t
      *)chunk_data.c_str(), chunk_data.size()); if (pushed != chunk_data.size())
        fprintf(stderr, "error appending to spsc_queue\n");*/

      std::lock_guard<std::mutex> guard(stream.queue->mtx);
      stream.queue->fifo.insert(stream.queue->fifo.end(), chunk_data.c_str(),
                                chunk_data.c_str() + chunk_data.size());
    }

    /*printf("[stream_molecules] number of remaining bytes: %zu\n",
           stream.queue->q.read_available());*/

    std::lock_guard<std::mutex> guard(stream.queue->mtx);
    printf("[stream_molecules] number of remaining bytes: %zu\n",
           stream.queue->fifo.size());

    // end of chunked encoding
    stream.queue->eof = true;
  }).detach();
}

void get_spectrum(const response *res, std::shared_ptr<FITS> fits)
{
  std::ostringstream json;

  fits->to_json(json);

  if (json.tellp() > 0)
  {
    header_map mime;
    mime.insert(std::pair<std::string, header_value>(
        "Content-Type", {"application/json", false}));
    mime.insert(std::pair<std::string, header_value>("Cache-Control",
                                                     {"no-cache", false}));

    res->write_head(200, mime);
    res->end(json.str());
  }
  else
  {
    return http_not_implemented(res);
  }
}

void serve_file(const request *req, const response *res, std::string uri)
{
  // a safety check against directory traversal attacks
  if (!check_path(uri))
    return http_not_found(res);

  // check if a resource exists
  std::string path = docs_root + uri;

  if (std::filesystem::exists(path))
  {
    // detect mime-types
    header_map mime;

    size_t pos = uri.find_last_of(".");

    if (pos != std::string::npos)
    {
      std::string ext = uri.substr(pos + 1, std::string::npos);

      if (ext == "htm" || ext == "html")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"text/html", false}));

      if (ext == "txt")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"text/plain", false}));

      if (ext == "js")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"application/javascript", false}));

      if (ext == "vert")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"text/plain", false}));

      if (ext == "frag")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"text/plain", false}));

      if (ext == "ico")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/x-icon", false}));

      if (ext == "png")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"image/png", false}));

      if (ext == "gif")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"image/gif", false}));

      if (ext == "webp")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/webp", false}));

      if (ext == "jpg" || ext == "jpeg")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/jpeg", false}));

      if (ext == "mp4")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"video/mp4", false}));

      if (ext == "css")
        mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                         {"text/css", false}));

      if (ext == "pdf")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"application/pdf", false}));

      if (ext == "svg")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"image/svg+xml", false}));

      if (ext == "wasm")
        mime.insert(std::pair<std::string, header_value>(
            "Content-Type", {"application/wasm", false}));
    }

    // check for compression
    header_map headers = req->header();

    auto it = headers.find("accept-encoding");
    if (it != headers.end())
    {
      auto value = it->second.value;
      // std::cout << "Supported compression: " << value << std::endl;

      // prefer brotli due to smaller file sizes
      size_t pos = value.find("br"); // brotli or gzip

      if (pos != std::string::npos)
      {
        if (std::filesystem::exists(path + ".br"))
        {
          path += ".br";
          // append the compression mime
          mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                           {"br", false}));
        }
      }
      else
      {
        // fallback to gzip
        size_t pos = value.find("gzip");

        if (pos != std::string::npos)
        {
          if (std::filesystem::exists(path + ".gz"))
          {
            path += ".gz";
            // append the compression mime
            mime.insert(std::pair<std::string, header_value>("Content-Encoding",
                                                             {"gzip", false}));
          }
        }
      }
    }

    res->write_head(200, mime);
    res->end(file_generator(path));
  }
  else
  {
    http_not_found(res);
  }
}

void http_fits_response(const response *res, std::vector<std::string> datasets,
                        bool composite, bool has_fits)
{
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
              "src=\"//cdnjs.cloudflare.com/ajax/libs/numeral.js/2.0.6/"
              "numeral.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/ra_dec_conversion.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/sylvester.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/shortcut.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/colourmaps.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/lz4.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/marchingsquares-isocontours.min.js\"></script>\n");
  html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/fits_web_ql/htdocs/"
              "fitswebql/marchingsquares-isobands.min.js\"></script>\n");

  /*html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/FITSWebQL/" +
              docs_root + "/"
                          "fitswebql/webgl-utils.js\"></script>\n");*/

  // OpenEXR WASM decoder
  html.append("<script "
              "src=\"exr." WASM_VERSION ".js\"></script>\n");
  /*html.append("<script "
              "src=\"https://cdn.jsdelivr.net/gh/jvo203/FITSWebQL/" +
              docs_root + "/"
                          "fitswebql/exr." WASM_VERSION ".js\"></script>\n");*/
  html.append(R"(
    <script>
    Module.ready
      .then( status => console.log( status ))
      .catch(e => console.error(e))    
  </script>
  )");

  // bootstrap
  html.append("<meta name=\"viewport\" content=\"width=device-width, "
              "initial-scale=1, "
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
  html.append("<script src=\"fitswebql.js?" VERSION_STRING "\"></script>\n");
  html.append("<link rel=\"stylesheet\" href=\"fitswebql.css?" VERSION_STRING
              "\"/>\n");

  // HTML content
  html.append("<title>FITSWebQL</title></head><body>\n");
  html.append("<div id='votable' style='width: 0; height: 0;' data-va_count='" +
              std::to_string(datasets.size()) + "' ");

  if (datasets.size() == 1)
    html.append("data-datasetId='" + datasets[0] + "' ");
  else
  {
    for (unsigned int i = 0; i < datasets.size(); i++)
      html.append("data-datasetId" + std::to_string(i + 1) + "='" +
                  datasets[i] + "' ");

    if (composite && datasets.size() <= 3)
      html.append("data-composite='1' ");
  }

  boost::uuids::uuid session_id = boost::uuids::random_generator()();

  html.append("data-root-path='/" + std::string("fitswebql") +
              "/' data-server-version='" + VERSION_STRING +
              "' data-server-string='" + SERVER_STRING + "' data-session-id='" +
              boost::uuids::to_string(session_id) +
#ifdef LOCAL
              "' data-server-mode='" + "LOCAL" +
#else
              "' data-server-mode='" + "SERVER" +
#endif
              "' data-has-fits='" + std::to_string(has_fits) + "'></div>\n");

  // the page entry point
  html.append(R"(<script>
        const golden_ratio = 1.6180339887;        
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
        };
        mainRenderer();
    </script>)");

  html.append("</body></html>");

  header_map mime;
  mime.insert(std::pair<std::string, header_value>("Content-Type",
                                                   {"text/html", false}));
  res->write_head(200, mime);
  res->end(html);
}

void execute_fits(const response *res, std::string dir, std::string ext,
                  std::string db, std::string table,
                  std::vector<std::string> datasets, bool composite,
                  std::string flux)
{
  bool has_fits = true;

#ifndef LOCAL
  PGconn *jvo_db = NULL;

  if (db != "")
    jvo_db = jvo_db_connect(db);
#endif

  int va_count = datasets.size();

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
        std::thread(&FITS::from_path_mmap, fits, path, is_compressed, flux,
                    va_count)
            .detach();
      }
      else
      {
        // the last resort
        std::string url = std::string("http://") + JVO_FITS_SERVER +
                          ":8060/skynode/getDataForALMA.do?db=" + JVO_FITS_DB +
                          "&table=cube&data_id=" + data_id + "_00_00_00";

        // download FITS data from a URL in a separate thread
        std::thread(&FITS::from_url, fits, url, flux, va_count).detach();
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

  return http_fits_response(res, datasets, composite, has_fits);
}

void signalHandler(int signum)
{
  printf("Interrupt signal (%d) received. Please wait...\n", signum);

#ifdef CLUSTER
  exiting = true;
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

  http2_server->stop();

  /*signal(SIGINT, prevSIGINT);
  signal(SIGTERM, prevSIGTERM);
  raise(signum);*/
}

int main(int argc, char *argv[])
{
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

  boost::system::error_code ec;
  boost::asio::ssl::context tls(boost::asio::ssl::context::sslv23);

  tls.use_private_key_file("ssl/server.key", boost::asio::ssl::context::pem);
  tls.use_certificate_chain_file("ssl/server.crt");

  if (configure_tls_context_easy(ec, tls))
  {
    std::cerr << "error: " << ec.message() << std::endl;
  }

  int no_threads = MAX(std::thread::hardware_concurrency() / 2, 1);

  http2_server = new http2();
  http2_server->num_threads(no_threads);

#ifdef LOCAL
  http2_server->handle(
      "/get_directory", [](const request &req, const response &res) {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string dir;
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
              dir = value;
            }
          }
        }

        if (dir != "")
          serve_directory(&res, dir);
        else
          serve_directory(&res, home_dir);
      });
#endif

  http2_server->handle("/", [](const request &req, const response &res) {
    boost::system::error_code ec;

    auto uri = req.uri().path;

    if (uri == "/")
    {
      auto push = res.push(ec, "GET", "/favicon.ico");
      serve_file(&req, push, "/favicon.ico");

#ifdef LOCAL
      push = res.push(ec, "GET", "/local.css");
      serve_file(&req, push, "/local.css");

      push = res.push(ec, "GET", "/local.js");
      serve_file(&req, push, "/local.js");

      push = res.push(ec, "GET", "/logo_naoj_all_s.png");
      serve_file(&req, push, "/logo_naoj_all_s.png");

      serve_file(&req, &res, "/local.html");
#else
      push = res.push(ec, "GET", "/test.css");
      serve_file(&req, push, "/test.css");

      push = res.push(ec, "GET", "/test.js");
      serve_file(&req, push, "/test.js");

      serve_file(&req, &res, "/test.html");
#endif
    }
    else
    {
      std::cout << uri << std::endl;

      if (uri.find("get_progress/") != std::string::npos)
      {
        size_t pos = uri.find_last_of("/");

        if (pos != std::string::npos)
        {
          std::string datasetid = uri.substr(pos + 1, std::string::npos);

          // process the response
          std::cout << "progress(" << datasetid << ")" << std::endl;

          auto fits = get_dataset(datasetid);

          if (fits == nullptr)
            return http_not_found(&res);
          else
          {
            if (fits->has_error)
              return http_not_found(&res);
            else
            {
              // make json
              std::ostringstream json;
              bool valid = false;
              {
                std::shared_lock<std::shared_mutex> lock(fits->progress_mtx);

                json << "{\"total\" : " << fits->progress.total << ",";
                json << "\"running\" : " << fits->progress.running << ",";
                json << "\"elapsed\" : " << fits->progress.elapsed << "}";

                if (fits->progress.total > 0)
                  valid = true;
              }

              if (valid)
              {
                header_map mime;
                mime.insert(std::pair<std::string, header_value>(
                    "Content-Type", {"application/json", false}));
                res.write_head(200, mime);
                res.end(json.str());
              }
              else
                http_accepted(&res);

              return;
            }
          }
        }
        else
          return http_not_found(&res);
      }

      if (uri.find("progress/") != std::string::npos)
      {
        size_t pos = uri.find_last_of("/");

        if (pos != std::string::npos)
        {
          std::string datasetid = uri.substr(pos + 1, std::string::npos);

          // process the response
          std::cout << "progress(" << datasetid << ")" << std::endl;

          auto fits = get_dataset(datasetid);

          if (fits == nullptr)
            return http_not_found(&res);
          else
          {
            if (fits->has_error)
              return http_not_found(&res);
            else
            {
              header_map mime;

              mime.insert(std::pair<std::string, header_value>(
                  "Content-Type", {"text/event-stream", false}));
              mime.insert(std::pair<std::string, header_value>(
                  "Cache-Control", {"no-cache", false}));
              res.write_head(200, mime);
              res.end(progress_generator(fits));

              return;
            }
          }
        }
        else
          return http_not_found(&res);
      }

      if (uri.find("/get_molecules") != std::string::npos)
      {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        bool compress = false;

        // check for compression
        header_map headers = req.header();

        auto it = headers.find("accept-encoding");
        if (it != headers.end())
        {
          auto value = it->second.value;
          // std::cout << "Supported compression: " << value << std::endl;

          size_t pos = value.find("gzip");

          if (pos != std::string::npos)
          {
            compress = true;
          }
        }

        std::string datasetid;
        double freq_start = 0.0;
        double freq_end = 0.0;

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

            if (key.find("dataset") != std::string::npos)
            {
              datasetid = value;
            }

            if (key.find("freq_start") != std::string::npos)
              freq_start = std::stod(value) / 1.0E9; //[Hz -> GHz]

            if (key.find("freq_end") != std::string::npos)
              freq_end = std::stod(value) / 1.0E9; //[Hz -> GHz]
          }
        }

        if (FPzero(freq_start) || FPzero(freq_end))
        {
          // get the frequency range from the FITS header
          auto fits = get_dataset(datasetid);

          if (fits == nullptr)
            return http_not_found(&res);
          else
          {
            if (fits->has_error)
              return http_not_found(&res);

            /*std::unique_lock<std::mutex> header_lck(fits->header_mtx);
            while (!fits->processed_header)
              fits->header_cv.wait(header_lck);*/

            if (!fits->has_header)
              return http_accepted(&res);
            // return http_not_found(&res);

            if (fits->depth <= 1 || !fits->has_frequency)
              return http_not_implemented(&res);

            // extract the freq. range
            fits->get_frequency_range(freq_start, freq_end);
          }
        }

        // process the response
        std::cout << "get_molecules(" << datasetid << "," << freq_start
                  << "GHz," << freq_end << "GHz)" << std::endl;

        if (!FPzero(freq_start) && !FPzero(freq_end))
          return stream_molecules(&res, freq_start, freq_end, compress);
        else
          return http_not_implemented(&res);
      }

      if (uri.find("/heartbeat") != std::string::npos)
      {
        res.write_head(200);

        size_t pos = uri.find_last_of("/");

        if (pos != std::string::npos)
        {
          std::string timestamp = uri.substr(pos + 1, std::string::npos);
          res.end(timestamp);
        }
        else
          res.end("N/A");

        return;
      }

      if (uri.find("/image_spectrum") != std::string::npos)
      {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string datasetid;
        int width = 0;
        int height = 0;
        float quality = 45;
        bool fetch_data = false;

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

            if (key.find("dataset") != std::string::npos)
            {
              datasetid = value;
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
              quality = std::stof(value);
            }

            if (key.find("fetch_data") != std::string::npos)
            {
              if (value == "true")
                fetch_data = true;
            }
          }
        }

        // process the response
        std::cout << "get_image(" << datasetid << "::" << width
                  << "::" << height << "::" << quality
                  << "::" << (fetch_data ? "true" : "false") << ")"
                  << std::endl;

        auto fits = get_dataset(datasetid);

        if (fits == nullptr)
          return http_not_found(&res);
        else
        {
          if (fits->has_error)
            return http_not_found(&res);
          else
          {
            /*std::unique_lock<std::mutex> data_lock(fits->data_mtx);
            while (!fits->processed_data)
              fits->data_cv.wait(data_lock);*/

            if (!fits->has_data)
              // return http_not_found(&res);
              return http_accepted(&res);
            else
              // return http_not_implemented(&res);
              return stream_image_spectrum(&res, fits, width, height, quality,
                                           fetch_data);
          }
        }
      }

      if (uri.find("/get_spectrum") != std::string::npos)
      {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string datasetid;

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

            if (key.find("dataset") != std::string::npos)
            {
              datasetid = value;
            }
          }
        }

        // process the response
        std::cout << "get_spectrum(" << datasetid << ")" << std::endl;

        auto fits = get_dataset(datasetid);

        if (fits == nullptr)
          return http_not_found(&res);
        else
        {
          if (fits->has_error)
            return http_not_found(&res);
          else
          {
            /*std::unique_lock<std::mutex> data_lock(fits->data_mtx);
            while (!fits->processed_data)
              fits->data_cv.wait(data_lock);*/

            if (!fits->has_data)
              // return http_not_found(&res);
              return http_accepted(&res);
            else
              return get_spectrum(&res, fits);
          }
        }
      }

      // real-time spectrum/viewport updates
      if (uri.find("/realtime_image_spectrum") != std::string::npos)
      {
        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::string datasetid;
        int seq = -1;
        int dx = 0;
        float quality = 45;
        bool image_update = false;
        int x1 = -1;
        int x2 = -1;
        int y1 = -1;
        int y2 = -1;
        double frame_start = 0;
        double frame_end = 0;
        double ref_freq = 0;
        float timestamp = 0;
        intensity_mode intensity = mean;
        beam_shape beam = square;
        std::string session_id;

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

            if (key.find("dataset") != std::string::npos)
              datasetid = value;

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
              beam =
                  (strcasecmp("circle", value.c_str()) == 0) ? circle : square;

            if (key.find("intensity") != std::string::npos)
              intensity = (strcasecmp("integrated", value.c_str()) == 0)
                              ? integrated
                              : mean;

            if (key.find("session_id") != std::string::npos)
              session_id = value;
          }
        }

        // process the response
        std::cout << "query(" << datasetid << "::" << dx << "::" << quality
                  << "::" << (image_update ? "true" : "false") << "::<X:> "
                  << x1 << ".." << x2 << ",Y:> " << y1 << ".." << y2
                  << ">::" << frame_start << "::" << frame_end
                  << "::" << ref_freq
                  << "::" << (beam == circle ? "circle" : "square")
                  << "::" << (intensity == integrated ? "integrated" : "mean")
                  << "::" << seq << "::" << timestamp << "::" << session_id << ")" << std::endl;

        auto fits = get_dataset(datasetid);

        if (fits == nullptr)
          return http_not_found(&res);
        else
        {
          if (fits->has_error)
            return http_not_found(&res);
          else
          {
            if (!fits->has_data)
              return http_not_found(&res);
            else
            {
              fits->update_timestamp();
              return stream_realtime_image_spectrum(
                  &res, fits, dx, quality, image_update, x1, x2, y1, y2,
                  frame_start, frame_end, ref_freq, beam, intensity, seq,
                  timestamp, session_id);
            }
          }
        }
      }

      // FITSWebQL entry
      if (uri.find("FITSWebQL.html") != std::string::npos)
      {
        auto push = res.push(ec, "GET", "/favicon.ico");
        serve_file(&req, push, "/favicon.ico");

        push = res.push(ec, "GET", "/fitswebql/fitswebql.css?" VERSION_STRING);
        serve_file(&req, push, "/fitswebql/fitswebql.css");

        push = res.push(ec, "GET", "/fitswebql/fitswebql.js?" VERSION_STRING);
        serve_file(&req, push, "/fitswebql/fitswebql.js");

        /*push = res.push(ec, "GET", "/fitswebql/vertex-shader.vert?"
        VERSION_STRING); serve_file(&req, push,
        "/fitswebql/vertex-shader.vert");

        push = res.push(ec, "GET", "/fitswebql/fragment-shader.frag?"
        VERSION_STRING); serve_file(&req, push,
        "/fitswebql/fragment-shader.frag");*/

        auto uri = req.uri();
        auto query = percent_decode(uri.raw_query);

        std::vector<std::string> datasets;
        std::string dir, ext, db, table, flux;
        bool composite = false;

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

            if (key.find("dataset") != std::string::npos)
            {
              datasets.push_back(value);
            }

            if (key.find("filename") != std::string::npos)
            {
              datasets.push_back(value);
            }

            if (key == "dir")
            {
              dir = value;
            }

            if (key == "ext")
            {
              ext = value;
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

        // sane defaults
        {
          if (db.find("hsc") != std::string::npos)
          {
            flux = "ratio";
          }

          if (table.find("fugin") != std::string::npos)
            flux = "logistic";
        }

        std::cout << "dir:" << dir << ", ext:" << ext << ", db:" << db
                  << ", table:" << table << ", composite:" << composite
                  << ", flux:" << flux << ", ";
        for (auto const &dataset : datasets)
          std::cout << dataset << " ";
        std::cout << std::endl;

        if (datasets.size() == 0)
        {
          return http_not_found(&res);
        }
        else
          return execute_fits(&res, dir, ext, db, table, datasets, composite,
                              flux);
      }

      serve_file(&req, &res, uri);
    }
  });

  signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  if (http2_server->listen_and_serve(ec, tls, "0.0.0.0",
                                     std::to_string(HTTPS_PORT), true))
  {
    std::cerr << "error: " << ec.message() << std::endl;
  }

  http2_server->join();
  delete http2_server;

  // cleanup and close up stuff here
  // terminate program

  if (splat_db != NULL)
    sqlite3_close(splat_db);

  std::cout << "Terminating FITSWebQL..." << std::endl;
}
