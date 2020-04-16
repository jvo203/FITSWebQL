    // red
    gl_FragColor = colormap_cubehelix(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}