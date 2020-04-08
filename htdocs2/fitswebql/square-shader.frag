     float pixel = (x - black) * sensitivity;

     if (pixel > 0.0)
          pixel = clamp(pixel * pixel, 0.0, 1.0);
     else
          pixel = 0.0;

     // to be glued together with a separate colourmap shader