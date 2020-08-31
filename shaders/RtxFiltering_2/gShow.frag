#version 450

layout (binding = 0) uniform sampler2D inSampler1;
layout (binding = 1) uniform sampler2D inSampler2;

layout (location = 0) out vec4 outColor;

layout (push_constant) uniform pcBlock {
	ivec2 viewportSize;
} pcb;

void main()
{	
	vec2 pixel = gl_FragCoord.xy / vec2(pcb.viewportSize);
	//outColor = pcb.denoise == 0 ? subpassLoad(inColorNoisy).rgba : subpassLoad(inColorDenoised).rgba;
	outColor = vec4(texture(inSampler2, pixel).g < 0.02 ? 1 : 0, 0, 0, 1);
}