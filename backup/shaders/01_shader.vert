#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// per vertex
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

// per instance static
layout(location = 3) in vec3 inTranslate;
// per instance dynamic
//layout(location = 4) in mat4 modelTransform;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragTexCoord;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition + inTranslate, 1.0);
    fragColor = inColor;
    fragTexCoord = vec3(inTexCoord, 0);
}