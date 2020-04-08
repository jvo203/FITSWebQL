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

vec4 colormap_hot(float x, float alpha) {
    float r = clamp(8.0 / 3.0 * x, 0.0, 1.0);
    float g = clamp(8.0 / 3.0 * x - 1.0, 0.0, 1.0);
    float b = clamp(4.0 * x - 3.0, 0.0, 1.0);
    return vec4(r, g, b, alpha);
}

vec4 colormap_hsv2rgb(float h, float s, float v, float alpha) {
	float r = v;
	float g = v;
	float b = v;
	if (s > 0.0) {
		h *= 6.0;
		int i = int(h);
		float f = h - float(i);
		if (i == 1) {
			r *= 1.0 - s * f;
			b *= 1.0 - s;
		} else if (i == 2) {
			r *= 1.0 - s;
			b *= 1.0 - s * (1.0 - f);
		} else if (i == 3) {
			r *= 1.0 - s;
			g *= 1.0 - s * f;
		} else if (i == 4) {
			r *= 1.0 - s * (1.0 - f);
			g *= 1.0 - s;
		} else if (i == 5) {
			g *= 1.0 - s;
			b *= 1.0 - s * f;
		} else {
			g *= 1.0 - s * (1.0 - f);
			b *= 1.0 - s;
		}
	}
	return vec4(r, g, b, alpha);
}

vec4 colormap_rainbow(float x, float alpha) {
	if (x < 0.0) {
		return vec4(0.0, 0.0, 0.0, alpha);
	} else if (1.0 < x) {
		return vec4(0.0, 0.0, 0.0, alpha);
	} else {
		float h = clamp(-9.42274071356572E-01 * x + 8.74326827903982E-01, 0.0, 1.0);
		float s = 1.0;
		float v = clamp(4.90125513855204E+00 * x + 9.18879034690780E-03, 0.0, 1.0);
		return colormap_hsv2rgb(h, s, v, alpha);
	}
}

void main() {
     vec4 colour = texture2D(u_texture, v_texcoord);// the raw floating-point colour
     float x = (colour.r + colour.g + colour.b) / 3.0;