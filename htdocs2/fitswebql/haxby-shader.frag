    // haxby
    gl_FragColor = colormap_haxby(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}