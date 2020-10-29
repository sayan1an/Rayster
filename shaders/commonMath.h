#define PI 3.14159265358979324
#define SQRT_2 1.41421356237
#define CLIP(v, min, max) ((v) < (min)) ? (min) : (((v) > (max)) ? (max) : (v)) 

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

uint xorshift(inout uint xorshiftState)
{
	xorshiftState ^= (xorshiftState << 13);
    xorshiftState ^= (xorshiftState >> 17);
    xorshiftState ^= (xorshiftState << 5);

	return xorshiftState;
}

struct XorwowState {
  uint a, b, c, d;
  uint counter;
};

uint xorwow(inout XorwowState xorwowState)
{
	uint t = xorwowState.d;

	uint s = xorwowState.a;
	xorwowState.d = xorwowState.c;
	xorwowState.c = xorwowState.b;
	xorwowState.b = s;

	t ^= (t >> 2);
	t ^= (t << 1);
	t ^= (s ^ (s << 4));
	xorwowState.a = t;

	xorwowState.counter += 362437;
	return t + xorwowState.counter;
}

uint wangHash(in uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float ggxDist(const vec3 h, const vec3 n, float alpha) 
{
    float cosine_sq = dot(h, n);
	cosine_sq *= cosine_sq;
    float alpha_sq = alpha * alpha;

	float beckmannExp = (1.0f - cosine_sq) / (alpha_sq * cosine_sq);
	float root = (1.0f + beckmannExp) * cosine_sq;
	
	return 1.0f / (PI * alpha_sq * root * root);
}

float ggxG1(const vec3 v, const vec3 h, const vec3 n, float alpha) 
{
    float cosv_sq = dot(v, n);
    cosv_sq *= cosv_sq;
    float tanv_sq = 1.0f / cosv_sq - 1.0f;

    if (tanv_sq <= 1e-15)
        return 1.0f;
    else if (dot(h, v) <= 1e-15)
        return 0.0f;

    float alpha_sq = alpha * alpha;

    tanv_sq *= alpha_sq;
    tanv_sq += 1.0f;
    tanv_sq = 1.0f + sqrt(tanv_sq);

    return 2.0f/tanv_sq;
}

float ggxBrdf(const vec3 wo, const vec3 wi, const vec3 n, float alpha) 
{	
	vec3 h = normalize(wo + wi);
    
    float D = ggxDist(h, n, alpha);
    float G = 1.0f;//ggxG1(wo, h, n, alpha) * ggxG1(wi, h, n, alpha);

	return (D * G) / (4.0f * dot(wo, n));
}

vec4 boundingSphereTri(in vec3 a, in vec3 b, in vec3 c)
{
	vec4 ret;

	ret.xyz = (a + b + c) / 3.0;
	vec3 da = ret.xyz - a;
	vec3 db = ret.xyz - b;
	vec3 dc = ret.xyz - c;

	ret.w = sqrt(max(max(dot(da, da), dot(db, db)), dot(dc, dc)));

	return ret;
}

vec2 uniformToGaussian(in vec2 u)
{	
	return vec2(sqrt(-2*log(u.x))*cos(2*PI*u.y), sqrt(-2*log(u.x))*sin(2*PI*u.y));
}