    // hot
    gl_FragColor = colormap_hot(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}