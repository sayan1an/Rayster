#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV vec4 hitValue;

void main()
{
    hitValue = vec4(0.0, 0.0, 0.2, 1.0);
}