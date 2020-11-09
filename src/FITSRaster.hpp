#pragma once

#include "contours.h"

#include <ipp.h>

class FITSRaster : CRaster
{
public:
    FITSRaster(std::shared_ptr<Ipp32f> _pixels, int _width, int _height)
    {
        pixels = _pixels;
        width = _width;
        height = _height;

        if (pixels)
            data = pixels.get();
        else
            data = NULL;            
    }
    ~FITSRaster()
    {
        pixels.reset();
    };

    std::shared_ptr<Ipp32f> pixels;
    Ipp32f *data;
    int width;
    int height;
};