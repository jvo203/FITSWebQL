#ifdef _OPENMP

#if defined(WITH_IPP)
/*
* This source code file was modified with Intel(R) Integrated Performance Primitives library content
*/
#endif

/* compress 1d contiguous array in parallel */
static void
_t2(compress_omp, Scalar, 1)(zfp_stream* stream, const zfp_field* field)
{
  /* array metadata */
  const Scalar* data = (const Scalar*)field->data;
  uint nx = field->nx;

  /* number of omp threads, blocks, and chunks */
  uint threads = thread_count_omp(stream);
  uint blocks = (nx + 3) / 4;
  uint chunks = chunk_count_omp(stream, blocks, threads);

  /* allocate per-thread streams */
  bitstream** bs = compress_init_par(stream, field, chunks, blocks);

  /* compress chunks of blocks in parallel */
  int chunk;
  #pragma omp parallel for num_threads(threads)
  for (chunk = 0; chunk < (int)chunks; chunk++) {
    /* determine range of block indices assigned to this thread */
    uint bmin = chunk_offset(blocks, chunks, chunk + 0);
    uint bmax = chunk_offset(blocks, chunks, chunk + 1);
    uint block;
    /* set up thread-local bit stream */
    zfp_stream s = *stream;
    zfp_stream_set_bit_stream(&s, bs[chunk]);
    /* compress sequence of blocks */
    for (block = bmin; block < bmax; block++) {
      /* determine block origin x within array */
      const Scalar* p = data;
      uint x = 4 * block;
      p += x;
      /* compress partial or full block */
      if (nx - x < 4)
        _t2(zfp_encode_partial_block_strided, Scalar, 1)(&s, p, MIN(nx - x, 4u), 1);
      else
        _t2(zfp_encode_block, Scalar, 1)(&s, p);
    }
  }

  /* concatenate per-thread streams */
  compress_finish_par(stream, bs, chunks);
}

/* compress 1d strided array in parallel */
static void
_t2(compress_strided_omp, Scalar, 1)(zfp_stream* stream, const zfp_field* field)
{
  /* array metadata */
  const Scalar* data = (const Scalar*)field->data;
  uint nx = field->nx;
  int sx = field->sx ? field->sx : 1;

  /* number of omp threads, blocks, and chunks */
  uint threads = thread_count_omp(stream);
  uint blocks = (nx + 3) / 4;
  uint chunks = chunk_count_omp(stream, blocks, threads);

  /* allocate per-thread streams */
  bitstream** bs = compress_init_par(stream, field, chunks, blocks);

  /* compress chunks of blocks in parallel */
  int chunk;
  #pragma omp parallel for num_threads(threads)
  for (chunk = 0; chunk < (int)chunks; chunk++) {
    /* determine range of block indices assigned to this thread */
    uint bmin = chunk_offset(blocks, chunks, chunk + 0);
    uint bmax = chunk_offset(blocks, chunks, chunk + 1);
    uint block;
    /* set up thread-local bit stream */
    zfp_stream s = *stream;
    zfp_stream_set_bit_stream(&s, bs[chunk]);
    /* compress sequence of blocks */
    for (block = bmin; block < bmax; block++) {
      /* determine block origin x within array */
      const Scalar* p = data;
      uint x = 4 * block;
      p += sx * (ptrdiff_t)x;
      /* compress partial or full block */
      if (nx - x < 4)
        _t2(zfp_encode_partial_block_strided, Scalar, 1)(&s, p, MIN(nx - x, 4u), sx);
      else
        _t2(zfp_encode_block_strided, Scalar, 1)(&s, p, sx);
    }
  }

  /* concatenate per-thread streams */
  compress_finish_par(stream, bs, chunks);
}

