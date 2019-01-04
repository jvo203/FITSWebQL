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