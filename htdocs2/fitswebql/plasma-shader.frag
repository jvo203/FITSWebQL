    // plasma
    gl_FragColor = colormap_plasma(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}