/* compress 2d strided array in parallel */
static void
_t2(compress_strided_omp, Scalar, 2)(zfp_stream* stream, const zfp_field* field)
{
  /* array metadata */
  const Scalar* data = (const Scalar*)field->data;
  uint nx = field->nx;
  uint ny = field->ny;
  int sx = field->sx ? field->sx : 1;
  int sy = field->sy ? field->sy : nx;

  /* number of omp threads, blocks, and chunks */
  uint threads = thread_count_omp(stream);
  uint bx = (nx + 3) / 4;
  uint by = (ny + 3) / 4;
  uint blocks = bx * by;
  uint chunks = chunk_count_omp(stream, blocks, threads);

  /* allocate per-thread streams */
  bitstream** bs = compress_init_par(stream, field, chunks, blocks);

  /* compress chunks of blocks in parallel */
  int chunk;
  #pragma omp parallel for num_threads(threads)
  for (chunk = 0; chunk < (int)chunks; chunk++) {
    /* determine range of block indices assigned to this thread */
    uint bmin = chunk_offset(blocks, chunks, chunk + 0);
    uint bmax = chunk_offset(blocks, chunks, chunk + 1);
    uint block;
    /* set up thread-local bit stream */
    zfp_stream s = *stream;
    zfp_stream_set_bit_stream(&s, bs[chunk]);
    /* compress sequence of blocks */
    for (block = bmin; block < bmax; block++) {
      /* determine block origin (x, y) within array */
      const Scalar* p = data;
      uint b = block;
      uint x, y;
      x = 4 * (b % bx); b /= bx;
      y = 4 * b;
      p += sx * (ptrdiff_t)x + sy * (ptrdiff_t)y;
      /* compress partial or full block */
      if (nx - x < 4 || ny - y < 4)
        _t2(zfp_encode_partial_block_strided, Scalar, 2)(&s, p, MIN(nx - x, 4u), MIN(ny - y, 4u), sx, sy);
      else
        _t2(zfp_encode_block_strided, Scalar, 2)(&s, p, sx, sy);
    }
  }

  /* concatenate per-thread streams */
  compress_finish_par(stream, bs, chunks);
}

#if defined(IPP_OPTIMIZATION_ENABLED) && !defined(_SET_TMP_BLOCK_FROM_)
#define _SET_TMP_BLOCK_FROM_
/* Copy partial data to 4x4x4 block */
static void  CopyFromPartialBlock(const Ipp32f *pSrc, int stepY, int stepZ, int sizeX, int sizeY, int sizeZ, Ipp32f *pTmpBlock)
{
    Ipp32f    *pTmp;
    int       x, y, z, serIdx;
    int       copyX, copyY, copyZ;

    for (serIdx = z = 0; z < 4; z++) {
        copyZ = (z < sizeZ) ? z : sizeZ - 1;
        for (y = 0; y < 4; y++) {
            copyY = (y < sizeY) ? y : sizeY - 1;
            pTmp = (Ipp32f*)pSrc + copyZ * stepZ + copyY * stepY;
            for (x = 0; x < 4; x++) {
                copyX = (x < sizeX) ? x : sizeX - 1;
                pTmpBlock[serIdx++] = pTmp[copyX];
            }
        }
    }
}
#endif

