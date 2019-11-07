#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput inDiffuseColor;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput inSpecularColor;
layout (input_attachment_index = 2, binding = 2) uniform subpassInput inNormal;
layout (input_attachment_index = 3, binding = 3) uniform subpassInput inDepthMatInfo;
layout (input_attachment_index = 4, binding = 4) uniform subpassInput inDepth;

layout (location = 0) out vec4 outColor;

void main() 
{
	// Apply brightness and contrast filer to color input
	if (true) {
		// Read color from previous color input attachment
		outColor.rgb = subpassLoad(inDiffuseColor).rgb;
	}

	// Visualize depth input range
	else {
		// Read depth from previous depth input attachment
		float depth = subpassLoad(inDepth).r;
		outColor.rgb = vec3(depth);
	}
}