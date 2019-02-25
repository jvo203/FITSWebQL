#include "fits.hpp"

#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

FITS::FITS()
{
    std::cout << this->dataset_id << "::default constructor." << std::endl;

    this->has_data = false;
    this->timestamp = std::time(nullptr);
    this->fits_file_size = 0;
}

FITS::FITS(std::string id, std::string flux)
{
    std::cout << id << "::constructor." << std::endl;

    this->dataset_id = id;
    this->data_id = id + "_00_00_00";
    this->flux = flux;
    this->has_data = false;
    this->timestamp = std::time(nullptr);
    this->fits_file_size = 0;
}

FITS::~FITS()
{
    std::cout << this->dataset_id << "::destructor." << std::endl;
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

    //for the time being compression is not supported yet
    if (is_compressed)
        return;

    //try to open the FITS file
    int fd = open(path.c_str(), O_RDONLY);

    if (fd == -1)
    {
        printf("error opening %s .", path.c_str());
        return;
    }

    struct stat64 st;
    stat64(path.c_str(), &st);

    this->fits_file_size = st.st_size;

    if (this->fits_file_size < FITS_CHUNK_LENGTH)
    {
        printf("error: FITS file size smaller than %d bytes.", FITS_CHUNK_LENGTH);
        return;
    }

    printf("reading FITS header...\n");

    void *buffer = NULL;

    if (fd != -1)
        close(fd);

    this->timestamp = std::time(nullptr);
}