#pragma once

#include <string>
#include <ctime>
#include <mutex>

class FITS
{
  public:
    FITS();
    FITS(std::string id, std::string flux);
    ~FITS();

  public:
    void update_timestamp();

  public:
    std::string dataset_id;
    bool has_data;
    std::mutex fits_mutex;

  private:
    std::string data_id;
    std::string flux;
    std::time_t timestamp;
};