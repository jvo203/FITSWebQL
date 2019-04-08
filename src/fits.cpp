#include "../fits.h"
#include "fits.hpp"

#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <cfloat>
#include <cmath>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <chrono>
using std::chrono::steady_clock;

#include <omp.h>

#include <boost/algorithm/string.hpp>

//Parallel STL
#ifdef __INTEL_COMPILER
#include "pstl/execution"
#include "pstl/algorithm"
#include "pstl/memory"
#endif

auto Ipp32fFree = [](Ipp32f *p) {
    static size_t counter = 0;
    if (p != NULL)
    {
        printf("freeing <Ipp32f*>#%zu\t", counter++);
        ippsFree(p);
    }
};

auto Ipp8uFree = [](Ipp8u *p) {
    static size_t counter = 0;
    if (p != NULL)
    {
        printf("freeing <Ipp8u*>#%zu\t", counter++);
        ippsFree(p);
    }
};

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

template <typename T = double, typename C>
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
        auto max_it = std::max_element(tmp_array.begin(), tmp_array.begin() + n);
        return (*max_it + tmp_array[n]) / 2.0;
    }
}

void remove_nan(std::vector<Ipp32f> &v)
{
    if (v.empty())
        return;

    size_t n = v.size();
    size_t v_end = n - 1;

    // this does not leave an empty vector when all elements are NAN
    // the first NAN remains...
    // it's OK, the median-finding functions will deal with this border-line case

    //iterate through the vector, replacing NAN/INFINITE with valid numbers from the end
    for (size_t i = 0; i <= v_end; i++)
    {
        if (!std::isfinite(v[i]))
        {
            //replace it with a finite value from the end
            while (v_end > i && !std::isfinite(v[v_end]))
                v_end--;

            if (v_end <= i)
                break;
            else
                v[i] = v[v_end--];
        }
    }

    v.resize(v_end + 1);
    printf("v: original length: %zu, after NAN/INFINITE pruning: %zu\n", n, v.size());
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
#ifdef __INTEL_COMPILER
    std::nth_element(pstl::execution::par_unseq, v.begin(), v.begin() + n, v.end());
#else
    std::nth_element(v.begin(), v.begin() + n, v.end());
#endif

    if (v.size() % 2)
    {
        medVal = v[n];
    }
    else
    {
// even sized vector -> average the two middle values
#ifdef __INTEL_COMPILER
        auto max_it = std::max_element(pstl::execution::par_unseq, v.begin(), v.begin() + n);
#else
        auto max_it = std::max_element(v.begin(), v.begin() + n);
#endif
        medVal = (*max_it + v[n]) / 2.0f;
    }

    auto end_t = steady_clock::now();

    double elapsedSeconds = ((end_t - start_t).count()) * steady_clock::period::num / static_cast<double>(steady_clock::period::den);
    double elapsedMilliseconds = 1000.0 * elapsedSeconds;

#ifdef __INTEL_COMPILER
    printf("parallel_stl_median::<value = %f, elapsed time: %5.2f [ms]>\n", v[n], elapsedMilliseconds);
#else
    printf("stl_median::<value = %f, elapsed time: %5.2f [ms]>\n", v[n], elapsedMilliseconds);
#endif

    return medVal;
}

FITS::FITS()
{
    std::cout << this->dataset_id << "::default constructor." << std::endl;

    this->timestamp = std::time(nullptr);
    this->fits_file_desc = -1;
    this->compressed_fits_stream = NULL;
    this->fits_file_size = 0;
    this->gz_compressed = false;
    this->header = NULL;
    this->pixels = NULL;
    this->mask = NULL;
    this->cube = NULL;
    this->defaults();
}

FITS::FITS(std::string id, std::string flux)
{
    std::cout << id << "::constructor." << std::endl;

    this->dataset_id = id;
    this->data_id = id + "_00_00_00";
    this->flux = flux;
    this->timestamp = std::time(nullptr);
    this->fits_file_desc = -1;
    this->compressed_fits_stream = NULL;
    this->fits_file_size = 0;
    this->gz_compressed = false;
    this->header = NULL;
    this->pixels = NULL;
    this->mask = NULL;
    this->cube = NULL;
    this->defaults();
}

