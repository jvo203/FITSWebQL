// an attribute will receive data from a buffer
attribute vec4 a_position;
//attribute vec2 a_texcoord;   
//uniform mat4 u_matrix;

varying vec2 v_texcoord;
     
void main() {
     gl_Position = a_position;     
     v_texcoord = 0.5 * a_position.xy + vec2(0.5, 0.5); // transform [-1, 1] to []
}