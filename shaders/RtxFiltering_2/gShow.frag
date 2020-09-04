#version 450

layout (binding = 0) uniform sampler2D inSampler1;
layout (binding = 1) uniform sampler2D inSampler2;
layout (binding = 2) uniform sampler2D inSampler3;
layout (binding = 3) uniform sampler2D inSampler4;

layout (location = 0) out vec4 outColor;

layout (push_constant) uniform pcBlock {
	ivec2 viewportSize;
	ivec2 choice;
} pcb;

uint between(in float val, in float min, in float max)
{
	return val > min && val < max ? 1 : 0;
}

void main()
{	
	vec2 texCoord = gl_FragCoord.xy / vec2(pcb.viewportSize);
	
	if (pcb.choice.x == 0) {
		uint b = between(texture(inSampler2, texCoord).a, 0.5, 1.5);
		uint g = between(texture(inSampler3, texCoord).a, 0.5, 1.5);
		uint r = between(texture(inSampler4, texCoord).a, 0.5, 1.5);

		outColor = vec4(r, g, b, 1);
	}

	else
		outColor = vec4(texture(inSampler1, texCoord).rgba);
}