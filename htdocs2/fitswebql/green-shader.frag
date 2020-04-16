    // green
    gl_FragColor = colormap_green_white_linear(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}
