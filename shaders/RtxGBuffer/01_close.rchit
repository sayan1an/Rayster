#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInNV vec4 hitValue;
hitAttributeNV vec3 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;
layout(binding = 3, set = 0) buffer Vertices { vec4 v[]; } vertices;
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices;
layout(binding = 5, set = 0) buffer StaticInstances { uvec4 i[]; } staticInstances;
layout(binding = 6, set = 0) uniform sampler2DArray ldrTexSampler;
layout(binding = 7, set = 0) uniform sampler2DArray hdrTexSampler;

struct Vertex 
{
	vec3 pos;
	vec3 color;
	vec3 normal;
	vec2 texCoord;
	float dummy;
};

// Number of vec4 values used to represent a vertex i.e. vertexSize * sizeof(vec4) == sizeof(Vertex)
uint vertexSize = 3;

Vertex unpackVertex(uint index)
{
  Vertex v;

  vec4 d0 = vertices.v[vertexSize * index + 0];
  vec4 d1 = vertices.v[vertexSize * index + 1];
  vec4 d2 = vertices.v[vertexSize * index + 2];

  v.pos = d0.xyz;
  v.color = vec3(d0.w, d1.x, d1.y);
  v.normal = vec3(d1.z, d1.w, d2.x);
  v.texCoord = vec2(d2.y, d2.z);
  v.dummy = d2.w;

  return v;
}

void main()
{ 
  ivec3 ind = ivec3(indices.i[3 * gl_PrimitiveID], indices.i[3 * gl_PrimitiveID + 1],
                    indices.i[3 * gl_PrimitiveID + 2]);
  
  uvec4 staticInstanceData = staticInstances.i[gl_InstanceID];
  
  const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

  Vertex v0 = unpackVertex(ind.x);
  Vertex v1 = unpackVertex(ind.y);
  Vertex v2 = unpackVertex(ind.z);

  vec3 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;
  vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
  
  hitValue = texture(ldrTexSampler, vec3(texCoord, staticInstanceData.y)) * vec4(color, 1.0f); // diffuse texture
}
