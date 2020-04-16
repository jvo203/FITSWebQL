     float black = params.z;
     float white = params.w;
     
     float slope = 1.0 / (white - black);
     float pixel = clamp((x - black) * slope, 0.0, 1.0);

     // to be glued together with a separate colourmap shader