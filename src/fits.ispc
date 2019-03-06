typedef int16 int16_t;
typedef int32 int32_t;
typedef int64 int64_t;
typedef unsigned int16 uint16_t;
typedef unsigned int32 uint32_t;
typedef unsigned int64 uint64_t;

bool isinf(float tmp)
{
  unsigned int i = intbits(tmp) ;  
  return ( (i & 0x7fffffff) == 0x7f800000 ) || ( (i & 0xffffffff) == 0xff800000 ) ;
}

uint32_t bswap32(uint32_t u)
{
  return ((u&0xFF)<<24) | (((u>>8)&0xFF)<<16) | (((u>>16)&0xFF)<<8) | (((u>>24)&0xFF)<<0);
}

export void fits2float32(uniform int src[], uniform unsigned int8 mask[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float& rmin, uniform float& rmax, uniform unsigned int size)
{    
    varying float vmin = rmin ;
    varying float vmax = rmax ;

    foreach(i=0 ... size)
    {
        //in-place endian conversion, rebase the pixels
        float tmp = bzero + bscale * floatbits(bswap32(src[i]));        

        //next the min/max values & the mask
        cif(!(isnan(tmp) || isinf(tmp)) && (tmp > ignrval) && (tmp >= datamin) && (tmp <= datamax))
        {
            vmin = min(vmin, tmp) ;
            vmax = max(vmax, tmp) ;

            src[i] = intbits(tmp) ;
            mask[i] = 255;            
        }
        else
        {
            src[i] = 0x7ff80000; // quiet NaN        
            mask[i] = 0;
        }
    }

    rmin = reduce_min(vmin) ;
    rmax = reduce_max(vmax) ;
} ;
