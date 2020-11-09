#ifdef GL_core_profile
float offsetCalc(in uint x, in uint level)
{	
	uint den = (1 << level); // den = 1,2,4 when level = 0,1,2
	return (x == 0 ? 0.5f  / den : 1.0f - 0.5f / den); 
}
#else
#pragma once
#endif

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

#define MAX_SPP 8
#define CUTOFF_WEIGHT 0.001

#define MAX_SAMPLE_CLIP_VALUE 50000.0