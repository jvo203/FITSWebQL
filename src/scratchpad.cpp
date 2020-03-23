
void make_histogram_ipp(const std::vector<Ipp32f> &v, Ipp32u *bins, int nbins,
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

  printf("make_histogram_ipp::num_threads: %d\n", num_threads);

#pragma omp parallel for
  for (int tid = 0; tid < num_threads; tid++) {
    Ipp32u thread_hist[NBINS];

    for (int i = 0; i < nbins; i++)
      thread_hist[i] = 0;

    size_t work_size = total_size / num_threads;
    size_t start = tid * work_size;

    if (tid == num_threads - 1)
      work_size = total_size - start;

    int nLevels[] = {nbins + 1};
    Ipp32f lowerLevel[] = {pmin};
    Ipp32f upperLevel[] = {pmax};
    Ipp32f pLevels[nbins + 1], *ppLevels[1];
    int sizeHistObj, sizeBuffer;
    IppiHistogramSpec *pHistObj;
    Ipp8u *pBuffer;
    IppStatus sts;

    IppiSize roi = {work_size, 1};

    // get sizes for spec and buffer
    sts = ippiHistogramGetBufferSize(ipp32f, roi, nLevels, 1 /*nChan*/,
                                     1 /*uniform*/, &sizeHistObj, &sizeBuffer);

    if (sts != ippStsNoErr)
      printf("%s\n", ippGetStatusString(sts));

    pHistObj = (IppiHistogramSpec *)ippsMalloc_8u(sizeHistObj);
    pBuffer = (Ipp8u *)ippsMalloc_8u(sizeBuffer);
    // initialize spec
    sts = ippiHistogramUniformInit(ipp32f, lowerLevel, upperLevel, nLevels, 1,
                                   pHistObj);

    if (sts != ippStsNoErr)
      printf("%s\n", ippGetStatusString(sts));

    // check levels of bins
    ppLevels[0] = pLevels;
    sts = ippiHistogramGetLevels(pHistObj, ppLevels);

    if (sts != ippStsNoErr)
      printf("%s\n", ippGetStatusString(sts));

    // printf_32f("pLevels:", pLevels, nBins + 1, sts);
    /*for (int i = 0; i < nbins + 1; i++)
        printf("%f\t", pLevels[i]);
    printf("\t");*/

    // calculate histogram
    sts = ippiHistogram_32f_C1R((float *)&(v[start]), work_size * sizeof(float),
                                roi, thread_hist, pHistObj, pBuffer);
    // ispc::histogram((float *)&(v[start]), work_size, thread_hist, nbins,
    // pmin, pmax);

    if (sts != ippStsNoErr)
      printf("%s\n", ippGetStatusString(sts));

    ippsFree(pHistObj);
    ippsFree(pBuffer);

#pragma omp critical
    {
      IppStatus sts = ippsAdd_32u_I(thread_hist, bins, nbins);

      if (sts != ippStsNoErr)
        printf("%s\n", ippGetStatusString(sts));
    }
  };

  auto end_t = steady_clock::now();

  double elapsedSeconds = ((end_t - start_t).count()) *
                          steady_clock::period::num /
                          static_cast<double>(steady_clock::period::den);
  double elapsedMilliseconds = 1000.0 * elapsedSeconds;

  printf("make_histogram_ipp::elapsed time: %5.2f [ms]\n", elapsedMilliseconds);

  /*for (int i = 0; i < nbins; i++)
      printf("histogram[%d]: %u\t", i, bins[i]);
  printf("\n");*/

  std::ofstream _f("histogram_ipp.txt");
  for (int i = 0; i < nbins; i++)
    _f << bins[i] << std::endl;
}

struct IppZfp {
  IppZfp() : buffer(NULL), len(0), pEncState(NULL), _x(0), _y(0), _z(0) {}

  void *buffer;
  size_t len;
  IppEncodeZfpState_32f *pEncState;
  // dimensions padded to be a multiple of 4
  size_t _x;
  size_t _y;
  size_t _z;
};

