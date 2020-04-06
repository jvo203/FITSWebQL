// an attribute will receive data from a buffer
attribute vec4 a_position;

uniform float xmin;
uniform float xmax;
uniform float ymin;
uniform float ymax;

varying vec2 v_texcoord;
     
void main() {
     gl_Position = a_position;
     vec2 tex_space = 0.5 * a_position.xy + vec2(0.5, 0.5); // transform [-1, 1] to [0, 1]
     v_texcoord = tex_space;
}