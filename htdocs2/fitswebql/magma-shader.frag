    // magma
    gl_FragColor = colormap_magma(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}