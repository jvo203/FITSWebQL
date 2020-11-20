#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

// HEVC video decoder
#include <libavcodec/hevc_parse.h>

#include <libavutil/common.h>

#include <string.h>

//colourmaps
#include "colourmap.h"

static AVCodec *codec;
static AVCodecContext **avctx = NULL;
static AVFrame **avframe = NULL;
static AVPacket **avpkt = NULL;

extern AVCodec ff_hevc_decoder;
//extern AVCodecParser ff_hevc_parser;

static void hevc_init(int va_count);
static void hevc_destroy(int va_count);
static double hevc_decode_nal_unit(int index, const unsigned char *data, size_t data_len, unsigned char *canvas, unsigned int _w, unsigned int _h, const unsigned char *alpha, unsigned char *bytes, const char *colourmap);

// OpenEXR image decoder
#include <OpenEXR/IlmThread.h>
#include <OpenEXR/ImfNamespace.h>
#include <OpenEXR/ImfThreading.h>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfInputFile.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <fpzip/include/fpzip.h>

using namespace emscripten;

class BufferAdapter : public OPENEXR_IMF_NAMESPACE::IStream
{
public:
  BufferAdapter(char *buffer, size_t size)
      : IStream("BufferAdapter"), m_buffer(buffer), m_size(size), m_offset() {}

  virtual bool isMemoryMapped() const override { return true; }

  virtual bool read(char c[/*n*/], int n) override
  {
    std::size_t remaining = m_size - m_offset;
    if (std::size_t(n) > remaining)
      throw std::runtime_error("Out of bounds read request");
    auto current = readMemoryMapped(n);
    std::copy(current, current + n, c);
    return n == remaining;
  }

  virtual char *readMemoryMapped(int n) override
  {
    std::size_t remaining = m_size - m_offset;
    if (std::size_t(n) > remaining)
      throw std::runtime_error("Out of bounds read request");
    auto current = bufferCurrent();
    m_offset += n;
    return current;
  }

  virtual OPENEXR_IMF_NAMESPACE::Int64 tellg() override { return m_offset; }

  virtual void seekg(OPENEXR_IMF_NAMESPACE::Int64 pos) override
  {
    m_offset = pos;
  }

public:
  char *bufferCurrent() const { return m_buffer + m_offset; }

private:
  char *m_buffer;
  std::size_t m_size;
  std::size_t m_offset;
};

typedef std::vector<char> Bytes;
typedef std::vector<float> Pixel;
typedef std::map<std::string, Pixel> Planes;

struct EXRImage
{
  std::size_t width;
  std::size_t height;
  Planes planes;

  emscripten::val plane(std::string const &name) const
  {
    using namespace emscripten;
    auto it = planes.find(name);
    if (it == planes.end())
      return val::undefined();
    return val(typed_memory_view(it->second.size(), it->second.data()));
  }

  emscripten::val channels() const
  {
    using namespace emscripten;
    auto c = val::array();
    std::size_t idx = 0;
    for (auto const &plane : planes)
      c.set(idx++, plane.first);
    return val(c);
  }
};

EXRImage loadEXRRaw(char const *buffer, std::size_t size)
{
  using namespace OPENEXR_IMF_NAMESPACE;

  EXRImage image;
  try
  {
    BufferAdapter bytes(const_cast<char *>(buffer), size);
    InputFile file(bytes);
    auto window = file.header().dataWindow();
    image.width = window.max.x - window.min.x + 1;
    image.height = window.max.y - window.min.y + 1;
    size_t strideX = sizeof(float);
    size_t strideY = sizeof(float) * image.width;
    FrameBuffer fb;
    auto const &channels = file.header().channels();
    auto it = channels.begin();
    auto ed = channels.end();
    for (; it != ed; ++it) // OpenEXR iterators are not real iterators so ranged
                           // for loop doesn't work
    {
      auto const &name = it.name();
      image.planes[name].resize(image.width * image.height);
      // OpenEXR needs to be passed a pointer that, when the window is applied,
      // leads to the start of the data buffer, even if that pointer is illegal
      auto startOffset = window.min.x + window.min.y * image.width;
      fb.insert(name, Slice(FLOAT,
                            reinterpret_cast<char *>(image.planes[name].data() -
                                                     startOffset),
                            strideX, strideY));
    }
    file.setFrameBuffer(fb);
    file.readPixels(window.min.y, window.max.y);
  }
  catch (...)
  {
  }
  return image;
}

EXRImage loadEXRVec(std::vector<char> const &bytes)
{
  return loadEXRRaw(bytes.data(), bytes.size());
}

EXRImage loadEXRStr(std::string const &bytes)
{
  return loadEXRRaw(bytes.data(), bytes.size());
}

void enableMultithreading(int no_threads)
{
  if (ILMTHREAD_NAMESPACE::supportsThreads())
  {
    OPENEXR_IMF_NAMESPACE::setGlobalThreadCount(no_threads);
    std::cout << "[OpenEXR] number of threads: "
              << OPENEXR_IMF_NAMESPACE::globalThreadCount() << std::endl;
  }
}

