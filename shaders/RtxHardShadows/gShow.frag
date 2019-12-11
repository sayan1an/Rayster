#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput inColor;

layout (push_constant) uniform pcBlock {
	uint select;
	float scale;
} pcb;

layout (location = 0) out vec4 outColor;

void main() 
{	
	if (pcb.select == 0)
		outColor = subpassLoad(inColor).rgba;
	else
		outColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);	
}