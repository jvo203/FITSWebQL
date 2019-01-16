#include "fits.hpp"

#include <iostream>

FITS::FITS()
{
    std::cout << this->dataset_id << "::default constructor." << std::endl;

    this->has_data = false;
    this->timestamp = std::time(nullptr);
}

FITS::FITS(std::string id, std::string flux)
{
    std::cout << id << "::constructor." << std::endl;

    this->dataset_id = id;
    this->data_id = id + "_00_00_00";
    this->flux = flux;
    this->has_data = false;
    this->timestamp = std::time(nullptr);
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
}