std::vector<float> FPunzip(std::string const &bytes)
{
  std::cout << "[fpunzip] " << bytes.size() << " bytes." << std::endl;

  FPZ *fpz = fpzip_read_from_buffer(bytes.data());

  /* read header */
  if (!fpzip_read_header(fpz))
  {
    fprintf(stderr, "cannot read header: %s\n", fpzip_errstr[fpzip_errno]);
    return std::vector<float>();
  }

  // decompress into <spectrum.data()>
  uint32_t spec_len = fpz->nx;

  if (spec_len == 0)
  {
    fprintf(stderr, "zero-sized fpzip array\n");
    return std::vector<float>();
  }

  std::vector<float> spectrum(spec_len, 0.0f);

  if ((fpz->ny != 1) || (fpz->nz != 1) || (fpz->nf != 1))
  {
    fprintf(stderr, "array size does not match dimensions from header\n");
    return std::vector<float>();
  }

  /* perform actual decompression */
  if (!fpzip_read(fpz, spectrum.data()))
  {
    fprintf(stderr, "decompression failed: %s\n", fpzip_errstr[fpzip_errno]);
    return std::vector<float>();
  }

  fpzip_read_close(fpz);

  return spectrum;
}

// HEVC decoder
static void hevc_init(int va_count)
{
    //the "standard" way
    codec = &ff_hevc_decoder;

    if (avctx == NULL)
    {
        avctx = malloc(va_count * sizeof(AVCodecContext *));

        for (int i = 0; i < va_count; i++)
        {
            avctx[i] = avcodec_alloc_context3(codec);

            if (!avctx[i])
                printf("Failed to initialize HEVC decoder[%d].\n", i);
        }
    }

    if (avpkt == NULL)
    {
        avpkt = malloc(va_count * sizeof(AVPacket *));

        for (int i = 0; i < va_count; i++)
        {
            avpkt[i] = av_packet_alloc();

            if (!avpkt[i])
                printf("Failed to allocate HEVC packet[%d].\n", i);
            else
                av_init_packet(avpkt[i]);
        }
    }

    if (avframe == NULL)
    {
        avframe = malloc(va_count * sizeof(AVFrame *));

        for (int i = 0; i < va_count; i++)
        {
            avframe[i] = av_frame_alloc();

            if (!avframe[i])
                printf("Failed to allocate HEVC frame[%d].\n", i);
        }
    }

    for (int i = 0; i < va_count; i++)
    {
        avctx[i]->err_recognition |= AV_EF_CRCCHECK;
        if (avcodec_open2(avctx[i], codec, NULL) < 0)
        {
            printf("Failed to open the HEVC codec[%d].\n", i);
        }
    }
}

static void hevc_destroy(int va_count)
{
    if (avctx != NULL)
    {
        for (int i = 0; i < va_count; i++)
            if (avctx[i] != NULL)
                avcodec_free_context(&(avctx[i]));

        free(avctx);
        avctx = NULL;
    }

    if (avframe != NULL)
    {
        for (int i = 0; i < va_count; i++)
            if (avframe[i] != NULL)
                av_frame_free(&(avframe[i]));

        free(avframe);
        avframe = NULL;
    }

    if (avpkt != NULL)
    {
        for (int i = 0; i < va_count; i++)
            if (avpkt[i] != NULL)
                av_packet_free(&(avpkt[i]));

        free(avpkt);
        avpkt = NULL;
    }
}

