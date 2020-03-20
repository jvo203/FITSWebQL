#include <ipp.h>

#include <fstream>  // ifstream
#include <iostream> // cout, cerr
#include <sstream>  // stringstream
using namespace std;

IppStatus resizeExample_C1R(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep,
                            Ipp8u *pDst, IppiSize dstSize, Ipp32s dstStep);

IppStatus Resize8u(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep, Ipp8u *pDst,
                   IppiSize dstSize, Ipp32s dstStep);

IppStatus Resize32f(Ipp32f *pSrc, IppiSize srcSize, Ipp32s srcStep,
                    Ipp32f *pDst, IppiSize dstSize, Ipp32s dstStep);                    

int main() {
  int x = 0, y = 0, width = 0, height = 0, maxval = 0;

  string inputLine = "";
  std::string filename = "zero.pgm";
  std::ifstream pgm_file(filename, std::ios::out | std::ios::binary);

  getline(pgm_file, inputLine);

  if (inputLine.compare("P5") != 0)
    cerr << "Version error" << endl;
  else
    cout << "Version : " << inputLine << endl;

  // Second line : comment
  getline(pgm_file, inputLine);
  cout << "Comment : " << inputLine << endl;

  pgm_file >> width >> height >> maxval;
  cout << width << " x " << height << " maxval: " << maxval << endl;

  size_t img_size = width * height;
  uint8_t array[img_size];

  pgm_file.read((char*)array, img_size);
  pgm_file.close();

  {
    // export luma to a PGM file for a cross-check
    std::string filename = "zero_src.pgm";
    std::fstream pgm_file(filename, std::ios::out | std::ios::binary);

    pgm_file << "P5" << std::endl;
    pgm_file << width << " " << height << " 255" << std::endl;
    pgm_file.write((const char *)array, img_size);
    pgm_file.close();
  }

  // prepare the source arrays
  Ipp32f *pix32f = ippsMalloc_32f_L(img_size);
  Ipp8u *pix8u = ippsMalloc_8u_L(img_size);

  for(size_t i=0; i<img_size; i++) {
    pix8u[i] = array[i];
    pix32f[i] = (float)array[i];
  }

  // the resize part
  int img_width = width / 2;
  int img_height = height / 2;
  size_t plane_size = img_width * img_height;

  Ipp32f *dstPix32f = ippsMalloc_32f_L(plane_size);
  Ipp8u *dstPix8u = ippsMalloc_8u_L(plane_size);

  // 8-bit unsigned integer pixels
  {
    IppiSize srcSize;
    srcSize.width = width;
    srcSize.height = height;
    Ipp32s srcStep = srcSize.width;

    IppiSize dstSize;
    dstSize.width = img_width;
    dstSize.height = img_height;
    Ipp32s dstStep = dstSize.width;

    //resizeExample_C1R(pix8u, srcSize, srcStep, dstPix8u, dstSize, dstStep);
    Resize8u(pix8u, srcSize, srcStep, dstPix8u, dstSize, dstStep);

    // export luma to a PGM file for a cross-check
    std::string filename = "zero_half.pgm";
    std::fstream pgm_file(filename, std::ios::out | std::ios::binary);

    pgm_file << "P5" << std::endl;
    pgm_file << img_width << " " << img_height << " 255" << std::endl;
    pgm_file.write((const char *)dstPix8u, plane_size);
    pgm_file.close();
  }

  // 32-bit floating-point pixels
  {
    IppiSize srcSize;
    srcSize.width = width;
    srcSize.height = height;
    Ipp32s srcStep = srcSize.width * sizeof(Ipp32f);

    IppiSize dstSize;
    dstSize.width = img_width;
    dstSize.height = img_height;
    Ipp32s dstStep = dstSize.width * sizeof(Ipp32f);

    Resize32f(pix32f, srcSize, srcStep, dstPix32f, dstSize, dstStep);

    for(size_t i=0; i<plane_size; i++)
      dstPix8u[i] = (int)dstPix32f[i] ;

    // export luma to a PGM file for a cross-check
    std::string filename = "zero_half_float.pgm";
    std::fstream pgm_file(filename, std::ios::out | std::ios::binary);

    pgm_file << "P5" << std::endl;
    pgm_file << img_width << " " << img_height << " 255" << std::endl;
    pgm_file.write((const char *)dstPix8u, plane_size);
    pgm_file.close();
  }

  // release the memory
  ippsFree(pix32f);
  ippsFree(pix8u);

  ippsFree(dstPix32f);
  ippsFree(dstPix8u);
}

