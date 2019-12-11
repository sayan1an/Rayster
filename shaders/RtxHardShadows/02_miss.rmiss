#version 460
#extension GL_NV_ray_tracing : require

layout(location = 1) rayPayloadInNV Payload {
    uint hit;
} payload;

void main()
{
    payload.hit = 1;
}