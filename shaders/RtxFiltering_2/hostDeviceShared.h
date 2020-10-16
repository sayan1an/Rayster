#ifdef GL_core_profile
#define DTYPE_VEC2 vec2
#else
#pragma once
#define DTYPE_VEC2 glm::vec2
#endif

#define SHADOW_MAP_SUBSAMPLE 4
#define COLLECT_MARKOV_CHAIN_SAMPLES 1
#if COLLECT_MARKOV_CHAIN_SAMPLES
#define SAVE_SAMPLES_TO_DISK 1
#endif
#define MAX_MARKOV_CHAIN_SAMPLES 1024

#define DEFAULT_MC_SAMPLE_MEAN 0.5f
#define DEFAULT_MC_SAMPLE_VAR 0.25f

struct McSampleInfo 
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
    McSampleInfo sampleInfo; \
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
    

#else
static_assert(sizeof(McSampleInfo) % 16 == 0, "The size in bytes of a buffer must be multiple of 16 bytes.");
#endif

