#version 450

layout (binding = 0) uniform sampler2DArray inSampler1; // rtx comp pass
layout (binding = 1) uniform sampler2D inSampler2; // stencil 1
layout (binding = 2) uniform sampler2D inSampler3; // stencil 2
layout (binding = 3) uniform sampler2D inSampler4; // stencil 3
layout (binding = 4) uniform sampler2DArray inSampler5; // mc state


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
		uint b = between(texture(inSampler2, texCoord).a, 0.5, 1.5);
		uint g = between(texture(inSampler3, texCoord).a, 0.5, 1.5);
		uint r = between(texture(inSampler4, texCoord).a, 0.5, 1.5);

		outColor = vec4(r, g, b, 1);
	}
	else if (pcb.choice.x == 1) // raw rtx out
		outColor = vec4(texture(inSampler1, vec3(texCoord, 0)).rgba);
	else if (pcb.choice.x == 2) // rtx out (no-tex)
		outColor = vec4(texture(inSampler1, vec3(texCoord, 1)).rgba);
	else if (pcb.choice.x == 3) {
		vec2 val = texture(inSampler5, vec3(texCoord, 0)).rg;
		outColor = vec4(val.x,val.y,val.y,1);
	}

}