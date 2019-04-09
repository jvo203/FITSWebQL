
void make_histogram_ipp(const std::vector<Ipp32f> &v, Ipp32u *bins, int nbins, float pmin, float pmax)
{
    if (v.size() <= 1)
        return;

    auto start_t = steady_clock::now();

    for (int i = 0; i < nbins; i++)
        bins[i] = 0;

    int max_threads = omp_get_max_threads();

    //keep the worksize within int32 limits
    size_t total_size = v.size();
    size_t max_work_size = 1024 * 1024 * 1024;
    size_t work_size = MIN(total_size / max_threads, max_work_size);
    int num_threads = total_size / work_size;

    printf("make_histogram_ipp::num_threads: %d\n", num_threads);

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
        sts = ippiHistogramGetBufferSize(ipp32f, roi, nLevels, 1 /*nChan*/, 1 /*uniform*/, &sizeHistObj, &sizeBuffer);

        if (sts != ippStsNoErr)
            printf("%s\n", ippGetStatusString(sts));

        pHistObj = (IppiHistogramSpec *)ippsMalloc_8u(sizeHistObj);
        pBuffer = (Ipp8u *)ippsMalloc_8u(sizeBuffer);
        // initialize spec
        sts = ippiHistogramUniformInit(ipp32f, lowerLevel, upperLevel, nLevels, 1, pHistObj);

        if (sts != ippStsNoErr)
            printf("%s\n", ippGetStatusString(sts));

        // check levels of bins
        ppLevels[0] = pLevels;
        sts = ippiHistogramGetLevels(pHistObj, ppLevels);

        if (sts != ippStsNoErr)
            printf("%s\n", ippGetStatusString(sts));

        //printf_32f("pLevels:", pLevels, nBins + 1, sts);
        /*for (int i = 0; i < nbins + 1; i++)
            printf("%f\t", pLevels[i]);
        printf("\t");*/

        // calculate histogram
        sts = ippiHistogram_32f_C1R((float *)&(v[start]), work_size * sizeof(float), roi, thread_hist, pHistObj, pBuffer);
        //ispc::histogram((float *)&(v[start]), work_size, thread_hist, nbins, pmin, pmax);

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

    double elapsedSeconds = ((end_t - start_t).count()) * steady_clock::period::num / static_cast<double>(steady_clock::period::den);
    double elapsedMilliseconds = 1000.0 * elapsedSeconds;

    printf("make_histogram_ipp::elapsed time: %5.2f [ms]\n", elapsedMilliseconds);

    /*for (int i = 0; i < nbins; i++)
        printf("histogram[%d]: %u\t", i, bins[i]);
    printf("\n");*/

    std::ofstream _f("histogram_ipp.txt");
    for (int i = 0; i < nbins; i++)
        _f << bins[i] << std::endl;
}

struct IppZfp
{
    IppZfp() : buffer(NULL), len(0), pEncState(NULL), _x(0), _y(0), _z(0) {}

    void *buffer;
    size_t len;
    IppEncodeZfpState_32f *pEncState;
    //dimensions padded to be a multiple of 4
    size_t _x;
    size_t _y;
    size_t _z;
};

//Intel IPP ZFP
std::optional<struct IppZfp> iCube;

iCube = IppZfp();
iCube->_x = width + width % 4;
iCube->_y = height + height % 4;
iCube->_z = depth + depth % 4;

int encStateSize;
ippsEncodeZfpGetStateSize_32f(&encStateSize);
iCube->pEncState = (IppEncodeZfpState_32f *)ippsMalloc_8u(encStateSize);

if (iCube->pEncState == NULL)
{
    fprintf(stderr, "%s::error allocating a IppZfp state.\n", dataset_id.c_str());
    return;
}
else
    printf("%s::IppZfp::encoder state size: %d bytes.\n", dataset_id.c_str(), encStateSize);

if (iCube)
{
    std::cout << this->dataset_id << "::destructor::iCube." << std::endl;

    //release the Zfp state
    if (iCube->pEncState != NULL)
        ippsFree(iCube->pEncState);

    //unmmap the stream buffer
    if (iCube->buffer != NULL)
    {
        int ret = munmap(iCube->buffer, iCube->len);
        if (!ret)
            perror("FITS munmap::");

        //what about truncating the underlying file?
        //it needs to be done here
    }
}

pmin = FLT_MAX;
pmax = -FLT_MAX;

if (this->depth == 1)
{
    pmin = dmin;
    pmax = dmax;
}
else
{
    if (v.size() > 1)
    {
        auto i = std::minmax_element(v.begin(), v.end());
        pmin = *i.first;
        pmax = *i.second;
    }
};

printf("%s::pixel_range<%f,%f>\n", dataset_id.c_str(), pmin, pmax);