precision mediump float;
     
varying vec2 v_texcoord;
uniform sampler2D u_texture;

uniform float median;
uniform float sensitivity;
uniform float white;
uniform float black;

void main() {
     vec4 colour = texture2D(u_texture, v_texcoord);// the raw floating-point colour
     float x = (colour.r + colour.g + colour.b) / 3.0;
     float pixel = 5.0 * (x - black) * sensitivity;

     if (pixel > 0.0)
          pixel = pixel / (1.0 + pixel) ;
     else
          pixel = 0.0;

     // greyscale for now
     colour.r = pixel;
     colour.g = pixel;
     colour.b = pixel;

     gl_FragColor = colour;
}