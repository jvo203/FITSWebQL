    // rainbow
    gl_FragColor = colormap_rainbow(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}