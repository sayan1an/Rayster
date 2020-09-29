#version 450

layout (binding = 0) uniform sampler2D inSampler1;
layout (binding = 1) uniform sampler2D inSampler2;
layout (binding = 2) uniform sampler2D inSampler3;
layout (binding = 3) uniform sampler2D inSampler4;
layout (binding = 4) uniform sampler2D inSampler5;
layout (binding = 5) uniform sampler2D inSampler6;
layout (binding = 6) uniform sampler2D inSampler7;
layout (binding = 7) uniform sampler2D inSampler8;

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
	
	if (pcb.choice.x == 0) { // stencil
		uint b = between(texture(inSampler5, texCoord).a, 0.5, 1.5);
		uint g = between(texture(inSampler6, texCoord).a, 0.5, 1.5);
		uint r = between(texture(inSampler7, texCoord).a, 0.5, 1.5);

		outColor = vec4(r, g, b, 1);
	}
	else if (pcb.choice.x == 1) // raw rtx out
		outColor = vec4(texture(inSampler1, texCoord).rgba);
	else if (pcb.choice.x == 2) // temporal filter
		outColor = vec4(texture(inSampler2, texCoord).rgba);
	else if (pcb.choice.x == 3) // shadow map
		outColor = vec4(texture(inSampler3, texCoord).r);
	else if (pcb.choice.x == 4) { // shadow map blurred
		float val = texture(inSampler4, texCoord).r;
		uint r = between(val, -0.0002, 0.0002);
		uint g = between(val, 1 - 0.0002, 1 + 0.0002);
		uint b = between(val, 0.0002, 1 - 0.0002);

		outColor = vec4(r, g, b, 1);
		//float rayTraceProb = exp(-(val - 0.5) * (val - 0.5)/ 0.125);
		//outColor = vec4(vec3(), 1);
	}
	else if (pcb.choice.x == 5) {
		float val = texture(inSampler8, texCoord).r;
		uint r = between(val, 0.5, 1.5);
		uint g = between(val, 1.5, 2.5);
		uint b = between(val, 2.5, 3.5);

		outColor = vec4(r,g,b,1);
	}

}