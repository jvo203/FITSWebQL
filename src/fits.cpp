#include "fits.hpp"

#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

FITS::FITS()
{
    std::cout << this->dataset_id << "::default constructor." << std::endl;

    this->has_data = false;
    this->timestamp = std::time(nullptr);
    this->fits_file_desc = -1;
    this->compressed_fits_stream = NULL;
    this->fits_file_size = 0;
    this->gz_compressed = false;
    this->header = NULL;
}

FITS::FITS(std::string id, std::string flux)
{
    std::cout << id << "::constructor." << std::endl;

    this->dataset_id = id;
    this->data_id = id + "_00_00_00";
    this->flux = flux;
    this->has_data = false;
    this->timestamp = std::time(nullptr);
    this->fits_file_desc = -1;
    this->compressed_fits_stream = NULL;
    this->fits_file_size = 0;
    this->gz_compressed = false;
    this->header = NULL;
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

void FITS::update_timestamp()
{
    std::lock_guard<std::mutex> lock(fits_mutex);
    timestamp = std::time(nullptr);
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

    char hdrLine[FITS_LINE_LENGTH + 1];
    int no_hu = 0;
    bool end = false;
    int bitpix = 0, naxis = 0;
    int naxes[4];
    size_t total_size = 1;
    size_t offset = 0;

    for (int ii = 0; ii < 4; ii++)
        naxes[ii] = 1;

    hdrLine[sizeof(hdrLine) - 1] = '\0';

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
            fprintf(stderr, "CRITICAL: read less than %zd bytes from the FITS header\n", bytes_read);

        for (offset = no_hu * FITS_CHUNK_LENGTH; offset < (no_hu + 1) * FITS_CHUNK_LENGTH; offset += FITS_LINE_LENGTH)
        {
            strncpy(hdrLine, header + offset, FITS_LINE_LENGTH);
            printf("%s\n", hdrLine);

            if (strncmp(hdrLine, "END       ", 10) == 0)
                end = true;
        }

        no_hu++;
    }

    printf("%s::FITS HEADER END.\n", dataset_id.c_str());

    header[offset] = '\0';

    void *buffer = NULL;

    this->timestamp = std::time(nullptr);
}