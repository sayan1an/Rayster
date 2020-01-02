#version 460
#extension GL_NV_ray_tracing : require

layout(location = 1) rayPayloadInNV vec4 radiance;

void main()
{
    radiance = vec4(0, 0, 0, 1);
}