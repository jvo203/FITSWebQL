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

export void mirror_u8(uniform unsigned int8 image[], uniform int32 width, uniform int32 height)
{  
  foreach(j = 0 ... height / 2, i = 0 ... width)
  {
    unsigned int8 tmp = image[j * width + i];
    image[j * width + i] = image[(height - 1 - j) * width + i];
    image[(height - 1 - j) * width + i] = tmp;    
  }
}

export void mirror_float32(uniform float image[], uniform int32 width, uniform int32 height)
{  
  foreach(j = 0 ... height / 2, i = 0 ... width)
  {
    float tmp = image[j * width + i];
    image[j * width + i] = image[(height - 1 - j) * width + i];
    image[(height - 1 - j) * width + i] = tmp;    
  }
}

export void copy_mirror_float32(uniform float src[], uniform float dst[], uniform int32 width, uniform int32 height)
{  
  foreach(j = 0 ... height / 2, i = 0 ... width)
  {    
    dst[j * width + i] = src[(height - 1 - j) * width + i];
    dst[(height - 1 - j) * width + i] = src[j * width + i];
  }
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

export void nan_mask(uniform float buffer[], uniform unsigned int8 mask[], uniform unsigned int size)
{
  foreach(i=0 ... size)
    {
      if(mask[i] == 0)
        buffer[i] = floatbits(0x7ff80000); // quiet NaN        
    }
}

export void f32PIXMtof16(uniform float pixels[], uniform unsigned int8 mask[], uniform int src_x, uniform int src_y, uniform unsigned int width, uniform unsigned int height, uniform unsigned int16 dst[], uniform unsigned int size, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16)
{
  foreach (j=0 ... size, i=0 ... size)
  {
    int idx_src = (src_y + j) * width + src_x + i;
    int idx_dst = j * size + i;

    // a default value
    float tmp = floatbits(0x7ff80000); // quiet NaN

    // check whether or not the original image dimensions are exceeded
    if ((src_x + i < width) && (src_y + j < height))
      // finally test the mask
      if(mask[idx_src] != 0)
        tmp = min16 + (pixels[idx_src] - frame_min) / (frame_max - frame_min) * (max16 - min16);

    dst[idx_dst] = float_to_half_fast(tmp);
  }
}

export void f32tof16(uniform float src[], uniform unsigned int16 dst[], uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform unsigned int size)
{
  foreach(i=0 ... size)
  {
    //float tmp = min16 + (src[i] - frame_min) / (frame_max - frame_min) * (max16 - min16);
    float tmp = src[i];
    dst[i] = float_to_half_fast(tmp);
  } 
}

export void f16tof32(uniform unsigned int16 src[], uniform float dst[], uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform unsigned int size)
{
  foreach(i=0 ... size)
    dst[i] = frame_min + (half_to_float_fast(src[i]) - min16) / (max16 - min16) * (frame_max - frame_min);
}

export void make_image_spectrumF32(uniform int32 src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float cdelt3, uniform float dst_pixels[], uniform unsigned int8 dst_mask[], uniform float& frame_min, uniform float& frame_max, uniform float& mean, uniform float& integrated, uniform unsigned int total_size)
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

export void make_image_spectrumF32_ro(uniform int32 src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float cdelt3, uniform float dst_pixels[], uniform unsigned int8 dst_mask[], uniform float& frame_min, uniform float& frame_max, uniform float& mean, uniform float& integrated, uniform unsigned int total_size)
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
	      dst_pixels[i] += tmp ;
	      dst_mask[i] = 255 ;
	      partial_sum += tmp ;
	      partial_count += 1 ;
	      partial_min = min(partial_min, tmp);
	      partial_max = max(partial_max, tmp);
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

export void make_image_spectrumF16(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float cdelt3, uniform float dst_pixels[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride, uniform float& mean, uniform float& integrated)
{  
  varying float partial_sum = 0.0f ;
  varying unsigned int64 partial_count = 0 ;

  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);

      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x); // to be re-worked using destination offsets

      //alternative coherent if
      cif (!nan)
	    {
	      dst_pixels[idx] += tmp ;
	      dst_mask[idx] = 255 ;
	      partial_sum += tmp ;
	      partial_count += 1 ;
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
} ;

export void make_planeF32(uniform int32 src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float dst_pixels[], uniform unsigned int8 dst_mask[], uniform unsigned int total_size)
{    
  foreach(i=0 ... total_size)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[i])) ;      
      
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;

      //alternative coherent if
      cif (!nan)
	    {	      
	      dst_pixels[i] = tmp ;
	      dst_mask[i] = 255 ;	 
	    }
      else
      {
	      dst_pixels[i] = 0.0f;
	      dst_mask[i] = 0;
      }
    } ;    
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

