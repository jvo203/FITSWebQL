// an attribute will receive data from a buffer
attribute vec4 a_position;
attribute vec2 a_texcoord;
     
/*uniform mat4 u_matrix;
uniform mat4 u_textureMat;*/

varying vec2 v_texcoord;
     
void main() {
     gl_Position = a_position;
     //gl_Position = u_matrix * a_position;
     //v_texcoord = (u_textureMat * vec4(a_texcoord, 0, 1)).xy;
     //v_texcoord = a_texcoord;
}