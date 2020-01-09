#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput inColorNoisy;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput inColorDenoised;
layout (location = 0) out vec4 outColor;

layout (push_constant) uniform pcBlock {
	int denoise;
} pcb;

void main()
{	
	outColor = pcb.denoise == 0 ? subpassLoad(inColorNoisy).rgba : subpassLoad(inColorDenoised).rgba;
}