export void image_to_luminance_f32_logarithmic_inplace(uniform float cubeData[], uniform unsigned int8 mask[], uniform float dmin, uniform float dmax, uniform float lmin, uniform float lmax, uniform unsigned int total_size)
{
  foreach(j=0 ... total_size)
  {
    cif(mask[j] > 0)
    {
      float tmp = cubeData[j] ;
      float pixel = 0.5f + (tmp - dmin) / (dmax - dmin) ;
      cubeData[j] = pixel > 0.0f ? clamp( (int) (255.0f * (log(pixel) - lmin) / (lmax - lmin)), 0, 255) : 0 ;
    }
    else
      cubeData[j] = 0 ;
  }
} ;

// big-endian byte order
export uniform float calculate_square_spectrumBF32(uniform int32 src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int width, uniform int x1, uniform int x2, uniform int y1, uniform int y2, uniform bool average, uniform float cdelt3)
{
  float partial_sum = 0.0f ;
  unsigned int64 partial_count = 0 ;
  
  foreach (y=y1 ... y2, x=x1 ... x2)
    {      
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x]));
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      
      partial_sum += nan ? 0.0f : tmp ;
      partial_count += nan ? 0 : 1 ;
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;

  if(count > 0)
    {
      if(average)
	      return sum / (float) count ;
      else
	      return sum * cdelt3 ;
    }
  else
    return 0.0f ;
} ;

// big-endian byte order
export uniform float calculate_radial_spectrumBF32(uniform int32 src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int width, uniform int x1, uniform int x2, uniform int y1, uniform int y2, uniform int cx, uniform int cy, uniform int r2, uniform bool average, uniform float cdelt3)
{
  float partial_sum = 0.0f ;
  unsigned int64 partial_count = 0 ;
  
  foreach (y=y1 ... y2, x=x1 ... x2)
    {      
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x]));
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);

      float dist2 = (cx-x)*(cx-x) + (cy-y)*(cy-y) ;
      bool exclude = (dist2 > r2) ;
      //tmp = (dist2 <= r2) ? tmp : 0.0f ;
      
      partial_sum += (nan || exclude) ? 0.0f : tmp ;
      partial_count += (nan || exclude) ? 0 : 1 ;
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;  

  if(count > 0)
    {
      if(average)
	      return sum / (float) count ;
      else
	      return sum * cdelt3 ;
    }
  else
    return 0.0f ;
} ;

// little-endian byte order
export uniform float calculate_square_spectrumLF32(uniform float src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int width, uniform int x1, uniform int x2, uniform int y1, uniform int y2, uniform bool average, uniform float cdelt3)
{
  float partial_sum = 0.0f ;
  unsigned int64 partial_count = 0 ;
  
  foreach (y=y1 ... y2, x=x1 ... x2)
    {      
      float tmp = bzero + bscale * src[y*width+x];
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      
      partial_sum += nan ? 0.0f : tmp ;
      partial_count += nan ? 0 : 1 ;
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;

  if(count > 0)
    {
      if(average)
	      return sum / (float) count ;
      else
	      return sum * cdelt3 ;
    }
  else
    return 0.0f ;
} ;

// little-endian byte order
export uniform float calculate_radial_spectrumLF32(uniform float src[], uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int width, uniform int x1, uniform int x2, uniform int y1, uniform int y2, uniform int cx, uniform int cy, uniform int r2, uniform bool average, uniform float cdelt3)
{
  float partial_sum = 0.0f ;
  unsigned int64 partial_count = 0 ;
  
  foreach (y=y1 ... y2, x=x1 ... x2)
    {      
      float tmp = bzero + bscale * src[y*width+x];
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);

      float dist2 = (cx-x)*(cx-x) + (cy-y)*(cy-y) ;
      bool exclude = (dist2 > r2) ;
      //tmp = (dist2 <= r2) ? tmp : 0.0f ;
      
      partial_sum += (nan || exclude) ? 0.0f : tmp ;
      partial_count += (nan || exclude) ? 0 : 1 ;
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;  

  if(count > 0)
    {
      if(average)
	      return sum / (float) count ;
      else
	      return sum * cdelt3 ;
    }
  else
    return 0.0f ;
} ;

// half-float
export uniform float calculate_square_spectrumF16(uniform unsigned int16 src[], uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int width, uniform int x1, uniform int x2, uniform int y1, uniform int y2, uniform bool average, uniform float cdelt3)
{
  float partial_sum = 0.0f ;
  unsigned int64 partial_count = 0 ;
  
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=y1 ... y2, x=x1 ... x2)
    {            
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*width+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*width+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      
      partial_sum += nan ? 0.0f : tmp ;
      partial_count += nan ? 0 : 1 ;
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;

  if(count > 0)
    {
      if(average)
	      return sum / (float) count ;
      else
	      return sum * cdelt3 ;
    }
  else
    return 0.0f ;
} ;

// half-float
export uniform float calculate_radial_spectrumF16(uniform unsigned int16 src[], uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int width, uniform int x1, uniform int x2, uniform int y1, uniform int y2, uniform int dx, uniform int dy, uniform int cx, uniform int cy, uniform int r2, uniform bool average, uniform float cdelt3)
{
  float partial_sum = 0.0f ;
  unsigned int64 partial_count = 0 ;

  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=y1 ... y2, x=x1 ... x2)
    {            
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*width+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*width+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);

      float dist2 = (cx-(x+dx))*(cx-(x+dx)) + (cy-(y+dy))*(cy-(y+dy)) ;
      bool exclude = (dist2 > r2) ;
      //tmp = (dist2 <= r2) ? tmp : 0.0f ;
      
      partial_sum += (nan || exclude) ? 0.0f : tmp ;
      partial_count += (nan || exclude) ? 0 : 1 ;
    } ;

  uniform float sum = reduce_add(partial_sum) ;
  uniform int64 count = reduce_add(partial_count) ;  

  if(count > 0)
    {
      if(average)
	      return sum / (float) count ;
      else
	      return sum * cdelt3 ;
    }
  else
    return 0.0f ;
} ;

