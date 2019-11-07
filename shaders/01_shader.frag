#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2DArray ldrTexSampler;
layout(binding = 2) uniform sampler2DArray hdrTexSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) flat in uvec4 fragData; // there is no interpolation for flat type

layout(location = 0) out vec4 outDiffuseColor;
layout(location = 1) out vec4 outSpecularColor;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outDepthMatInfo;

void main() 
{
    uint diffuseTextureIdx = fragData.x;
    uint specularTextureIdx = fragData.y;
    uint alphaIorIdx = fragData.z;
    uint bsdfType = fragData.w;
    outDiffuseColor = texture(ldrTexSampler, vec3(fragTexCoord, diffuseTextureIdx)) * vec4(fragColor, 1.0f);
    outSpecularColor = texture(ldrTexSampler, vec3(fragTexCoord, specularTextureIdx));
    vec4 alphaIntExtIor = texture(hdrTexSampler, vec3(fragTexCoord, alphaIorIdx));
    outNormal = vec4(fragNormal, alphaIntExtIor.x);
    outDepthMatInfo = vec4(0, alphaIntExtIor.yz, bsdfType);
}