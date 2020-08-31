#version 450

layout (binding = 0) uniform sampler2D inSampler1;
layout (binding = 1) uniform sampler2D inSampler2;
layout (binding = 2) uniform sampler2D inSampler3;

layout (location = 0) out vec4 outColor;

layout (push_constant) uniform pcBlock {
	ivec2 viewportSize;
} pcb;

uint between(in float val, in float min, in float max)
{
	return val > min && val < max ? 1 : 0;
}

void main()
{	
	vec2 pixel = gl_FragCoord.xy / vec2(pcb.viewportSize);
	
	uint b = between(texture(inSampler1, pixel).a, 0.5, 1.5);
	uint g = between(texture(inSampler2, pixel).a, 0.5, 1.5);
	uint r = between(texture(inSampler3, pixel).a, 0.5, 1.5);

	outColor = vec4(r, g, b, 1);
}