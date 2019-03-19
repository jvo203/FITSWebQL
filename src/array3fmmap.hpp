#pragma once

#include <string>

#include <zfparray3.h>

class array3fmmap : public zfp::array3f
{
public:
  array3fmmap(std::string dataset_id, uint nx, uint ny, uint nz, double rate, float *p, size_t csize = 0UL);
  ~array3fmmap();

  /*public:
  using zfp::array3f::compressed_size;
  using zfp::array3f::flush_cache;*/

public:
  void alloc(bool clear = true);
  void free();

private:
  std::string filename;
};