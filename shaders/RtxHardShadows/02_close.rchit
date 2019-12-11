#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 1) rayPayloadInNV Payload {
	uint hit;
} payload;

void main()
{ 
  payload.hit = 0;
}
