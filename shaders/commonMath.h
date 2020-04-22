#define PI 3.14159265358979324

vec2 complexExp(in float x)
{
	float arg = 2 * PI * x;
	return vec2(cos(arg), sin(arg));
}

vec2 complexMul(in vec2 x, in vec2 y)
{
	return vec2(x.r * y.r - x.g * y.g, x.r * y.g + x.g * y.r);
}

float complexMulReal(in vec2 x, in vec2 y)
{
	return x.r * y.r - x.g * y.g;
}

mat3x2 complexMul(in mat3x2 x, in vec2 y)
{
	return mat3x2(x[0].r * y.r - x[0].g * y.g, x[0].r * y.g + x[0].g * y.r,
		x[1].r * y.r - x[1].g * y.g, x[1].r * y.g + x[1].g * y.r,
		x[2].r * y.r - x[2].g * y.g, x[2].r * y.g + x[2].g * y.r);
}

vec3 complexMulReal(in mat3x2 x, in vec2 y)
{
	return vec3(x[0].r * y.r - x[0].g * y.g, x[1].r * y.r - x[1].g * y.g, x[2].r * y.r - x[2].g * y.g);
}

float complexAbs(in vec2 x)
{
	return sqrt(x.r * x.r + x.g * x.g);
}
