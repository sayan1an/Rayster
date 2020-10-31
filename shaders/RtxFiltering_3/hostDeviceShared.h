#ifdef GL_core_profile
#define DTYPE_VEC2 vec2
#else
#pragma once
#define DTYPE_VEC2 glm::vec2
#endif

#define SAMPLE_MEAN_BITS 15
#define SAMPLE_STD_BITS 13
#define GH_ORDER_BITS 4

#define COLLECT_MARKOV_CHAIN_SAMPLES 1
#if COLLECT_MARKOV_CHAIN_SAMPLES
#define SAVE_SAMPLES_TO_DISK 1
#define MC_SAMPLE_HEADER_SIZE 3
#endif

#define MAX_MARKOV_CHAIN_SAMPLES 1024

#define COLLECT_RT_SAMPLES 1
#if COLLECT_RT_SAMPLES
#define RT_SAMPLE_HEADER_SIZE 3
#endif
#define MAX_RT_SAMPLES 1024

#define DEFAULT_MC_SAMPLE_MEAN 0.5f
#define DEFAULT_MC_SAMPLE_VAR 0.25f

struct McSampleInfo2 
{
    DTYPE_VEC2 mean;
    DTYPE_VEC2 var; // var_x, var_y
    DTYPE_VEC2 covWeight; // var_xy, moving sample weight
    DTYPE_VEC2 maxVal; // maximum value of mc sample, average value of mcmc samples
};

#ifdef GL_core_profile
#define SAMPLE_INFO_BUFFER_NAME { vec4 data[]; } mcSampleRepresentation

// Number of vec4 required to pack McSampleInfo struct
#define MC_SAMPLE_INFO_UNPACK_STRIDE 2

#define UNPACK_MC_SAMPLE_INFO \
    McSampleInfo2 sampleInfo; \
    vec4 v1 = mcSampleRepresentation.data[index * MC_SAMPLE_INFO_UNPACK_STRIDE]; \
    vec4 v2 = mcSampleRepresentation.data[index * MC_SAMPLE_INFO_UNPACK_STRIDE + 1]; \
    sampleInfo.mean = v1.xy; \
    sampleInfo.var = v1.zw; \
    sampleInfo.covWeight = v2.xy; \
    sampleInfo.maxVal = v2.zw; \
    return sampleInfo;

#define REPACK_MC_SAMPLE_INFO \
    vec4 v1, v2; \
    v1.xy = sampleInfo.mean; \
    v1.zw = sampleInfo.var; \
    v2.xy = sampleInfo.covWeight; \
    v2.zw = sampleInfo.maxVal; \
    mcSampleRepresentation.data[index * MC_SAMPLE_INFO_UNPACK_STRIDE] = v1; \
    mcSampleRepresentation.data[index * MC_SAMPLE_INFO_UNPACK_STRIDE + 1] = v2;

#define SAMPLE_MEAN_MAX_UINT ((1 << SAMPLE_MEAN_BITS) - 1)
#define SAMPLE_STD_MAX_UINT ((1 << SAMPLE_STD_BITS) - 1)
#define GH_ORDER_MAX_UINT ((1 << GH_ORDER_BITS) - 1)

float offsetCalc(in uint x, in uint level)
{	
	uint den = (1 << level); // den = 1,2,4 when level = 0,1,2
	return (x == 0 ? 0.5f  / den : 1.0f - 0.5f / den); 
}
/*
uint packComponent(in float m, in float v, in uint o)
{
	o &= GH_ORDER_MAX_UINT;
	o <<= (32 - GH_ORDER_BITS);

	uint t = uint(v * SAMPLE_STD_MAX_UINT);
	t &= SAMPLE_STD_MAX_UINT;
	t <<= (32 - SAMPLE_STD_BITS - GH_ORDER_BITS);
	o |= t;

	t = uint(m * SAMPLE_MEAN_MAX_UINT);
	t &= SAMPLE_MEAN_MAX_UINT;
	o |= t;

	return o;
}*/

uvec2 packSampleStat(in vec2 m, in vec2 v, in uvec2 o)
{
	o &= GH_ORDER_MAX_UINT;
	o <<= (32 - GH_ORDER_BITS);

	uvec2 t = uvec2(round(v * SAMPLE_STD_MAX_UINT));
	t &= SAMPLE_STD_MAX_UINT;
	t <<= (32 - SAMPLE_STD_BITS - GH_ORDER_BITS);
	o |= t;

	t = uvec2(round(m * SAMPLE_MEAN_MAX_UINT));
	t &= SAMPLE_MEAN_MAX_UINT;
	o |= t;

	return o;
}

void unpackSampleStat(in uvec2 c, out vec2 m, out vec2 v, out uvec2 o) 
{
	m = vec2(c & SAMPLE_MEAN_MAX_UINT) / SAMPLE_MEAN_MAX_UINT;
	v = vec2((c >> SAMPLE_MEAN_BITS) & SAMPLE_STD_MAX_UINT) / SAMPLE_STD_MAX_UINT;
	o = (c >> (SAMPLE_STD_BITS + SAMPLE_MEAN_BITS));
}

/*
uvec2 packSampleStat(in vec2 mean, in vec2 var, in uvec2 order)
{	
	//return uvec2(packComponent(mean.x, var.x, order_x), packComponent(mean.y, var.y, order_y));
}
*/
/*
void unpackSampleStat(in uvec2 d, out vec2 mean, out vec2 var, out uint order_x, out uint order_y)
{
	unpackComponent(d.x, mean.x, var.x, order_x);
	unpackComponent(d.y, mean.y, var.y, order_y);
}*/
   

#else
static_assert(sizeof(McSampleInfo2) % 16 == 0, "The size in bytes of a buffer must be multiple of 16 bytes.");
#endif

