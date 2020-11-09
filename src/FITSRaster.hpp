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
        plane_size = width * height;

        if (pixels)
            data = pixels.get();
        else
            data = NULL;
    }

    ~FITSRaster()
    {
        pixels.reset();
    };

    // inherited virtual methods
    virtual double value(double x, double y)
    {
        if (data == NULL)
            return (0);

        int idx = int(y) * width + int(x);

        if (idx >= plane_size)
            return (0);
        else
            return (data[idx]);
    };

    virtual SPoint upper_bound() { return (SPoint(width - 1, height - 1)); };
    virtual SPoint lower_bound() { return (SPoint(0, 0)); };

    // attributes
private:
    std::shared_ptr<Ipp32f> pixels;
    Ipp32f *data;
    int width;
    int height;
    int plane_size;
};