    // jet
    gl_FragColor = colormap_jet(pixel, colour.a) ;
    gl_FragColor.rgb *= gl_FragColor.a;
}