// Intel IPP ZFP
std::optional<struct IppZfp> iCube;

iCube = IppZfp();
iCube->_x = width + width % 4;
iCube->_y = height + height % 4;
iCube->_z = depth + depth % 4;

int encStateSize;
ippsEncodeZfpGetStateSize_32f(&encStateSize);
iCube->pEncState = (IppEncodeZfpState_32f *)ippsMalloc_8u(encStateSize);

if (iCube->pEncState == NULL) {
  fprintf(stderr, "%s::error allocating a IppZfp state.\n", dataset_id.c_str());
  return;
} else
  printf("%s::IppZfp::encoder state size: %d bytes.\n", dataset_id.c_str(),
         encStateSize);

if (iCube) {
  std::cout << this->dataset_id << "::destructor::iCube." << std::endl;

  // release the Zfp state
  if (iCube->pEncState != NULL)
    ippsFree(iCube->pEncState);

  // unmmap the stream buffer
  if (iCube->buffer != NULL) {
    int ret = munmap(iCube->buffer, iCube->len);
    if (!ret)
      perror("FITS munmap::");

    // what about truncating the underlying file?
    // it needs to be done here
  }
}

pmin = FLT_MAX;
pmax = -FLT_MAX;

if (this->depth == 1) {
  pmin = dmin;
  pmax = dmax;
} else {
  if (v.size() > 1) {
    auto i = std::minmax_element(v.begin(), v.end());
    pmin = *i.first;
    pmax = *i.second;
  }
};

printf("%s::pixel_range<%f,%f>\n", dataset_id.c_str(), pmin, pmax);

// remove roaring and ZFP from FITS
// fits.hpp
#include "roaring.hh"

#include <zfparray3.h>

void from_path_zfp(
    std::string path, bool is_compressed, std::string flux,
    int va_count /*, boost::shared_ptr<shared_state> const& state*/);

// ZFP compressed arrays + masks
zfp::array3f *cube;
std::vector<Roaring64Map> masks;

