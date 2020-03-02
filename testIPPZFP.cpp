#include <stdio.h>
#include <math.h>

#include <ippdc.h>
#include <ipps.h>

#define NX 100
#define NY 100
#define NZ 100

static double accuracy[] = {
    1.e-1, 1.e-2, 1.e-3, 1.e-4, 1.e-5, 1.e-6, 1.e-7
};
void InitSrcArray(Ipp32f*, int, int, int);
void Compress(const Ipp32f* pSrc, int maxX, int maxY, int maxZ, Ipp64f accur, Ipp8u* pDst, int* pComprLen);
void Decompress(const Ipp8u* pSrc, int srcLen, int maxX, int maxY, int maxZ, Ipp64f accur, Ipp32f* pDst);

int main()
{
    Ipp32f *pSrcArray, *pDstArray;
    Ipp8u *pBuffer;
    int accuracyIndex;
    int numFloats = NX * NY * NZ;

    pSrcArray = ippsMalloc_32f(numFloats);
    pDstArray = ippsMalloc_32f(numFloats);
    InitSrcArray(pSrcArray, NX, NY, NZ);
    pBuffer = ippsMalloc_8u(sizeof(Ipp32f) * numFloats);
    for (accuracyIndex = 0; accuracyIndex < sizeof(accuracy) / sizeof(double); accuracyIndex++)
    {
        int comprLen, i;
        double maxErr;

        Compress(pSrcArray, NX, NY, NZ, accuracy[accuracyIndex], pBuffer, &comprLen);
        printf("Accuracy = %-7g, ratio = %-5.2f, ", accuracy[accuracyIndex],
                                        (double)(sizeof(Ipp32f) * numFloats) / comprLen);
        Decompress(pBuffer, comprLen, NX, NY, NZ, accuracy[accuracyIndex], pDstArray);
        /* Absolute error calculation */
        maxErr = 0.;
        for (i = 0; i < numFloats; i++)
        {
            double locErr = fabs(pSrcArray[i] - pDstArray[i]);
            if (locErr > maxErr)
                maxErr = locErr;
        }
        printf("err = %-7.2g\n", maxErr);
    }
    ippsFree(pBuffer);
    ippsFree(pSrcArray); ippsFree(pDstArray);
}
/* Data initialization from ZFP's "simple" example */
void InitSrcArray(Ipp32f* pSrc, int dimX, int dimY, int dimZ)
{
    int i, j, k;

    for (k = 0; k < dimZ; k++)
        for (j = 0; j < dimY; j++)
            for (i = 0; i < dimX; i++) {
                double x = 2.0 * i / dimX;
                double y = 2.0 * j / dimY;
                double z = 2.0 * k / dimZ;
                pSrc[i + dimX * (j + dimY * k)] = (Ipp32f)exp(-(x * x + y * y + z * z));
            }
}

void Compress(const Ipp32f* pSrc, int maxX, int maxY, int maxZ, Ipp64f accur, Ipp8u* pDst, int* pComprLen)
{
    int encStateSize;
    IppEncodeZfpState_32f* pEncState;
    int x, y, z;
    int yStep = maxY, zStep = maxX * maxY;

    ippsEncodeZfpGetStateSize_32f(&encStateSize);
    pEncState = (IppEncodeZfpState_32f*)ippsMalloc_8u(encStateSize);
    ippsEncodeZfpInit_32f(pDst, sizeof(Ipp32f) * (maxX * maxY * maxZ), pEncState);
    ippsEncodeZfpSetAccuracy_32f(accur, pEncState);
    for (z = 0; z < maxZ; z += 4)
        for (y = 0; y < maxY; y += 4)
            for (x = 0; x < maxX; x += 4)
            {
                const Ipp32f* pData = pSrc + x + y * yStep + z * zStep;
                ippsEncodeZfp444_32f(pData, yStep * sizeof(Ipp32f), zStep * sizeof(Ipp32f), pEncState);
            }
    ippsEncodeZfpFlush_32f(pEncState);
    ippsEncodeZfpGetCompressedSize_32f(pEncState, pComprLen);
    ippsFree(pEncState);
}
void Decompress(const Ipp8u* pSrc, int srcLen, int maxX, int maxY, int maxZ, Ipp64f accur, Ipp32f* pDst)
{
    int decStateSize;
    IppDecodeZfpState_32f* pDecState;
    int x, y, z;
    int yStep = maxY, zStep = maxX * maxY;

    ippsDecodeZfpGetStateSize_32f(&decStateSize);
    pDecState = (IppDecodeZfpState_32f*)ippsMalloc_8u(decStateSize);
    ippsDecodeZfpInit_32f(pSrc, srcLen, pDecState);
    ippsDecodeZfpSetAccuracy_32f(accur, pDecState);
    for (z = 0; z < NZ; z += 4)
        for (y = 0; y < NY; y += 4)
            for (x = 0; x < NX; x += 4)
            {
                Ipp32f* pData = pDst + x + y * yStep + z * zStep;
                ippsDecodeZfp444_32f(pDecState, pData, yStep * sizeof(Ipp32f), zStep * sizeof(Ipp32f));
            }
    ippsFree(pDecState);
}
