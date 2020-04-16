    // red
    gl_FragColor = colormap_blue_white_linear(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}