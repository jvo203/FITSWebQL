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
     float pixel = 1.0 / (1.0 + exp(-6.0 * (x - median) * sensitivity));

     // to be glued together with a separate colourmap shader