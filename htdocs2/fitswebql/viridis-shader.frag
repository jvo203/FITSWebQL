    // viridis
    gl_FragColor = colormap_viridis(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}