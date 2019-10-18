#ifndef ZFP_ARRAY_H
#define ZFP_ARRAY_H

#include <string>
#include <algorithm>
#include <climits>
#include "zfp.h"
#include "zfp/memory.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace zfp
{

// abstract base class for compressed array of scalars
class array
{
protected:
  // default constructor
  array() : dims(0), type(zfp_type_none),
            nx(0), ny(0), nz(0),
            bx(0), by(0), bz(0),
            blocks(0), blkbits(0),
            bytes(0), data(0),
            zfp(0),
            shape(0)
  {
  }

  // generic array with 'dims' dimensions and scalar type 'type'
  array(uint dims, zfp_type type) : dims(dims), type(type),
                                    nx(0), ny(0), nz(0),
                                    bx(0), by(0), bz(0),
                                    blocks(0), blkbits(0),
                                    bytes(0), data(0),
                                    zfp(zfp_stream_open(0)),
                                    shape(0)
  {
  }

  // copy constructor--performs a deep copy
  array(const array &a) : data(0),
                          zfp(0),
                          shape(0)
  {
    deep_copy(a);
  }

  // protected destructor (cannot delete array through base class pointer)
  ~array()
  {
    free();
    zfp_stream_close(zfp);
  }

  // assignment operator--performs a deep copy
  array &operator=(const array &a)
  {
    deep_copy(a);
    return *this;
  }

public:
  // rate in bits per value
  double rate() const { return double(blkbits) / block_size(); }

  // set compression rate in bits per value
  double set_rate(double rate)
  {
    rate = zfp_stream_set_rate(zfp, rate, type, dims, 1);
    blkbits = zfp->maxbits;
    alloc();
    return rate;
  }

  // empty cache without compressing modified cached blocks
  virtual void clear_cache() const = 0;

  // flush cache by compressing all modified cached blocks
  virtual void flush_cache() const = 0;

  // number of bytes of compressed data
  size_t compressed_size() const { return bytes; }

  // pointer to compressed data for read or write access
  uchar *compressed_data() const
  {
    // first write back any modified cached data
    flush_cache();
    return data;
  }

protected:
  // number of values per block
  uint block_size() const { return 1u << (2 * dims); }

  // allocate memory for compressed data
  void alloc(bool clear = true)
  {
    bytes = blocks * blkbits / CHAR_BIT;
    printf("zfp::array3::alloc<clear:%d, storage:%s, bytes:%zu>\n", clear, storage.c_str(), bytes);
    if (bytes > 1 && storage != "")
    {
      //try mmap first
      int fd = open(storage.c_str(), O_RDWR | O_CREAT, (mode_t)0600);
      if (fd != -1)
      {
#if defined(__APPLE__) && defined(__MACH__)
        int stat = ftruncate(fd, bytes);
#else
        int stat = ftruncate64(fd, bytes);
#endif
        if (!stat)
        {
          void *buffer = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

          if (buffer != MAP_FAILED)
          {
            data = (uchar *)buffer;
            is_mmapped = true;
            storage_size = bytes;
            printf("zfp::array3::alloc<mmap OK>.\n");
          }
          else
            perror("mmap");
        }
        else
        {
          perror("ftruncate64");
        }
        close(fd);
      }
    }

    if (!is_mmapped)
    {
      reallocate(data, bytes, 0x100u);
      if (clear)
        std::fill(data, data + bytes, 0);
    }
    stream_close(zfp->stream);
    zfp_stream_set_bit_stream(zfp, stream_open(data, bytes));
    clear_cache();
  }

  // free memory associated with compressed data
  void free()
  {
    nx = ny = nz = 0;
    bx = by = bz = 0;
    blocks = 0;
    stream_close(zfp->stream);
    zfp_stream_set_bit_stream(zfp, 0);
    bytes = 0;
    if (!is_mmapped)
      deallocate(data);
    else
    {
      if (data != NULL)
      {
        if (munmap(data, storage_size) == -1)
          perror("un-mapping error");
        else
          printf("zfp::array3::free<munmap OK>.\n");

        storage_size = 0;
        is_mmapped = false;
      };
    };
    data = 0;
    deallocate(shape);
    shape = 0;
  }

  // perform a deep copy
  void deep_copy(const array &a)
  {
    // copy metadata
    dims = a.dims;
    type = a.type;
    nx = a.nx;
    ny = a.ny;
    nz = a.nz;
    bx = a.bx;
    by = a.by;
    bz = a.bz;
    blocks = a.blocks;
    blkbits = a.blkbits;
    bytes = a.bytes;

    // copy dynamically allocated data
    clone(data, a.data, bytes, 0x100u);
    if (zfp)
    {
      if (zfp->stream)
        stream_close(zfp->stream);
      zfp_stream_close(zfp);
    }
    zfp = zfp_stream_open(0);
    *zfp = *a.zfp;
    zfp_stream_set_bit_stream(zfp, stream_open(data, bytes));
    clone(shape, a.shape, blocks);
  }

  uint dims;           // array dimensionality (1, 2, or 3)
  zfp_type type;       // scalar type
  uint nx, ny, nz;     // array dimensions
  uint bx, by, bz;     // array dimensions in number of blocks
  uint blocks;         // number of blocks
  size_t blkbits;      // number of bits per compressed block
  size_t bytes;        // total bytes of compressed data
  mutable uchar *data; // pointer to compressed data
  zfp_stream *zfp;     // compressed stream of blocks
  uchar *shape;        // precomputed block dimensions (or null if uniform)
  std::string storage; //persistent storage via mmap
  bool is_mmapped;
  size_t storage_size;
};

} // namespace zfp

#endif