// fits.cpp
void FITS::from_path_zfp(
    std::string path, bool is_compressed, std::string flux,
    int va_count /*, boost::shared_ptr<shared_state> const& state*/) {
  // state_ = state;

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

  if (img_pixels != NULL)
    ippsFree(img_pixels);

  if (img_mask != NULL)
    ippsFree(img_mask);

  img_pixels = ippsMalloc_32f_L(plane_size);
  img_mask = ippsMalloc_8u_L(plane_size);

  if (img_pixels == NULL || img_mask == NULL) {
    printf("%s::cannot malloc memory for a 2D image buffer.\n",
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

    // ZFP-compressed FITS cube
    if (cube != NULL)
      delete cube;

    std::string storage = FITSCACHE + std::string("/") +
                          boost::replace_all_copy(dataset_id, "/", "_") +
                          std::string(".zfp");
    printf("%s::mmap:%s\n", dataset_id.c_str(), storage.c_str());
    cube = new zfp::array3f(width, height, depth, 4, NULL,
                            0); //,
                                // storage); //(#bits per value)
    // cube = new array3fmmap(dataset_id, width, height, depth, 4, NULL);
    // //(#bits per value)

    if (cube == NULL) {
      fprintf(stderr, "%s::error allocating a ZFP-compressed FITS data cube.\n",
              dataset_id.c_str());
      processed_data = true;
      data_cv.notify_all();
      return;
    }

    cube->flush_cache();
    size_t zfp_size = cube->compressed_size();
    size_t real_size = frame_size * depth;
    printf(
        "%s::compressed size: %zu bytes, real size: %zu bytes, ratio: %5.2f.\n",
        dataset_id.c_str(), zfp_size, real_size,
        float(real_size) / float(zfp_size));

    // compressed bitmap masks
    masks.clear();
    masks.resize(depth);

    // the rest of the variables
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

      // ZFP compressed array private_view requires blocks-of-4 scheduling for
      // thread-safe mutable access
#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads)         \
    reduction(min                                                              \
              : _pmin) reduction(max                                           \
                                 : _pmax)

      for (size_t k = 0; k < depth; k += 4) {
        int tid = omp_get_thread_num();
        // printf("tid: %d, k: %zu\n", tid, k);
        // create a mutable private view starting at k, with a maximum depth of
        // 4
        size_t start_k = k;
        size_t end_k = MIN(k + 4, depth);
        size_t depth_k = end_k - start_k;

        if (pixels_buf[tid] == NULL || mask_buf[tid] == NULL ||
            omp_pixels[tid] == NULL || omp_mask[tid] == NULL) {
          fprintf(stderr,
                  "%s::<tid::%d>::problem allocating thread-local {pixels,buf} "
                  "arrays.\n",
                  dataset_id.c_str(), tid);
          bSuccess = false;
          continue;
        }

        // get a mutable private_view to a ZFP-compressed array
        zfp::array3f::private_view view(cube, 0, 0, start_k, width, height,
                                        depth_k);
        view.set_cache_size(67108864);
        // printf("%s::tid:%d::view %d x %d x %d\n", dataset_id.c_str(), tid,
        // view.size_x(), view.size_y(), view.size_z());

        for (size_t frame = start_k; frame < end_k; frame++) {
          // printf("k: %zu\tframe: %zu\n", k, frame);

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

            float _cdelt3 =
                this->has_velocity
                    ? this->cdelt3 * this->frame_multiplier / 1000.0f
                    : 1.0f;

            ispc::make_image_spectrumF32(
                (int32_t *)pixels_buf[tid], mask_buf[tid], bzero, bscale,
                ignrval, datamin, datamax, _cdelt3, omp_pixels[tid],
                omp_mask[tid], fmin, fmax, mean, integrated, plane_size);

            _pmin = MIN(_pmin, fmin);
            _pmax = MAX(_pmax, fmax);
            frame_min[frame] = fmin;
            frame_max[frame] = fmax;
            mean_spectrum[frame] = mean;
            integrated_spectrum[frame] = integrated;

            Roaring64Map &bitmask = masks[frame];

            // pixels_buf[tid] now contains floating-point data
            // fill-in the compressed array
            Ipp32f *thread_pixels = pixels_buf[tid];
            Ipp8u *thread_mask = mask_buf[tid];
            size_t view_offset = 0;
            for (int j = 0; j < height; j++)
              for (int i = 0; i < width; i++) {
                if (thread_mask[view_offset]) {
                  view(i, j, frame - start_k) = thread_pixels[view_offset];
                  bitmask.add(view_offset);
                } else
                  view(i, j, frame - start_k) = 0.0f;

                view_offset++;
              };

            bitmask.runOptimize();
          }
        }

        // compress all private cached blocks to shared storage
        // size_t cache_size_before_flush = view.cache_size();
        view.flush_cache();
        /*size_t cache_size_after_flush = view.cache_size();
        printf("private_view cache size before (%zu) and after a flush (%zu) "
               "bytes\n",
               cache_size_before_flush, cache_size_after_flush);*/

        send_progress_notification(end_k, depth);
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

      // a test print-out of the cube (the middle  plane)
      /*zfp::array3f::private_const_view view(cube);
        for (int i = 0; i < 10; i++)
        printf("%f\t", (double)view(i, 0, depth / 2));
        printf("\n+++++++++++++++++++++++\n");*/
    } else {
      printf("%s::gz-compressed depth > 1: work-in-progress.\n",
             dataset_id.c_str());

#pragma omp parallel num_threads(no_omp_threads)
      {
#pragma omp single
        {
          // ZFP requires blocks-of-4 processing
          for (size_t k = 0; k < depth; k += 4) {
            // create a mutable private view starting at k, with a maximum depth
            // of 4
            size_t start_k = k;
            size_t end_k = MIN(k + 4, depth);
            size_t depth_k = end_k - start_k;

            std::shared_ptr<std::vector<std::shared_ptr<Ipp32f>>> vec_pixels(
                new std::vector<std::shared_ptr<Ipp32f>>());
            std::shared_ptr<std::vector<std::shared_ptr<Ipp8u>>> vec_mask(
                new std::vector<std::shared_ptr<Ipp8u>>());

            // create private_view in the OpenMP task launched once every four
            // frames use the same construct for non-compressed FITS files

            for (size_t frame = start_k; frame < end_k; frame++) {
              // printf("k: %zu\tframe: %zu\n", k, frame);

              // allocate {pixel_buf, mask_buf}
              std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size),
                                                 Ipp32fFree);
              std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size),
                                              Ipp8uFree);
              // std::unique_ptr<Ipp32f, decltype(Ipp32fFree)>
              // pixels_buf(ippsMalloc_32f_L(plane_size), Ipp32fFree);
              // std::unique_ptr<Ipp8u, decltype(Ipp8uFree)>
              // mask_buf(ippsMalloc_8u_L(plane_size), Ipp8uFree);

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
                  ignrval, datamin, datamax, _cdelt3, img_pixels, img_mask,
                  fmin, fmax, mean, integrated, plane_size);

              _pmin = MIN(_pmin, fmin);
              _pmax = MAX(_pmax, fmax);
              frame_min[frame] = fmin;
              frame_max[frame] = fmax;
              mean_spectrum[frame] = mean;
              integrated_spectrum[frame] = integrated;

              vec_pixels->push_back(pixels_buf);
              vec_mask->push_back(mask_buf);
            }

            // lastly ZFP-compress in an OpenMP task
#pragma omp task
            {
              // printf("OpenMP<task::start:%zu,depth::%zu>::started.
              // vec_pixels::size():%zu,vec_mask::size():%zu\n", start_k,
              // depth_k, vec_pixels->size(), vec_mask->size());

              if (depth_k != vec_pixels->size() ||
                  depth_k != vec_mask->size()) {
                printf("%s::CRITICAL::depth_k != vec_pixels.size() || depth_k "
                       "!= vec_mask.size().\n",
                       dataset_id.c_str());
                bSuccess = false;
              } else {
                zfp::array3f::private_view view(cube, 0, 0, start_k, width,
                                                height, depth_k);
                view.set_cache_size(67108864);
                // printf("%s::start_k:%zu::view %d x %d x %d\n",
                // dataset_id.c_str(), start_k, view.size_x(), view.size_y(),
                // view.size_z());

                for (size_t frame = 0; frame < depth_k; frame++) {
                  Roaring64Map &bitmask = masks[start_k + frame];

                  // fill-in the compressed array
                  Ipp32f *thread_pixels = (*vec_pixels)[frame].get();
                  Ipp8u *thread_mask = (*vec_mask)[frame].get();
                  size_t view_offset = 0;
                  for (int j = 0; j < height; j++)
                    for (int i = 0; i < width; i++) {
                      if (thread_mask[view_offset]) {
                        view(i, j, frame) = thread_pixels[view_offset];
                        bitmask.add(view_offset);
                      } else
                        view(i, j, frame) = 0.0f;

                      view_offset++;
                    };

                  bitmask.runOptimize();
                }

                // compress all private cached blocks to shared storage
                // size_t cache_size_before_flush = view.cache_size();
                view.flush_cache();
                /*size_t cache_size_after_flush = view.cache_size();
                printf("private_view cache size before (%zu) and after a flush "
                       "(%zu) bytes\n",
                       cache_size_before_flush, cache_size_after_flush);*/
              }
              // printf("OpenMP<task::start:%zu>::finished.\n", start_k);
              send_progress_notification(end_k, depth);
            }
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


IppStatus Resize_32f_C1R(const Ipp32f *pSrc, IppiSize srcSize, int srcStep,
                         IppiRect srcROI, Ipp32f *pDst, int dstStep,
                         IppiSize dstRoiSize, double xFactor, double yFactor,
                         int interpolation) {
  IppStatus status = ippStsNoErr;   // status flag
  IppiResizeSpec_32f *pSpec = NULL; // specification structure buffer
  int specSize = 0;                 // size of specification structure buffer
  int initSize = 0; // size of initialization buffer (only cubic and lanzcos
                    // interpolation type use this)
  int bufSize = 0;  // size of working buffer
  Ipp8u *pBuffer = NULL;        // working buffer
  Ipp8u *pInit = NULL;          // initialization buffer
  IppiPoint dstOffset = {0, 0}; // offset to destination image, default is {0,0}
  IppiBorderType borderType = ippBorderRepl; // borderType, default is
                                             // <span>ippBorderRepl </span>
  Ipp32f borderValue = 0;                    // border value, default is zero
  Ipp32u antialiasing = 0;                   // not use antialiasing
  Ipp32u numChannels = 1; // this function works with 1 channel
  Ipp32f valueB = 0.0f;   // default value for cubic interpolation type
  Ipp32f valueC = 0.0f;   // default value for cubic interpolation type
  Ipp32u numLobes = 2;    // default value for lanczos interpolation type
  IppiInterpolationType interpolateType; // interpolation type
  IppiSize srcRoiSize;                   // size of source ROI
  IppiSize resizeSrcRoiSize;             // size of resize source ROI

  // Check pSrc and pDst not NULL
  if ((pSrc == NULL) || (pDst == NULL)) {
    return ippStsNullPtrErr;
  }

  // Check srcSize and dstRoiSize not have field with zero or negative
  // number
  if ((srcSize.width <= 0) || (srcSize.height <= 0) ||
      (dstRoiSize.width <= 0) || (dstRoiSize.height <= 0)) {
    return ippStsSizeErr;
  }

  // Check srcRoi has no intersection with the source image
  IppiPoint topLeft = {srcROI.x, srcROI.y};
  IppiPoint topRight = {srcROI.x + srcROI.width, srcROI.y};
  IppiPoint bottomLeft = {srcROI.x, srcROI.y + srcROI.height};
  IppiPoint bottomRight = {srcROI.x + srcROI.width, srcROI.y + srcROI.height};
  if (((topLeft.x < 0 || topLeft.x > srcSize.width) ||
       (topLeft.y < 0 || topLeft.y > srcSize.height)) &&
      ((topRight.x < 0 || topRight.x > srcSize.width) ||
       (topRight.y < 0 || topRight.y > srcSize.height)) &&
      ((bottomLeft.x < 0 || bottomLeft.x > srcSize.width) ||
       (bottomLeft.y < 0 || bottomLeft.y > srcSize.height)) &&
      ((bottomRight.x < 0 || bottomRight.x > srcSize.width) ||
       (bottomRight.y < 0 || bottomRight.y > srcSize.height))) {
    return ippStsWrongIntersectROI;
  }

  // Check xFactor or yFactor is not less than or equal to zero
  if ((xFactor <= 0) || (yFactor <= 0)) {
    return ippStsResizeFactorErr;
  }

  // Get interpolation filter type
  switch (interpolation) {
  case IPPI_INTER_NN:
    interpolateType = ippNearest;
    break;
  case IPPI_INTER_LINEAR:
    interpolateType = ippLinear;
    break;
  case IPPI_INTER_CUBIC:
    interpolateType = ippCubic;
    break;
  case IPPI_INTER_SUPER:
    interpolateType = ippSuper;
    break;
  case IPPI_INTER_LANCZOS:
    interpolateType = ippLanczos;
    break;
  default:
    return ippStsInterpolationErr;
  }

  // Set pSrcRoi to top-left corner of source ROI
  Ipp32f *pSrcRoi =
      (Ipp32f *)((Ipp8u *)pSrc + srcROI.y * srcStep) + srcROI.x * numChannels;

  // Set size of source ROI
  srcRoiSize.width = srcROI.width;
  srcRoiSize.height = srcROI.height;

  // Calculate size of resize source ROI
  resizeSrcRoiSize.width = (int)ceil(srcRoiSize.width * xFactor);
  resizeSrcRoiSize.height = (int)ceil(srcRoiSize.height * yFactor);

  // Get size of specification structure buffer and initialization buffer.
  status = ippiResizeGetSize_8u(srcRoiSize, resizeSrcRoiSize, interpolateType,
                                antialiasing, &specSize, &initSize);
  if (status != ippStsNoErr) {
    return status;
  }

  // Allocate memory for specification structure buffer.
  pSpec = (IppiResizeSpec_32f *)ippsMalloc_8u(specSize);
  if (pSpec == NULL) {
    return ippStsNoMemErr;
  }

  // Initialize specification structure buffer correspond to interpolation
  // type
  switch (interpolation) {
  case IPPI_INTER_NN:
    status = ippiResizeNearestInit_32f(srcRoiSize, resizeSrcRoiSize, pSpec);
    break;
  case IPPI_INTER_LINEAR:
    status = ippiResizeLinearInit_32f(srcRoiSize, resizeSrcRoiSize, pSpec);
    break;
  case IPPI_INTER_CUBIC:
    pInit = ippsMalloc_8u(initSize);
    status = ippiResizeCubicInit_32f(srcRoiSize, resizeSrcRoiSize, valueB,
                                     valueC, pSpec, pInit);
    ippsFree(pInit);
    break;
  case IPPI_INTER_SUPER:
    status = ippiResizeSuperInit_32f(srcRoiSize, resizeSrcRoiSize, pSpec);
    break;
  case IPPI_INTER_LANCZOS:
    pInit = ippsMalloc_8u(initSize);
    status = ippiResizeLanczosInit_32f(srcRoiSize, resizeSrcRoiSize, numLobes,
                                       pSpec, pInit);
    ippsFree(pInit);
    break;
  }
  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  // Get work buffer size
  status = ippiResizeGetBufferSize_8u(pSpec, resizeSrcRoiSize, numChannels,
                                      &bufSize);
  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  // Allocate memory for work buffer.
  pBuffer = ippsMalloc_8u(bufSize);
  if (pBuffer == NULL) {
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }
  // Execute resize processing correspond to interpolation type
  switch (interpolation) {
  case IPPI_INTER_NN:
    status = ippiResizeNearest_32f_C1R(pSrcRoi, srcStep, pDst, dstStep,
                                       dstOffset, dstRoiSize, pSpec, pBuffer);
    break;
  case IPPI_INTER_LINEAR:
    status = ippiResizeLinear_32f_C1R(pSrcRoi, srcStep, pDst, dstStep,
                                      dstOffset, dstRoiSize, borderType,
                                      &borderValue, pSpec, pBuffer);
    break;
  case IPPI_INTER_CUBIC:
    status = ippiResizeCubic_32f_C1R(pSrcRoi, srcStep, pDst, dstStep, dstOffset,
                                     dstRoiSize, borderType, &borderValue,
                                     pSpec, pBuffer);
    break;
  case IPPI_INTER_SUPER:
    status = ippiResizeSuper_32f_C1R(pSrcRoi, srcStep, pDst, dstStep, dstOffset,
                                     dstRoiSize, pSpec, pBuffer);
    break;
  case IPPI_INTER_LANCZOS:
    status = ippiResizeLanczos_32f_C1R(pSrcRoi, srcStep, pDst, dstStep,
                                       dstOffset, dstRoiSize, borderType,
                                       &borderValue, pSpec, pBuffer);
    break;
  }

  // Free memory
  ippsFree(pSpec);
  ippsFree(pBuffer);

  return status;
}