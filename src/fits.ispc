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

export void fits2float32(uniform int32 src[], uniform unsigned int8 mask[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float& rmin, uniform float& rmax, uniform unsigned int size)
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

export void make_image_spectrumF32(uniform int32 src[], uniform unsigned int8 mask[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float cdelt3, uniform float dst_pixels[], uniform unsigned int8 dst_mask[], uniform float& frame_min, uniform float& frame_max, uniform float& mean, uniform float& integrated, uniform unsigned int total_size)
{  
  varying float partial_sum = 0.0f ;
  varying unsigned int64 partial_count = 0 ;

  varying float partial_min = frame_min ;
  varying float partial_max = frame_max ;

  foreach(i=0 ... total_size)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[i])) ;      
      
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;

      //alternative coherent if
      cif (!nan)
	    {
	      src[i] = intbits(tmp) ;
	      mask[i] = 255;
	      dst_pixels[i] += tmp ;
	      dst_mask[i] = 255 ;
	      partial_sum += tmp ;
	      partial_count += 1 ;
	      partial_min = min(partial_min, tmp);
	      partial_max = max(partial_max, tmp);
	    }
      else
      {
	      src[i] = 0x7ff80000; // quiet NaN        
	      mask[i] = 0;
      }
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;

  if(count > 0)
    {
      
      mean = sum / (float) count ;
      integrated = sum * cdelt3 ;
    }
  else
    {
      mean = 0.0f ;
      integrated = 0.0f ;
    }

  frame_min = reduce_min(partial_min);
  frame_max = reduce_max(partial_max);  
} ;

export void join_pixels_masks(uniform float pixels[], uniform float pixels_tid[], uniform unsigned int8 mask[], uniform unsigned int8 mask_tid[], uniform unsigned int size)
{
  foreach(i=0 ... size)
    {
      pixels[i] += pixels_tid[i] ;//* cdelt3 ;
      mask[i] |= mask_tid[i] ;
    } ;
} ;

export void image_min_max(uniform float pixels[], uniform unsigned int8 mask[], uniform float cdelt3, uniform unsigned int size, uniform float& rmin, uniform float& rmax)
{
  varying float vmin = rmin ;
  varying float vmax = rmax ;
  
  foreach(i=0 ... size)
    {            
	    cif(mask[i])	
	    {
        pixels[i] *= cdelt3 ;
        float tmp = pixels[i] ;
	      vmin = min(vmin, tmp) ;
	      vmax = max(vmax, tmp) ;
	    }
      else
      {
        unsigned int tmp = 0x7ff80000; // quiet NaN
        pixels[i] = floatbits(tmp);
      }
    } ;

  rmin = reduce_min(vmin) ;
  rmax = reduce_max(vmax) ;
} ;

export void histogram(uniform float data[], uniform unsigned int size, uniform uint32_t hist[], uniform int nbins, uniform float pmin, uniform float pmax)
{
  foreach(i=0 ... size)
    {
      float tmp = (data[i] - pmin)/(pmax - pmin) ;

      cif(!( isnan(tmp) || isinf(tmp) || (tmp < 0.0f) || (tmp > 1.0f) ))
	    {	  
	      int index = clamp((int) (tmp * nbins), 0, nbins-1) ;
	      atomic_add_local(&hist[index], 1);
	    } ;
    } ;
} ;

export void asymmetric_mad(uniform float data[], uniform unsigned int8 mask[], uniform unsigned int size, uniform float median, uniform int64& count, uniform float& mad, uniform int64& countP, uniform float& madP, uniform int64& countN, uniform float& madN)
{
  varying int vcount = count ;
  varying float vmad = mad ;

  varying int vcountP = countP ;
  varying float vmadP = madP ;

  varying int vcountN = countN ;
  varying float vmadN = madN ;
  
  foreach(i=0 ... size)
    {
      float tmp = data[i] ;

      //cif(!( isnan(tmp) || isinf(tmp) ))
      cif(mask[i])
	{
	  vmad += abs(tmp - median) ;
	  vcount++ ;

	  vmadP += (tmp > median) ? (tmp - median) : 0.0f ;
	  vcountP += (tmp > median) ? 1 : 0 ;

	  vmadN += (tmp < median) ? (median - tmp) : 0.0f ;
	  vcountN += (tmp < median) ? 1 : 0 ;
	} ;
    } ;

  count = reduce_add(vcount) ;
  mad = reduce_add(vmad) ;

  countP = reduce_add(vcountP) ;
  madP = reduce_add(vmadP) ;

  countN = reduce_add(vcountN) ;
  madN = reduce_add(vmadN) ;
} ;

