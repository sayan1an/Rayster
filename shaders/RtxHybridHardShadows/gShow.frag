#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput inDiffuseColor;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput inSpecularColor;
layout (input_attachment_index = 2, binding = 2) uniform subpassInput inNormal;
layout (input_attachment_index = 3, binding = 3) uniform subpassInput inDepthMatInfo;
layout (input_attachment_index = 4, binding = 4) uniform subpassInput inDepth;

layout (push_constant) uniform pcBlock {
	uint select;
	float scale;
} pcb;

layout (location = 0) out vec4 outColor;

void main() 
{	
	if (pcb.select == 0)
		outColor = subpassLoad(inDiffuseColor).rgba;
	else if (pcb.select == 1)
		outColor = subpassLoad(inSpecularColor).rgba;
	else if (pcb.select == 2)
		outColor = vec4(subpassLoad(inNormal).rgb, 1.0f);
	else if (pcb.select == 3)
		outColor = vec4(vec3(subpassLoad(inDepthMatInfo).r * pcb.scale), 1.0f);
	else if (pcb.select == 4)
		outColor = vec4(vec3(subpassLoad(inDepth).r * pcb.scale), 1.0f);
	else if (pcb.select == 5)
		outColor = vec4(vec3(subpassLoad(inDepthMatInfo).g * pcb.scale), 1.0f); // Int IOR
	else if (pcb.select == 6)
		outColor = vec4(vec3(subpassLoad(inDepthMatInfo).b * pcb.scale), 1.0f); // Ext IOR
	else if (pcb.select == 7)
		outColor = vec4(vec3(subpassLoad(inNormal).a * pcb.scale), 1.0f); // Specular alpha
	else if (pcb.select == 8)
		outColor = vec4(vec3(subpassLoad(inDepthMatInfo).a * pcb.scale), 1.0f); // Material type
	else
		outColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);	
}