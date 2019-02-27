#include "fits.hpp"

#include <iostream>
#include <string>
#include <cfloat>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <boost/algorithm/string.hpp>

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

FITS::FITS()
{
    std::cout << this->dataset_id << "::default constructor." << std::endl;

    this->timestamp = std::time(nullptr);
    this->fits_file_desc = -1;
    this->compressed_fits_stream = NULL;
    this->fits_file_size = 0;
    this->gz_compressed = false;
    this->header = NULL;
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
    cdelt3 = NAN;
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
    is_optical = false;
}

void FITS::update_timestamp()
{
    std::lock_guard<std::mutex> lock(fits_mutex);
    timestamp = std::time(nullptr);
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

void FITS::from_url(std::string url, std::string flux, bool is_optical)
{
    printf("downloading %s from %s\n", this->dataset_id.c_str(), url.c_str());
}

void FITS::from_path(std::string path, bool is_compressed, std::string flux, bool is_optical)
{
    printf("loading %s from %s %s gzip compression\n", this->dataset_id.c_str(), path.c_str(), (is_compressed ? "with" : "without"));

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
    bool end = false;    
    size_t total_size = 1;
    size_t offset = 0;

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

    //try again, there may be an image extension
    if (naxis == 0)
    {
        printf("%s::<no WCS info found, trying an image extension>\n", dataset_id.c_str());
        end = false;

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
    }

    printf("%s::FITS HEADER END.\n", dataset_id.c_str());

    header[offset] = '\0';
    this->has_header = true;

    //printf("%s\n", header);

    void *buffer = NULL;

    this->timestamp = std::time(nullptr);
}