precision mediump float;
     
varying vec2 v_texcoord;
uniform sampler2D u_texture;

uniform float pmin;
uniform float pmax;
uniform float lmin;
uniform float lmax;

uniform float median;
uniform float sensitivity;
uniform float white;
uniform float black;

void main() {
     vec4 colour = texture2D(u_texture, v_texcoord);// the raw floating-point colour
     float x = (colour.r + colour.g + colour.b) / 3.0;
     float pixel = 0.5 + (x - pmin) / (pmax - pmin);

     if (pixel > 0.0)
          (log(pixel) - lmin) / (lmax - lmin);
     else
          pixel = 0.0;

     // to be glued together with a separate colourmap shader