/* compress 3d strided array in parallel */
static void
_t2(compress_strided_omp, Scalar, 3)(zfp_stream* stream, const zfp_field* field)
{
    /* array metadata */
    const Scalar* data = (const Scalar*)field->data;
    uint nx = field->nx;
    uint ny = field->ny;
    uint nz = field->nz;
    int sx = field->sx ? field->sx : 1;
    int sy = field->sy ? field->sy : nx;
    int sz = field->sz ? field->sz : (ptrdiff_t)nx * ny;

    /* number of omp threads, blocks, and chunks */
    uint threads = thread_count_omp(stream);

    uint bx = (nx + 3) / 4;
    uint by = (ny + 3) / 4;
    uint bz = (nz + 3) / 4;
    uint blocks = bx * by * bz;

    uint chunks = chunk_count_omp(stream, blocks, threads);
    /* allocate per-thread streams */
    bitstream** bs = compress_init_par(stream, field, chunks, blocks);

#if defined (IPP_OPTIMIZATION_ENABLED)
    IppEncodeZfpState_32f* pStates = NULL;

    Ipp64u* chunk_bit_lengths = (Ipp64u*)malloc(sizeof(Ipp64u)* chunks);
    int srcBlockLineStep = nx * sizeof(Ipp32f);
    int srcBlockPlaneStep = ny * srcBlockLineStep;

    uint min_bits, max_bits, max_prec;
    int min_exp;
    int sizeState = 0;

    /**/
    zfp_stream_params(stream, &min_bits, &max_bits, &max_prec, &min_exp);

    /* allocate per-thread IppEncodeZfpState_32f */
    ippsEncodeZfpGetStateSize_32f(&sizeState);
    pStates = (IppEncodeZfpState_32f*)ippsMalloc_8u(sizeState * threads);
#endif
    /* compress chunks of blocks in parallel */
    int chunk;

#if !defined (IPP_OPTIMIZATION_ENABLED)
#pragma omp parallel for num_threads(threads)
#else
        //firstprivate (min_bits, max_bits, max_prec, min_exp)
#pragma omp parallel \
    num_threads(threads)
        {
        bitstream *pBitStream = NULL;
        IppEncodeZfpState_32f* pState = NULL;
        Ipp32f pTmpBlock[64];
        pState = (IppEncodeZfpState_32f*)((Ipp8u*)pStates + omp_get_thread_num() * sizeState);
#pragma omp for
#endif
        for (chunk = 0; chunk < (int)chunks; chunk++) {
            /* determine range of block indices assigned to this thread */
            uint bmin = chunk_offset(blocks, chunks, chunk + 0);
            uint bmax = chunk_offset(blocks, chunks, chunk + 1);
            uint block;
            /* set up thread-local bit stream */
            zfp_stream s = *stream;
            zfp_stream_set_bit_stream(&s, bs[chunk]);

#if defined (IPP_OPTIMIZATION_ENABLED)
            pBitStream = bs[chunk];
            ippsEncodeZfpInitLong_32f((Ipp8u*)stream_data(pBitStream), stream_capacity(pBitStream), pState);
            ippsEncodeZfpSet_32f(min_bits, max_bits, max_prec, min_exp, pState);
#endif  
            /* compress sequence of blocks */
            for (block = bmin; block < bmax; block++) {
                const Scalar* p = data;
                uint b = block;
                uint x, y, z;
                /* determine block origin (x, y, z) within array */
                x = 4 * (b % bx); b /= bx;
                y = 4 * (b % by); b /= by;
                z = 4 * b;

                p += sx * (ptrdiff_t)x + sy * (ptrdiff_t)y + sz * (ptrdiff_t)z;
                // compress partial or full block 
                if (nx - x < 4 || ny - y < 4 || nz - z < 4)
                {
                  #if !defined(IPP_OPTIMIZATION_ENABLED)
                    _t2(zfp_encode_partial_block_strided, Scalar, 3)(&s, p, MIN(nx - x, 4u), MIN(ny - y, 4u), MIN(nz - z, 4u), sx, sy, sz);
                  #else
                    CopyFromPartialBlock((const Ipp32f *)p, sy, sz, MIN(nx - x, 4u), MIN(ny - y, 4u), MIN(nz - z, 4u), pTmpBlock);
                    ippsEncodeZfp444_32f(pTmpBlock, 4 * sizeof(Ipp32f), 4 * 4 * sizeof(Ipp32f), pState);
                  #endif
                }
                else
                {
                  #if !defined(IPP_OPTIMIZATION_ENABLED)
                    _t2(zfp_encode_block_strided, Scalar, 3)(&s, p, sx, sy, sz);
                  #else
                    ippsEncodeZfp444_32f((const Ipp32f *)p, srcBlockLineStep, srcBlockPlaneStep, pState);
                  #endif
                }
            }

#if defined (IPP_OPTIMIZATION_ENABLED)
            if (pState != NULL)
            {
                Ipp64u chunk_compr_length;
                ippsEncodeZfpGetCompressedBitSize_32f(pState, &chunk_bit_lengths[chunk]);
                ippsEncodeZfpFlush_32f(pState);
                chunk_compr_length = (size_t)((chunk_bit_lengths[chunk] + 7) >> 3);
                stream_set_eos(pBitStream, chunk_compr_length);
            }
#endif
        }
#if defined (IPP_OPTIMIZATION_ENABLED)
    }//The end of pragma omp parallel block

    /* concatenate per-thread streams */
    if (pStates != NULL)
    {
        compress_finish_par_opt(stream, bs, chunks, chunk_bit_lengths);
        free(chunk_bit_lengths);
        ippsFree(pStates);
        return;
    }
#endif
    /* concatenate per-thread streams */

    compress_finish_par(stream, bs, chunks);

}

