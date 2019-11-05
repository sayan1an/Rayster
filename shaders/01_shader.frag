#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2DArray texSampler;
layout(binding = 2) uniform sampler2DArray texSamplerHdr;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSamplerHdr, fragTexCoord); //vec4(fragColor, 1.0); //
}