    // negative
    pixel = 1.0 - pixel;
    
    colour.r = pixel;
    colour.g = pixel;
    colour.b = pixel;

    gl_FragColor = colour;
}