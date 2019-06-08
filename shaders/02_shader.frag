#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput inputColor;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput inputDepth;

layout (location = 0) out vec4 outColor;

void main() 
{
	// Apply brightness and contrast filer to color input
	if (false) {
		// Read color from previous color input attachment
		outColor.rgb = subpassLoad(inputColor).rgb;
	}

	// Visualize depth input range
	else {
		// Read depth from previous depth input attachment
		float depth = subpassLoad(inputDepth).r;
		outColor.rgb = vec3(depth);
	}
}