export uniform float pixels_mean_brightness_ratio(uniform float pixels[], uniform unsigned int8 mask[], uniform float black, uniform float sensitivity, uniform unsigned int total_size)
{
  float partial_sum = 0.0f ;
  unsigned int partial_count = 0 ;

  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {      
      float pixel = 5.0f * (pixels[j] - black) * sensitivity ;
      partial_sum += pixel > 0.0f ? pixel/(1.0f + pixel) : 0.0f ;       
      partial_count += 1 ;
    }   
  }

  uniform float sum = reduce_add(partial_sum) ;
  uniform int count = reduce_add(partial_count) ;

  if(count > 0)
    return sum / (float) count ;
  else
    return 0.0f;
} ;

export void image_to_luminance_f32_linear(uniform float cubeData[], uniform unsigned int8 mask[], uniform float black, uniform float slope, uniform unsigned int8 pixels[],  uniform unsigned int total_size)
{
  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {
      float tmp = cubeData[j] ;
      float pixel = 255.0f / ( 1.0f + exp(-6.0f * (tmp - black) * slope) ) ;
      pixels[j] = isnan(tmp) ? 0 : clamp((int) pixel, 0, 255) ;
    }
    else
      pixels[j] = 0 ;
  }
} ;

export void image_to_luminance_f32_logistic(uniform float cubeData[], uniform unsigned int8 mask[], uniform float median, uniform float sensitivity, uniform unsigned int8 pixels[],  uniform unsigned int total_size)
{
  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {
      float tmp = cubeData[j] ;
      float pixel = 255.0f / ( 1.0f + exp(-6.0f * (tmp - median) * sensitivity) ) ;
      pixels[j] = clamp( (int) pixel, 0, 255) ;
    }
    else
      pixels[j] = 0 ;
  }
} ;

export void image_to_luminance_f32_ratio(uniform float cubeData[], uniform unsigned int8 mask[], uniform float black, uniform float sensitivity, uniform unsigned int8 pixels[],  uniform unsigned int total_size)
{
  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {
      float tmp = cubeData[j] ;
      float pixel = 5.0f * (tmp - black) * sensitivity ;
      pixels[j] = pixel > 0.0f ? (int) (255.0f*pixel/(1.0f + pixel)) : 0 ;
    }
    else
      pixels[j] = 0 ;
  }
} ;

export void image_to_luminance_f32_square(uniform float cubeData[], uniform unsigned int8 mask[], uniform float black, uniform float sensitivity, uniform unsigned int8 pixels[], uniform unsigned int total_size)
{
  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {
      float tmp = cubeData[j] ;
      float pixel = (tmp - black) * sensitivity ;
      pixels[j] = pixel > 0.0f ? clamp( (int) (255.0f*pixel*pixel), 0, 255) : 0 ;
    }
    else
      pixels[j] = 0 ;
  }
} ;

export void image_to_luminance_f32_logarithmic(uniform float cubeData[], uniform unsigned int8 mask[], uniform float dmin, uniform float dmax, uniform float lmin, uniform float lmax, uniform unsigned int8 pixels[], uniform unsigned int total_size)
{
  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {
      float tmp = cubeData[j] ;
      float pixel = 0.5f + (tmp - dmin) / (dmax - dmin) ;
      pixels[j] = pixel > 0.0f ? clamp( (int) (255.0f * (log(pixel) - lmin) / (lmax - lmin)), 0, 255) : 0 ;
    }
    else
      pixels[j] = 0 ;
  }
} ;