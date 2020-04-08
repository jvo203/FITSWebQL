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

//IDL green, red, purple
      
float colormap_red(float x) {
    return 1.61361058036781E+00 * x - 1.55391688559828E+02;
}

float colormap_green(float x) {
    return 9.99817607003891E-01 * x + 1.01544260700389E+00;
}

float colormap_blue(float x) {
    return 3.44167852062589E+00 * x - 6.19885917496444E+02;
}

vec4 colormap_red_white_linear(float x, float alpha) {
    float t = x * 255.0;
    float r = clamp(colormap_red(t) / 255.0, 0.0, 1.0);
    float g = clamp(colormap_green(t) / 255.0, 0.0, 1.0);
    float b = clamp(colormap_blue(t) / 255.0, 0.0, 1.0);
    return vec4(g, r, b, alpha);
}

vec4 colormap_green_white_linear(float x, float alpha) {
    float t = x * 255.0;
    float r = clamp(colormap_red(t) / 255.0, 0.0, 1.0);
    float g = clamp(colormap_green(t) / 255.0, 0.0, 1.0);
    float b = clamp(colormap_blue(t) / 255.0, 0.0, 1.0);
    return vec4(r, g, b, alpha);
}

//IDL blue-white-linear

float colormap_red2(float x) {
    if (x < 1.0 / 3.0) {
        return 4.0 * x - 2.992156863;
    } else if (x < 2.0 / 3.0) {
        return 4.0 * x - 2.9882352941;
    } else if (x < 2.9843137255 / 3.0) {
        return 4.0 * x - 2.9843137255;
    } else {
        return x;
    }
}

float colormap_green2(float x) {
    return 1.602642681354730 * x - 5.948580022657070e-1;
}

float colormap_blue2(float x) {
    return 1.356416928785610 * x + 3.345982835050930e-3;
}

vec4 colormap_blue_white_linear(float x, float alpha) {
    float r = clamp(colormap_red2(x), 0.0, 1.0);
    float g = clamp(colormap_green2(x), 0.0, 1.0);
    float b = clamp(colormap_blue2(x), 0.0, 1.0);
    return vec4(r, g, b, alpha);
}

void main() {
     vec4 colour = texture2D(u_texture, v_texcoord);// the raw floating-point colour
     float x = (colour.r + colour.g + colour.b) / 3.0;