#include <cuda_runtime.h>
#include <nppi.h>


#include <fstream>  // ifstream
#include <iostream> // cout, cerr
#include <sstream>  // stringstream
using namespace std;

int main() {
  int x = 0, y = 0, width = 0, height = 0;
  ifstream infile("zero.pgm");
  stringstream ss;
  string inputLine = "";

  // First line : version
  getline(infile, inputLine);
  if (inputLine.compare("P5") != 0)
    cerr << "Version error" << endl;
  else
    cout << "Version : " << inputLine << endl;

  // Second line : comment
  getline(infile, inputLine);
  cout << "Comment : " << inputLine << endl;

  // Continue with a stringstream
  ss << infile.rdbuf();
  // Third line : size
  ss >> width >> height;
  cout << width << " x " << height << endl;

  uint8_t array[height][width];

  // Following lines : data
  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      ss >> array[y][x];

  // Now print the array to see the result
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      cout << (int)array[y][x] << " ";
    }
    cout << endl;
  }
  infile.close();

  // prepare the source arrays
  size_t img_size = width * height;

  float *pix32f = (float*) calloc(img_size, sizeof(float));
  uint8_t *pix8u = (uint8_t*) calloc(img_size, sizeof(uint8_t));

  size_t offset = 0;

  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++) {
      pix8u[offset] = array[y][x];
      pix32f[offset] = (float)array[y][x];
      offset++;
    }

  // the resize part
  int img_width = width / 2;
  int img_height = height / 2;
  size_t plane_size = img_width * img_height;

  float *dstPix32f = (float*) calloc(plane_size, sizeof(uint8_t));
  uint8_t *dstPix8u = (uint8_t*) calloc(plane_size, sizeof(uint8_t));

  // 8-bit unsigned integer pixels
  {
    NppiSize srcSize;
    srcSize.width = width;
    srcSize.height = height;
    int srcStep = srcSize.width;

    NppiRect srcROI = {0, 0, srcSize.width, srcSize.height};

    NppiSize dstSize;
    dstSize.width = img_width;
    dstSize.height = img_height;
    int dstStep = dstSize.width;

    NppiRect dstROI = {0, 0, dstSize.width, dstSize.height};

    NppStatus status = nppiResize_8u_C1R 	(pix8u,
		  srcStep,
		  srcSize,
		  srcROI,
		  dstPix8u,
		  dstStep,
		  dstSize,
		  dstROI,
		  NPPI_INTER_LANCZOS3_ADVANCED
    );

    std::cout << "NppStatus = " << status << std::endl;

    // export luma to a PGM file for a cross-check
    std::string filename = "zero_half.pgm";
    std::fstream pgm_file(filename, std::ios::out | std::ios::binary);

    pgm_file << "P5" << std::endl;
    pgm_file << img_width << " " << img_height << " 255" << std::endl;
    pgm_file.write((const char *)dstPix8u, plane_size);
    pgm_file.close();
  }

  // 32-bit floating-point pixels
  /*{
    IppiSize srcSize;
    srcSize.width = width;
    srcSize.height = height;
    Ipp32s srcStep = srcSize.width * sizeof(Ipp32f);

    IppiSize dstSize;
    dstSize.width = img_width;
    dstSize.height = img_height;
    Ipp32s dstStep = dstSize.width * sizeof(Ipp32f);
  }*/

  // release the memory
  free(pix32f);
  free(pix8u);

  free(dstPix32f);
  free(dstPix8u);
}