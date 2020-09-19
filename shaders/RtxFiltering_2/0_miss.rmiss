#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV vec3 radiance;

void main()
{
    radiance = vec3(0, 0, 0);
}