FITS::~FITS()
{
    std::cout << this->dataset_id << "::destructor." << std::endl;

    if (fits_file_desc != -1)
        close(fits_file_desc);

    if (compressed_fits_stream != NULL)
        gzclose(compressed_fits_stream);

    if (header != NULL)
        free(header);

    if (pixels != NULL)
        ippsFree(pixels);

    if (mask != NULL)
        ippsFree(mask);

    if (cube != NULL)
        delete cube;
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
    bzero = 0.0f,
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
    has_frequency = false;
    has_velocity = false;
    is_optical = true;
    is_xray = false;

    dmin = -FLT_MAX;
    dmax = FLT_MAX;
    median = NAN;
}

void FITS::update_timestamp()
{
    std::lock_guard<std::mutex> lock(fits_mutex);
    timestamp = std::time(nullptr);
}

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

void FITS::get_frequency_range(double &freq_start, double &freq_end)
{
    if (has_velocity)
    {
        double c = 299792458.0; //speed of light [m/s]

        double v1 = crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
        double v2 = crval3 * frame_multiplier + cdelt3 * frame_multiplier * (double(depth) - crpix3);

        double f1 = restfrq * sqrt((1.0 - v1 / c) / (1.0 + v1 / c));
        double f2 = restfrq * sqrt((1.0 - v2 / c) / (1.0 + v2 / c));

        freq_start = MIN(f1, f2) / 1.0E9; //[Hz -> GHz]
        freq_end = MAX(f1, f2) / 1.0E9;   //[Hz -> GHz]
    }
    else if (has_frequency)
    {
        double f1 = crval3 * frame_multiplier + cdelt3 * frame_multiplier * (1.0 - crpix3);
        double f2 = crval3 * frame_multiplier + cdelt3 * frame_multiplier * (double(depth) - crpix3);

        freq_start = MIN(f1, f2) / 1.0E9; //[Hz -> GHz]
        freq_end = MAX(f1, f2) / 1.0E9;   //[Hz -> GHz]
    }
}

bool FITS::process_fits_header_unit(const char *buf)
{
    char hdrLine[FITS_LINE_LENGTH + 1];
    bool end = false;

    hdrLine[sizeof(hdrLine) - 1] = '\0';

    for (size_t offset = 0; offset < FITS_CHUNK_LENGTH; offset += FITS_LINE_LENGTH)
    {
        strncpy(hdrLine, buf + offset, FITS_LINE_LENGTH);
        //printf("%s\n", hdrLine) ;

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
    }

    return end;
}

void FITS::from_url(std::string url, std::string flux, int va_count)
{
    int no_omp_threads = MAX(omp_get_max_threads() / va_count, 1);
    printf("downloading %s from %s, va_count = %d, no_omp_threads = %d\n", this->dataset_id.c_str(), url.c_str(), va_count, no_omp_threads);
}

