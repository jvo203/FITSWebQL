    // red
    gl_FragColor = colormap_red_white_linear(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}