IppStatus Resize8u(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep, Ipp8u *pDst,
                   IppiSize dstSize, Ipp32s dstStep) {
  IppStatus status;
  // IppiPoint srcOffset = {0, 0};
  IppiPoint dstOffset = {0, 0};
  IppiBorderSize borderSize = {0, 0, 0, 0};
  IppiBorderType border = ippBorderRepl;
  const Ipp8u *pBorderValue = NULL;

  IppiResizeSpec_32f *pSpec = 0;
  int specSize = 0, initSize = 0, bufSize = 0;
  Ipp8u *pBuffer = 0;
  Ipp8u *pInitBuf = 0;

  /* Spec and init buffer sizes */
  status = ippiResizeGetSize_8u(srcSize, dstSize, ippLanczos, 0, &specSize,
                                &initSize);

  if (status != ippStsNoErr)
    return status;

  /* Memory allocation */
  pInitBuf = ippsMalloc_8u(initSize);
  pSpec = (IppiResizeSpec_32f *)ippsMalloc_8u(specSize);

  if (pInitBuf == NULL || pSpec == NULL) {
    ippsFree(pInitBuf);
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }

  /* Filter initialization */
  status = ippiResizeLanczosInit_8u(srcSize, dstSize, 3, pSpec, pInitBuf);
  ippsFree(pInitBuf);

  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  status = ippiResizeGetBorderSize_8u(pSpec, &borderSize);
  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  std::cout << "borderSize: {" << borderSize.borderLeft << ","
            << borderSize.borderTop << "," << borderSize.borderRight << ","
            << borderSize.borderBottom << "}" << std::endl;

  ippiResizeGetBufferSize_8u(pSpec, dstSize, ippC1, &bufSize);

  pBuffer = ippsMalloc_8u(bufSize);

  status =
      ippiResizeLanczos_8u_C1R(pSrc, srcStep, pDst, dstStep, dstOffset, dstSize,
                               border, pBorderValue, pSpec, pBuffer);

  ippsFree(pBuffer);

  ippsFree(pSpec);

  return status;
}

IppStatus resizeExample_C1R(Ipp8u *pSrc, IppiSize srcSize, Ipp32s srcStep,
                            Ipp8u *pDst, IppiSize dstSize, Ipp32s dstStep) {
  IppiResizeSpec_32f *pSpec = 0;
  int specSize = 0, initSize = 0, bufSize = 0;
  Ipp8u *pBuffer = 0;
  Ipp8u *pInitBuf = 0;
  Ipp32u numChannels = ippC1;
  IppiPoint dstOffset = {0, 0};
  IppStatus status = ippStsNoErr;
  IppiBorderType border = ippBorderRepl;

  /* Spec and init buffer sizes */
  status = ippiResizeGetSize_8u(srcSize, dstSize, ippLanczos, 0, &specSize,
                                &initSize);

  if (status != ippStsNoErr)
    return status;

  /* Memory allocation */
  pInitBuf = ippsMalloc_8u(initSize);
  pSpec = (IppiResizeSpec_32f *)ippsMalloc_8u(specSize);

  if (pInitBuf == NULL || pSpec == NULL) {
    ippsFree(pInitBuf);
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }

  /* Filter initialization */
  status = ippiResizeLanczosInit_8u(srcSize, dstSize, 3, pSpec, pInitBuf);
  ippsFree(pInitBuf);

  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  /* work buffer size */
  status = ippiResizeGetBufferSize_8u(pSpec, dstSize, numChannels, &bufSize);
  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  pBuffer = ippsMalloc_8u(bufSize);
  if (pBuffer == NULL) {
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }

  /* Resize processing */
  status = ippiResizeLanczos_8u_C1R(pSrc, srcStep, pDst, dstStep, dstOffset,
                                    dstSize, border, 0, pSpec, pBuffer);

  ippsFree(pSpec);
  ippsFree(pBuffer);

  return status;
}

IppStatus Resize32f(Ipp32f *pSrc, IppiSize srcSize, Ipp32s srcStep,
                    Ipp32f *pDst, IppiSize dstSize, Ipp32s dstStep) {
  IppStatus status;
  // IppiPoint srcOffset = {0, 0};
  IppiPoint dstOffset = {0, 0};
  IppiBorderSize borderSize = {0, 0, 0, 0};
  IppiBorderType border = ippBorderRepl;
  const Ipp32f *pBorderValue = NULL;

  IppiResizeSpec_32f *pSpec = 0;
  int specSize = 0, initSize = 0, bufSize = 0;
  Ipp8u *pBuffer = 0;
  Ipp8u *pInitBuf = 0;

  /* Spec and init buffer sizes */
  status = ippiResizeGetSize_32f(srcSize, dstSize, ippLanczos, 0, &specSize,
                                &initSize);

  if (status != ippStsNoErr)
    return status;

  /* Memory allocation */
  pInitBuf = ippsMalloc_8u(initSize);
  pSpec = (IppiResizeSpec_32f *)ippsMalloc_8u(specSize);

  if (pInitBuf == NULL || pSpec == NULL) {
    ippsFree(pInitBuf);
    ippsFree(pSpec);
    return ippStsNoMemErr;
  }

  /* Filter initialization */
  status = ippiResizeLanczosInit_32f(srcSize, dstSize, 3, pSpec, pInitBuf);
  ippsFree(pInitBuf);

  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  status = ippiResizeGetBorderSize_32f(pSpec, &borderSize);
  if (status != ippStsNoErr) {
    ippsFree(pSpec);
    return status;
  }

  std::cout << "borderSize: {" << borderSize.borderLeft << ","
            << borderSize.borderTop << "," << borderSize.borderRight << ","
            << borderSize.borderBottom << "}" << std::endl;

  ippiResizeGetBufferSize_32f(pSpec, dstSize, ippC1, &bufSize);

  pBuffer = ippsMalloc_8u(bufSize);

  status =
      ippiResizeLanczos_32f_C1R(pSrc, srcStep, pDst, dstStep, dstOffset, dstSize,
                               border, pBorderValue, pSpec, pBuffer);

  ippsFree(pBuffer);

  ippsFree(pSpec);

  return status;
}