void FITS::from_path_zfp(std::string path, bool is_compressed, std::string flux, int va_count)
{
    auto start_t = steady_clock::now();

    int no_omp_threads = MAX(omp_get_max_threads() / va_count, 1);
    printf("loading %s from %s %s gzip compression, va_count = %d, no_omp_threads = %d\n", this->dataset_id.c_str(), path.c_str(), (is_compressed ? "with" : "without"), va_count, no_omp_threads);

    this->gz_compressed = is_compressed;

    //try to open the FITS file
    int fd = -1;
    gzFile file = NULL;

    if (is_compressed)
    {
        file = gzopen(path.c_str(), "r");

        if (!file)
        {
            printf("gzopen of '%s' failed: %s.\n", path.c_str(),
                   strerror(errno));
            return;
        }
    }
    else
    {
        fd = open(path.c_str(), O_RDONLY);

        if (fd == -1)
        {
            printf("error opening %s .", path.c_str());
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
            //fread FITS_CHUNK_LENGTH from fd into header+offset
            header = (char *)realloc(header, offset + FITS_CHUNK_LENGTH + 1); //an extra space for the ending NULL

            if (header == NULL)
                fprintf(stderr, "CRITICAL: could not (re)allocate FITS header\n");

            ssize_t bytes_read = 0;

            if (is_compressed)
                bytes_read = gzread(this->compressed_fits_stream, header + offset, FITS_CHUNK_LENGTH);
            else
                bytes_read = read(this->fits_file_desc, header + offset, FITS_CHUNK_LENGTH);

            if (bytes_read != FITS_CHUNK_LENGTH)
            {
                fprintf(stderr, "CRITICAL: read less than %zd bytes from the FITS header\n", bytes_read);
                return;
            }

            end = this->process_fits_header_unit(header + offset);

            offset += FITS_CHUNK_LENGTH;
            no_hu++;
        }

        printf("%s::FITS HEADER END.\n", dataset_id.c_str());
    }

    header[offset] = '\0';

    //test for frequency/velocity
    frame_reference_unit();
    frame_reference_type();

    if (has_frequency || has_velocity)
        is_optical = false;

    if (restfrq > 0.0)
        has_frequency = true;

    this->has_header = true;

    //printf("%s\n", header);

    if (bitpix != -32)
    {
        printf("%s::unsupported bitpix(%d), FITS data will not be read.\n", dataset_id.c_str(), bitpix);
        return;
    }

    if (width <= 0 || height <= 0 || depth <= 0)
    {
        printf("%s::incorrect dimensions (width:%ld, height:%ld, depth:%ld)\n", dataset_id.c_str(), width, height, depth);
        return;
    }

    const size_t plane_size = width * height;
    const size_t frame_size = plane_size * abs(bitpix / 8);

    if (frame_size != plane_size * sizeof(float))
    {
        printf("%s::plane_size != frame_size, is the bitpix correct?\n", dataset_id.c_str());
        return;
    }

    if (pixels != NULL)
        ippsFree(pixels);

    if (mask != NULL)
        ippsFree(mask);

    pixels = ippsMalloc_32f_L(plane_size);
    mask = ippsMalloc_8u_L(plane_size);

    if (pixels == NULL || mask == NULL)
    {
        printf("%s::cannot malloc memory for a 2D image buffer.\n", dataset_id.c_str());
        return;
    }

    std::atomic<bool> bSuccess = true;

    if (depth == 1)
    {
        //read/process the FITS plane (image) in parallel
        //unless this is a compressed file, in which case
        //the data can only be read sequentially

        //use ispc to process the plane
        //1. endianness
        //2. fill-in {pixels,mask}

        float pmin = FLT_MAX;
        float pmax = -FLT_MAX;

        //get pmin, pmax
        int max_threads = omp_get_max_threads();

        //keep the worksize within int32 limits
        size_t max_work_size = 1024 * 1024 * 1024;
        size_t work_size = MIN(plane_size / max_threads, max_work_size);
        int num_threads = plane_size / work_size;

        printf("%s::fits2float32:\tsize = %zu, work_size = %zu, num_threads = %d\n", dataset_id.c_str(), plane_size, work_size, num_threads);

        if (is_compressed)
        {
            //load data into the buffer sequentially
            ssize_t bytes_read = gzread(this->compressed_fits_stream, pixels, frame_size);

            if (bytes_read != frame_size)
            {
                fprintf(stderr, "%s::CRITICAL: read less than %zd bytes from the FITS data unit\n", dataset_id.c_str(), bytes_read);
                return;
            }
            else
                printf("%s::FITS data read OK.\n", dataset_id.c_str());

#pragma omp parallel for schedule(static) num_threads(no_omp_threads) reduction(min                   \
                                                                                : pmin) reduction(max \
                                                                                                  : pmax)
            for (int tid = 0; tid < num_threads; tid++)
            {
                size_t work_size = plane_size / num_threads;
                size_t start = tid * work_size;

                if (tid == num_threads - 1)
                    work_size = plane_size - start;

                ispc::fits2float32((int32_t *)&(pixels[start]), (uint8_t *)&(mask[start]), bzero, bscale, ignrval, datamin, datamax, pmin, pmax, work_size);
            };
        }
        else
        {
            //load data into the buffer in parallel chunks
            //the data part starts at <offset>

#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads) reduction(min                   \
                                                                                 : pmin) reduction(max \
                                                                                                   : pmax)
            for (int tid = 0; tid < num_threads; tid++)
            {
                size_t work_size = plane_size / num_threads;
                size_t start = tid * work_size;

                if (tid == num_threads - 1)
                    work_size = plane_size - start;

                //parallel read (pread) at a specified offset
                ssize_t bytes_read = pread(this->fits_file_desc, &(pixels[start]), work_size * sizeof(float), offset + start * sizeof(float));

                if (bytes_read != work_size * sizeof(float))
                {
                    fprintf(stderr, "%s::CRITICAL: only read %zd out of requested %zd bytes.\n", dataset_id.c_str(), bytes_read, (work_size * sizeof(float)));
                    bSuccess = false;
                }
                else
                    ispc::fits2float32((int32_t *)&(pixels[start]), (uint8_t *)&(mask[start]), bzero, bscale, ignrval, datamin, datamax, pmin, pmax, work_size);
            };
        }

        dmin = pmin;
        dmax = pmax;
    }
    else
    {
        printf("%s::depth > 1: work-in-progress.\n", dataset_id.c_str());

        //ZFP-compressed FITS cube
        if (cube != NULL)
            delete cube;

        std::string storage = FITSCACHE + std::string("/") + boost::replace_all_copy(dataset_id, "/", "_") + std::string(".zfp");
        printf("%s::mmap:%s\n", dataset_id.c_str(), storage.c_str());
        cube = new zfp::array3f(width, height, depth, 4, NULL, 0, storage); //(#bits per value)
        //cube = new array3fmmap(dataset_id, width, height, depth, 4, NULL); //(#bits per value)

        if (cube == NULL)
        {
            fprintf(stderr, "%s::error allocating a ZFP-compressed FITS data cube.\n", dataset_id.c_str());
            return;
        }

        cube->flush_cache();
        size_t zfp_size = cube->compressed_size();
        size_t real_size = frame_size * depth;
        printf("%s::compressed size: %zu bytes, real size: %zu bytes, ratio: %5.2f.\n", dataset_id.c_str(), zfp_size, real_size, float(real_size) / float(zfp_size));

        //the rest of the variables
        frame_min.resize(depth, FLT_MAX);
        frame_max.resize(depth, -FLT_MAX);
        mean_spectrum.resize(depth, 0.0f);
        integrated_spectrum.resize(depth, 0.0f);

        //prepare the main image/mask
        memset(mask, 0, plane_size);
        for (size_t i = 0; i < plane_size; i++)
            pixels[i] = 0.0f;

        float pmin = FLT_MAX;
        float pmax = -FLT_MAX;

        int max_threads = omp_get_max_threads();

        if (!is_compressed)
        {
            //pre-allocated floating-point read buffers
            //to reduce RAM thrashing
            std::vector<Ipp32f *> pixels_buf(max_threads);
            std::vector<Ipp8u *> mask_buf(max_threads);

            //OpenMP per-thread {pixels,mask}
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

//ZFP compressed array private_view requires blocks-of-4 scheduling for thread-safe mutable access
#pragma omp parallel for schedule(dynamic) num_threads(no_omp_threads) reduction(min                   \
                                                                                 : pmin) reduction(max \
                                                                                                   : pmax)

            for (size_t k = 0; k < depth; k += 4)
            {
                int tid = omp_get_thread_num();
                //printf("tid: %d, k: %zu\n", tid, k);
                //create a mutable private view starting at k, with a maximum depth of 4
                size_t start_k = k;
                size_t end_k = MIN(k + 4, depth);
                size_t depth_k = end_k - start_k;

                if (pixels_buf[tid] == NULL || mask_buf[tid] == NULL || omp_pixels[tid] == NULL || omp_mask[tid] == NULL)
                {
                    fprintf(stderr, "%s::<tid::%d>::problem allocating thread-local {pixels,buf} arrays.\n", dataset_id.c_str(), tid);
                    bSuccess = false;
                    continue;
                }

                //get a mutable private_view to a ZFP-compressed array
                zfp::array3f::private_view view(cube, 0, 0, start_k, width, height, depth_k);
                //printf("%s::tid:%d::view %d x %d x %d\n", dataset_id.c_str(), tid, view.size_x(), view.size_y(), view.size_z());

                for (size_t frame = start_k; frame < end_k; frame++)
                {
                    //printf("k: %zu\tframe: %zu\n", k, frame);

                    //parallel read (pread) at a specified offset
                    ssize_t bytes_read = pread(this->fits_file_desc, pixels_buf[tid], frame_size, offset + frame_size * frame);

                    if (bytes_read != frame_size)
                    {
                        fprintf(stderr, "%s::<tid::%d>::CRITICAL: only read %zd out of requested %zd bytes.\n", dataset_id.c_str(), tid, bytes_read, frame_size);
                        bSuccess = false;
                    }
                    else
                    {
                        float fmin = FLT_MAX;
                        float fmax = -FLT_MAX;
                        float mean = 0.0f;
                        float integrated = 0.0f;

                        float _cdelt3 = this->has_velocity ? this->cdelt3 * this->frame_multiplier / 1000.0f : 1.0f;

                        ispc::make_image_spectrumF32((int32_t *)pixels_buf[tid], mask_buf[tid], bzero, bscale, ignrval, datamin, datamax, _cdelt3, omp_pixels[tid], omp_mask[tid], fmin, fmax, mean, integrated, plane_size);

                        pmin = MIN(pmin, fmin);
                        pmax = MAX(pmax, fmax);
                        frame_min[frame] = fmin;
                        frame_max[frame] = fmax;
                        mean_spectrum[frame] = mean;
                        integrated_spectrum[frame] = integrated;

                        //pixels_buf[tid] now contains floating-point data
                        //fill-in the compressed array
                        Ipp32f *thread_pixels = pixels_buf[tid];
                        Ipp8u *thread_mask = mask_buf[tid];
                        size_t view_offset = 0;
                        for (int j = 0; j < height; j++)
                            for (int i = 0; i < width; i++)
                            {
                                if (thread_mask[view_offset])
                                    view(i, j, frame - start_k) = thread_pixels[view_offset];
                                else
                                    view(i, j, frame - start_k) = 0.0f;

                                view_offset++;
                            };
                    }
                }

                // compress all private cached blocks to shared storage
                view.flush_cache();
            }

            //join omp_{pixel,mask}
            float _cdelt3 = this->has_velocity ? this->cdelt3 * this->frame_multiplier / 1000.0f : 1.0f;

            //keep the worksize within int32 limits
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

                    ispc::join_pixels_masks(&(pixels[start]), &(pixels_tid[start]), &(mask[start]), &(mask_tid[start]), _cdelt3, work_size);
                }
            }

            //release memory
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

            //a test print-out of the cube (the middle  plane)
            /*zfp::array3f::private_const_view view(cube);
            for (int i = 0; i < 10; i++)
                printf("%f\t", (double)view(i, 0, depth / 2));
            printf("\n+++++++++++++++++++++++\n");*/
        }
        else
        {
            printf("%s::gz-compressed depth > 1: work-in-progress.\n", dataset_id.c_str());

#pragma omp parallel num_threads(no_omp_threads)
            {
#pragma omp single
                {
                    //ZFP requires blocks-of-4 processing
                    for (size_t k = 0; k < depth; k += 4)
                    {
                        //create a mutable private view starting at k, with a maximum depth of 4
                        size_t start_k = k;
                        size_t end_k = MIN(k + 4, depth);
                        size_t depth_k = end_k - start_k;

                        std::shared_ptr<std::vector<std::shared_ptr<Ipp32f>>> vec_pixels(new std::vector<std::shared_ptr<Ipp32f>>());
                        std::shared_ptr<std::vector<std::shared_ptr<Ipp8u>>> vec_mask(new std::vector<std::shared_ptr<Ipp8u>>());

                        //create private_view in the OpenMP task launched once every four frames
                        //use the same construct for non-compressed FITS files

                        for (size_t frame = start_k; frame < end_k; frame++)
                        {
                            //printf("k: %zu\tframe: %zu\n", k, frame);

                            //allocate {pixel_buf, mask_buf}
                            std::shared_ptr<Ipp32f> pixels_buf(ippsMalloc_32f_L(plane_size), Ipp32fFree);
                            std::shared_ptr<Ipp8u> mask_buf(ippsMalloc_8u_L(plane_size), Ipp8uFree);
                            //std::unique_ptr<Ipp32f, decltype(Ipp32fFree)> pixels_buf(ippsMalloc_32f_L(plane_size), Ipp32fFree);
                            //std::unique_ptr<Ipp8u, decltype(Ipp8uFree)> mask_buf(ippsMalloc_8u_L(plane_size), Ipp8uFree);

                            if (pixels_buf.get() == NULL || mask_buf.get() == NULL)
                            {
                                printf("%s::CRITICAL::cannot malloc memory for {pixels,mask} buffers.\n", dataset_id.c_str());
                                bSuccess = false;
                                break;
                            }

                            //load data into the buffer sequentially
                            ssize_t bytes_read = gzread(this->compressed_fits_stream, pixels_buf.get(), frame_size);

                            if (bytes_read != frame_size)
                            {
                                fprintf(stderr, "%s::CRITICAL: read less than %zd bytes from the FITS data unit\n", dataset_id.c_str(), bytes_read);
                                bSuccess = false;
                                break;
                            }

                            //process the buffer
                            float fmin = FLT_MAX;
                            float fmax = -FLT_MAX;
                            float mean = 0.0f;
                            float integrated = 0.0f;

                            float _cdelt3 = this->has_velocity ? this->cdelt3 * this->frame_multiplier / 1000.0f : 1.0f;

                            ispc::make_image_spectrumF32((int32_t *)pixels_buf.get(), mask_buf.get(), bzero, bscale, ignrval, datamin, datamax, _cdelt3, pixels, mask, fmin, fmax, mean, integrated, plane_size);

                            pmin = MIN(pmin, fmin);
                            pmax = MAX(pmax, fmax);
                            frame_min[frame] = fmin;
                            frame_max[frame] = fmax;
                            mean_spectrum[frame] = mean;
                            integrated_spectrum[frame] = integrated;

                            vec_pixels->push_back(pixels_buf);
                            vec_mask->push_back(mask_buf);
                        }

//lastly ZFP-compress in an OpenMP task
#pragma omp task
                        {
                            //printf("OpenMP<task::start:%zu,depth::%zu>::started. vec_pixels::size():%zu,vec_mask::size():%zu\n", start_k, depth_k, vec_pixels->size(), vec_mask->size());

                            if (depth_k != vec_pixels->size() || depth_k != vec_mask->size())
                            {
                                printf("%s::CRITICAL::depth_k != vec_pixels.size() || depth_k != vec_mask.size().\n", dataset_id.c_str());
                                bSuccess = false;
                            }
                            else
                            {
                                zfp::array3f::private_view view(cube, 0, 0, start_k, width, height, depth_k);
                                //printf("%s::start_k:%zu::view %d x %d x %d\n", dataset_id.c_str(), start_k, view.size_x(), view.size_y(), view.size_z());

                                for (size_t frame = 0; frame < depth_k; frame++)
                                {
                                    //fill-in the compressed array
                                    Ipp32f *thread_pixels = (*vec_pixels)[frame].get();
                                    Ipp8u *thread_mask = (*vec_mask)[frame].get();
                                    size_t view_offset = 0;
                                    for (int j = 0; j < height; j++)
                                        for (int i = 0; i < width; i++)
                                        {
                                            if (thread_mask[view_offset])
                                                view(i, j, frame) = thread_pixels[view_offset];
                                            else
                                                view(i, j, frame) = 0.0f;

                                            view_offset++;
                                        };
                                }

                                // compress all private cached blocks to shared storage
                                view.flush_cache();
                            }
                            //printf("OpenMP<task::start:%zu>::finished.\n", start_k);
                        }
                    }
                }
            }
        }

        dmin = pmin;
        dmax = pmax;

        /*printf("FMIN/FMAX\tSPECTRUM\n");
        for (int i = 0; i < depth; i++)
            printf("%d (%f):(%f)\t\t(%f):(%f)\n", i, frame_min[i], frame_max[i], mean_spectrum[i], integrated_spectrum[i]);
        printf("\n");*/
    }

    auto end_t = steady_clock::now();

    double elapsedSeconds = ((end_t - start_t).count()) * steady_clock::period::num / static_cast<double>(steady_clock::period::den);
    double elapsedMilliseconds = 1000.0 * elapsedSeconds;

    printf("%s::<data:%s>\tdmin = %f\tdmax = %f\telapsed time: %5.2f [ms]\n", dataset_id.c_str(), (bSuccess ? "true" : "false"), dmin, dmax, elapsedMilliseconds);

    if (bSuccess)
    {
        image_statistics();
    }

    this->has_data = bSuccess ? true : false;
    this->timestamp = std::time(nullptr);
}

