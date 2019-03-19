#include "array3fmmap.hpp"
#include "fits.hpp"

#include <boost/algorithm/string/replace.hpp>
#include <sys/mman.h>

array3fmmap::array3fmmap(std::string dataset_id, uint nx, uint ny, uint nz, double rate, float *p, size_t csize) : zfp::array3f(nx, ny, nz, rate, p, csize)
{
    filename = FITSCACHE + std::string("/") + boost::replace_all_copy(dataset_id, "/", "_") + std::string(".zfp");
    printf("%s::mmap:%s\n", dataset_id.c_str(), filename.c_str());    
}

array3fmmap::~array3fmmap()
{
    //this->free();
}

// set compression rate in bits per value
/*double array3fmmap::set_rate(double rate)
{
    printf("array3fmmap::set_rate\n");
    rate = zfp_stream_set_rate(zfp, rate, type, dims, 1);
    blkbits = zfp->maxbits;
    this->alloc();
    return rate;
}*/

// allocate memory for compressed data
void array3fmmap::alloc(bool clear)
{
    printf("array3fmmap::alloc\n");
    bytes = blocks * blkbits / CHAR_BIT;
    reallocate(data, bytes, 0x100u); //to be replaced by mmap
    if (clear)
        std::fill(data, data + bytes, 0);
    stream_close(zfp->stream);
    zfp_stream_set_bit_stream(zfp, stream_open(data, bytes));
    clear_cache();
}

// free memory associated with compressed data
void array3fmmap::free()
{
    printf("array3fmmap::free");
    nx = ny = nz = 0;
    bx = by = bz = 0;
    blocks = 0;
    stream_close(zfp->stream);
    zfp_stream_set_bit_stream(zfp, 0);
    bytes = 0;
    deallocate(data); //to be replaced by unmap
    data = 0;
    deallocate(shape);
    shape = 0;
}