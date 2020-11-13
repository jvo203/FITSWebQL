#pragma once

#include "contours.h"

#include <ipp.h>

class FITSRaster : public CRaster
{
public:
    FITSRaster(std::shared_ptr<Ipp8u> _pixels, int _width, int _height, int _stride)
    {
        pixels = _pixels;
        width = _width;
        height = _height;
        stride = _stride;        

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

        if ( (int(x) >= width) || (int(y) >= height) )
            return (0);

        int idx = int(y) * stride + int(x);

        /*if ((idx < 0) || (idx >= plane_size))
            return (0);
        else*/
        return (data[idx]);
    };

    virtual SPoint upper_bound() { return (SPoint(width - 1, height - 1)); };
    virtual SPoint lower_bound() { return (SPoint(0, 0)); };

    // attributes
private:
    std::shared_ptr<Ipp8u> pixels;
    Ipp8u *data;
    int width;
    int height;
    int stride;    
};