export void make_video_frameF16(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform float dst_pixels[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride)
{
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);

      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x); // to be re-worked using destination offsets

      //alternative coherent if
      cif (!nan)
	    {
	      dst_pixels[idx] = tmp ;
	      dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_pixels[idx] = 0.0f ;
	      dst_mask[idx] = 0 ;         
      }
    } ;  
} ;

export void make_video_frameF32_linear(uniform int32 src[], uniform int width, uniform int height, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int dst_stride, uniform float black, uniform float slope)
{
  foreach (y=0 ... height, x=0 ... width)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x])) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;
      unsigned int idx = y * dst_stride + x;

      //alternative coherent if
      cif (!nan)
	    {
	      float pixel = 255.0f / ( 1.0f + exp(-6.0f * (tmp - black) * slope) ) ;             
        dst_luma[idx] = (int) clamp(pixel, 0.0f, 255.0f) ;
	      dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;         
      }
    } ;  
} ;

export void make_video_frameF32_logistic(uniform int32 src[], uniform int width, uniform int height, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int dst_stride, uniform float median, uniform float sensitivity)
{
  foreach (y=0 ... height, x=0 ... width)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x])) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;
      unsigned int idx = y * dst_stride + x;

      //alternative coherent if
      cif (!nan)
	    {
	      float pixel = 255.0f / ( 1.0f + exp(-6.0f * (tmp - median) * sensitivity) ) ;                
        dst_luma[idx] = (int) clamp(pixel, 0.0f, 255.0f) ;
	      dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;         
      }
    } ;  
} ;

export void make_video_frameF32_ratio(uniform int32 src[], uniform int width, uniform int height, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int dst_stride, uniform float black, uniform float sensitivity)
{
  foreach (y=0 ... height, x=0 ... width)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x])) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;
      unsigned int idx = y * dst_stride + x;

      //alternative coherent if
      cif (!nan)
	    {
	      float pixel = 5.0f * (tmp - black) * sensitivity ;
        dst_luma[idx] = pixel > 0.0f ? (int) clamp( 255.0f*pixel/(1.0f + pixel), 0.0f, 255.0f) : 0 ;
	      dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;         
      }
    } ;  
} ;

export void make_video_frameF32_square(uniform int32 src[], uniform int width, uniform int height, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int dst_stride, uniform float black, uniform float sensitivity)
{
  foreach (y=0 ... height, x=0 ... width)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x])) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;
      unsigned int idx = y * dst_stride + x;

      //alternative coherent if
      cif (!nan)
	    {
	      float pixel = 5.0f * (tmp - black) * sensitivity ;
        dst_luma[idx] = pixel > 0.0f ? (int) clamp( 255.0f*pixel/(1.0f + pixel), 0.0f, 255.0f) : 0 ;
	      dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;         
      }
    } ;  
} ;