/* compress 4d strided array in parallel */
static void
_t2(compress_strided_omp, Scalar, 4)(zfp_stream* stream, const zfp_field* field)
{
  /* array metadata */
  const Scalar* data = field->data;
  uint nx = field->nx;
  uint ny = field->ny;
  uint nz = field->nz;
  uint nw = field->nw;
  int sx = field->sx ? field->sx : 1;
  int sy = field->sy ? field->sy : nx;
  int sz = field->sz ? field->sz : (ptrdiff_t)nx * ny;
  int sw = field->sw ? field->sw : (ptrdiff_t)nx * ny * nz;

  /* number of omp threads, blocks, and chunks */
  uint threads = thread_count_omp(stream);
  uint bx = (nx + 3) / 4;
  uint by = (ny + 3) / 4;
  uint bz = (nz + 3) / 4;
  uint bw = (nw + 3) / 4;
  uint blocks = bx * by * bz * bw;
  uint chunks = chunk_count_omp(stream, blocks, threads);

  /* allocate per-thread streams */
  bitstream** bs = compress_init_par(stream, field, chunks, blocks);

  /* compress chunks of blocks in parallel */
  int chunk;
  #pragma omp parallel for num_threads(threads)
  for (chunk = 0; chunk < (int)chunks; chunk++) {
    /* determine range of block indices assigned to this thread */
    uint bmin = chunk_offset(blocks, chunks, chunk + 0);
    uint bmax = chunk_offset(blocks, chunks, chunk + 1);
    uint block;
    /* set up thread-local bit stream */
    zfp_stream s = *stream;
    zfp_stream_set_bit_stream(&s, bs[chunk]);
    /* compress sequence of blocks */
    for (block = bmin; block < bmax; block++) {
      /* determine block origin (x, y, z, w) within array */
      const Scalar* p = data;
      uint b = block;
      uint x, y, z, w;
      x = 4 * (b % bx); b /= bx;
      y = 4 * (b % by); b /= by;
      z = 4 * (b % bz); b /= bz;
      w = 4 * b;
      p += sx * (ptrdiff_t)x + sy * (ptrdiff_t)y + sz * (ptrdiff_t)z + sw * (ptrdiff_t)w;
      /* compress partial or full block */
      if (nx - x < 4 || ny - y < 4 || nz - z < 4 || nw - w < 4)
        _t2(zfp_encode_partial_block_strided, Scalar, 4)(&s, p, MIN(nx - x, 4u), MIN(ny - y, 4u), MIN(nz - z, 4u), MIN(nw - w, 4u), sx, sy, sz, sw);
      else
        _t2(zfp_encode_block_strided, Scalar, 4)(&s, p, sx, sy, sz, sw);
    }
  }
  /* concatenate per-thread streams */
  compress_finish_par(stream, bs, chunks);
}

#endif