static double hevc_decode_nal_unit(int index, const unsigned char *data, size_t data_len, unsigned char *canvas, unsigned int _w, unsigned int _h, const unsigned char *alpha, unsigned char *bytes, const char *colourmap)
{
    if (avctx == NULL || avpkt == NULL || avframe == NULL)
        return 0.0;

    if (avctx[index] == NULL || avpkt[index] == NULL || avframe[index] == NULL)
        return 0.0;

    double start = emscripten_get_now();
    double stop = 0.0;

    printf("HEVC: decoding a NAL unit of length %zu bytes\n", data_len);

    uint8_t *buf = malloc(data_len + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(buf, data, data_len);
    memset(buf + data_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    avpkt[index]->data = (uint8_t *)buf;
    avpkt[index]->size = data_len;

    int ret = avcodec_send_packet(avctx[index], avpkt[index]);

    stop = emscripten_get_now();

    printf("[wasm hevc] ret = %d, elapsed time %5.2f [ms]\n", ret, (stop - start));

    if (ret == AVERROR(EAGAIN))
        printf("avcodec_receive_frame() is needed to remove decoded video frames\n");

    if (ret == AVERROR_EOF)
        printf("the decoder has been flushed\n");

    if (ret == AVERROR(EINVAL))
        printf("codec not opened or requires flush\n");

    if (ret == AVERROR(ENOMEM))
        printf("failed to add packet to internal queue etc.\n");

    while ((ret = avcodec_receive_frame(avctx[index], avframe[index])) == 0)
    {
        enum AVColorSpace cs = av_frame_get_colorspace(avframe[index]);
        int format = avframe[index]->format;

        printf("[wasm hevc] decoded a %d x %d frame in a colourspace:format %d:%d, elapsed time %5.2f [ms], colourmap: %s\n", avframe[index]->width, avframe[index]->height, cs, format, (stop - start), colourmap);

        if (format == AV_PIX_FMT_YUV444P)
        {
            printf("processing a YUV444 format\n");

            int w = avframe[index]->width;
            int h = avframe[index]->height;

            int stride_y = avframe[index]->linesize[0];
            int stride_u = avframe[index]->linesize[1];
            int stride_v = avframe[index]->linesize[2];

            const unsigned char *y = avframe[index]->data[0];
            const unsigned char *u = avframe[index]->data[1];
            const unsigned char *v = avframe[index]->data[2];

            if (w == _w && h == _h && stride_y == stride_u && stride_y == stride_v)
            {
                //carry YUV (RGB) over to the canvas
                apply_yuv(canvas, y, u, v, w, h, stride_y, alpha);
            }
            else
                printf("[wasm hevc] canvas image dimensions %d x %d do not match the decoded image size, or the y,u,v strides differ; doing nothing\n", _w, _h);
        }
        else
        {
            printf("processing a YUV400 format\n");

            //apply a colourmap etc.
            int w = avframe[index]->width;
            int h = avframe[index]->height;
            int stride = avframe[index]->linesize[0];
            const unsigned char *luma = avframe[index]->data[0];

            if (w == _w && h == _h)
            {
                //copy luma into bytes
                if (bytes != NULL)
                {
                    size_t dst_offset = 0;

                    for (int j = 0; j < h; j++)
                    {
                        size_t offset = j * stride;

                        for (int i = 0; i < w; i++)
                            bytes[dst_offset++] = luma[offset++];
                    }
                }

                //apply a colourmap
                if (strcmp(colourmap, "red") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, ocean_g, ocean_r, ocean_b, alpha);
                }
                else if (strcmp(colourmap, "green") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, ocean_r, ocean_g, ocean_b, alpha);
                }
                else if (strcmp(colourmap, "blue") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, ocean_b, ocean_r, ocean_g, alpha);
                }
                else if (strcmp(colourmap, "hot") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, hot_r, hot_g, hot_b, alpha);
                }
                else if (strcmp(colourmap, "haxby") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, haxby_r, haxby_g, haxby_b, alpha);
                }
                else if (strcmp(colourmap, "rainbow") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, true, rainbow_r, rainbow_g, rainbow_b, alpha);
                }
                else if (strcmp(colourmap, "cubehelix") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, cubehelix_r, cubehelix_g, cubehelix_b, alpha);
                }
                else if (strcmp(colourmap, "parula") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, parula_r, parula_g, parula_b, alpha);
                }
                else if (strcmp(colourmap, "inferno") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, inferno_r, inferno_g, inferno_b, alpha);
                }
                else if (strcmp(colourmap, "magma") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, magma_r, magma_g, magma_b, alpha);
                }
                else if (strcmp(colourmap, "plasma") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, plasma_r, plasma_g, plasma_b, alpha);
                }
                else if (strcmp(colourmap, "viridis") == 0)
                {
                    apply_colourmap(canvas, luma, w, h, stride, false, viridis_r, viridis_g, viridis_b, alpha);
                }
                else if (strcmp(colourmap, "negative") == 0)
                {
                    apply_greyscale(canvas, luma, w, h, stride, alpha, true);
                }
                else
                {
                    //no colour by default
                    apply_greyscale(canvas, luma, w, h, stride, alpha, false);
                };
            }
            else
                printf("[wasm hevc] canvas image dimensions %d x %d do not match the decoded image size, doing nothing\n", _w, _h);
        }

        av_frame_unref(avframe[index]);
    }

    printf("avcodec_receive_frame returned = %d\n", ret);

    double elapsed = stop - start;

    if (buf != NULL)
        free(buf);

    return elapsed;
}

EMSCRIPTEN_BINDINGS(Wrapper)
{
  register_vector<float>("Pixel");
  register_vector<char>("Bytes");
  register_map<std::string, Pixel>("Planes");
  class_<EXRImage>("EXRImage")
      .property("width", &EXRImage::width)
      .property("height", &EXRImage::height)
      .property("planes", &EXRImage::planes)
      .function("plane", &EXRImage::plane)
      .function("channels", &EXRImage::channels);
  function("loadEXRRaw", &loadEXRRaw, allow_raw_pointer<arg<0>>());
  function("loadEXRVec", &loadEXRVec);
  function("loadEXRStr", &loadEXRStr);
  function("enableMultithreading", &enableMultithreading);
  function("FPunzip", &FPunzip);
}