export void make_video_frameF32_legacy(uniform int32 src[], uniform int width, uniform int height, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int dst_stride, uniform float dmin, uniform float dmax, uniform float lmin, uniform float lmax)
{
  foreach (y=0 ... height, x=0 ... width)
    {
      float tmp = bzero + bscale * floatbits(bswap32(src[y*width+x])) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp <= ignrval) || (tmp < datamin) || (tmp > datamax) ;
      unsigned int idx = y * dst_stride + x;

      //alternative coherent if
      cif (!nan)
	    {
	      float pixel = 0.5f + (tmp - dmin) / (dmax - dmin) ;
        dst_luma[idx] = pixel > 0.0f ? (int) clamp( 255.0f * (log(pixel) - lmin) / (lmax - lmin), 0.0f, 255.0f) : 0 ;// 16 - 235 : 16
	      dst_mask[idx] = 255 ;// 240
	    }
      else
      {
        dst_luma[idx] = 0 ;// 16
	      dst_mask[idx] = 0 ;// 16    
      }
    } ;  
} ;

export void make_video_frameF16_linear(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride, uniform float black, uniform float slope)
{
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x);

      //alternative coherent if
      cif (!nan)
	    {        
        float pixel = 255.0f / ( 1.0f + exp(-6.0f * (tmp - black) * slope) ) ;             
        dst_luma[idx] = (int) clamp(pixel, 0.0f, 255.0f) ;
        dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;
      }
    } ;  
} ;

export void make_video_frameF16_logistic(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride, uniform float median, uniform float sensitivity)
{
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x);

      //alternative coherent if
      cif (!nan)
	    {
        float pixel = 255.0f / ( 1.0f + exp(-6.0f * (tmp - median) * sensitivity) ) ;                
        dst_luma[idx] = (int) clamp(pixel, 0.0f, 255.0f) ;
        dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;
      }
    } ;  
} ;

export void make_video_frameF16_ratio(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride, uniform float black, uniform float sensitivity)
{
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x);

      //alternative coherent if
      cif (!nan)
	    {
        float pixel = 5.0f * (tmp - black) * sensitivity ;
        dst_luma[idx] = pixel > 0.0f ? (int) clamp( 255.0f*pixel/(1.0f + pixel), 0.0f, 255.0f) : 0 ;
        dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;
      }
    } ;  
} ;

export void make_video_frameF16_square(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride, uniform float black, uniform float sensitivity)
{
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x);

      //alternative coherent if
      cif (!nan)
	    {
        float pixel = 5.0f * (tmp - black) * sensitivity ;
        dst_luma[idx] = pixel > 0.0f ? (int) clamp( 255.0f*pixel/(1.0f + pixel), 0.0f, 255.0f) : 0 ;
        dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;
      }
    } ;  
} ;

export void make_video_frameF16_legacy(uniform unsigned int16 src[], uniform int dx, uniform int dy, uniform int src_stride, uniform float frame_min, uniform float frame_max, uniform float min16, uniform float max16, uniform float bzero, uniform float bscale, uniform float ignrval, uniform float datamin, uniform float datamax, uniform unsigned int8 dst_luma[], uniform unsigned int8 dst_mask[], uniform int offset_x, uniform int offset_y, uniform int dst_stride, uniform float dmin, uniform float dmax, uniform float lmin, uniform float lmax)
{
  uniform float bzero16 = frame_min - min16 * (frame_max - frame_min) / (max16 - min16) ;
  uniform float bscale16 = (frame_max - frame_min) / (max16 - min16) ;
  uniform float bzero_new = bzero + bscale * bzero16 ;
  uniform float bscale_new = bscale * bscale16;

  foreach (y=0 ... dy, x=0 ... dx)
    {
      //float tmp = bzero_new + bscale_new * half_to_float_fast(src[y*src_stride+x]) ;
      float tmp = bzero + bscale * half_to_float_fast(src[y*src_stride+x]) ;
      bool nan = isnan(tmp) || isinf(tmp) || (tmp < datamin) || (tmp > datamax) || (tmp <= ignrval);
      unsigned int idx = (offset_y +  y) * dst_stride + (offset_x +  x);

      //alternative coherent if
      cif (!nan)
	    {       
        float pixel = 0.5f + (tmp - dmin) / (dmax - dmin) ;
        dst_luma[idx] = pixel > 0.0f ? (int) clamp( 255.0f * (log(pixel) - lmin) / (lmax - lmin), 0.0f, 255.0f) : 0 ;
        dst_mask[idx] = 255 ;
	    }
      else
      {
        dst_luma[idx] = 0 ;
	      dst_mask[idx] = 0 ;
      }
    } ;  
} ;