void FITS::image_statistics()
{
    size_t len = width * height;
    std::vector<Ipp32f> v(len);
    //memcpy(v.data(), pixels, len * sizeof(Ipp32f));

    IppiSize roiSize;
    roiSize.width = width;
    roiSize.height = height;
    ippiCopy_32f_C1R(pixels, width * sizeof(Ipp32f), v.data(), width * sizeof(Ipp32f), roiSize);

    remove_nan(v);

    make_histogram(v, hist, NBINS, dmin, dmax);

    median = stl_median(v);
}

void make_histogram(const std::vector<Ipp32f> &v, Ipp32u *bins, int nbins, float pmin, float pmax)
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

        ispc::histogram((float *)&(v[start]), work_size, thread_hist, nbins, pmin, pmax);

#pragma omp critical
        {
            IppStatus sts = ippsAdd_32u_I(thread_hist, bins, nbins);

            if (sts != ippStsNoErr)
                printf("%s\n", ippGetStatusString(sts));
        };
    };

    auto end_t = steady_clock::now();

    double elapsedSeconds = ((end_t - start_t).count()) * steady_clock::period::num / static_cast<double>(steady_clock::period::den);
    double elapsedMilliseconds = 1000.0 * elapsedSeconds;

    printf("make_histogram::elapsed time: %5.2f [ms]\n", elapsedMilliseconds);
}