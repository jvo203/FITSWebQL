     float pixel = 0.5 + (x - pmin) / (pmax - pmin);

     if (pixel > 0.0)
          clamp((log(pixel) - lmin) / (lmax - lmin), 0.0, 1.0);
     else
          pixel = 0.0;

     // to be glued together with a separate colourmap shader