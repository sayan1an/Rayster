#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV uint hit;

void main()
{
    hit = 1;
}