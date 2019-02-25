#pragma once

#include <string>
#include <ctime>
#include <mutex>

#define JVO_FITS_SERVER "jvox.vo.nao.ac.jp"
#define JVO_FITS_DB "alma"

#define FITS_CHUNK_LENGTH 2880
#define FITS_LINE_LENGTH 80

class FITS
{
public:
  FITS();
  FITS(std::string id, std::string flux);
  ~FITS();

public:
  void update_timestamp();
  void from_url(std::string url, std::string flux, bool is_optical);
  void from_path(std::string path, bool is_compressed, std::string flux, bool is_optical);

public:
  std::string dataset_id;
  bool has_data;
  std::mutex fits_mutex;

private:
  std::string data_id;
  std::string flux;
  std::time_t timestamp;
  size_t fits_file_size;
};