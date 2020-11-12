#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../hostDeviceShared.h"

layout(binding = 0) uniform UniformBufferObject {
    VIEWPROJ_BLOCK
} ubo;

layout(binding = 1) readonly buffer Material {
    uvec4 textureIds[];
} materials;

// per vertex
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

// per instance static
layout(location = 4) in uvec4 inData;

// per instance dynamic
layout(location = 5) in mat4 modelTransform;
layout(location = 9) in mat4 modelTransformIT;
layout(location = 13) in mat4 modelTransformPrev;

// per vertex
layout(location = 17) in uint materialIdx;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out uvec4 fragData;
layout(location = 4) out vec4 worldPos;
layout(location = 5) out vec4 worldPosPrev;

void main() 
{   
    worldPos = modelTransform * vec4(inPosition, 1.0);
    worldPosPrev = modelTransformPrev * vec4(inPosition, 1.0);
    gl_Position = ubo.projView * worldPos;
    fragColor = inColor;
    fragNormal = normalize((modelTransformIT * vec4(inNormal, 0)).xyz);
    fragTexCoord = inTexCoord;
    uint materialIndex = inData.x == 0xffffffff ? materialIdx : inData.x;
    fragData = materials.textureIds[materialIndex];
}