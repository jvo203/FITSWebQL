    // parula
    gl_FragColor = colormap_parula(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}