#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInNV Payload {
	vec4 diffuseColor;
	vec4 specularColor;
  vec4 normal; // normla + specular alpha
  vec4 other; // depth, int ior, ext ior, material type
} payload;


hitAttributeNV vec3 attribs;

layout(binding = 6, set = 0) readonly buffer Material { uvec4 textureIdx[]; } materials;
layout(binding = 7, set = 0) readonly buffer Vertices { vec4 v[]; } vertices;
layout(binding = 8, set = 0) readonly buffer Indices { uint i[]; } indices;
layout(binding = 9, set = 0) readonly buffer StaticInstanceData { uvec4 i[]; } staticInstanceData;
layout(binding = 10, set = 0) uniform sampler2DArray ldrTexSampler;
layout(binding = 11, set = 0) uniform sampler2DArray hdrTexSampler;

struct Vertex 
{
	vec3 pos;
	vec3 color;
	vec3 normal;
	vec2 texCoord;
	uint materialIdx;
};

struct InstanceData_static 
{
	uvec4 data;
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
  v.materialIdx = floatBitsToUint(d2.w);

  return v;
}

void main()
{ 
  uvec4 staticInstanceDataUnit = staticInstanceData.i[gl_InstanceID];
  
  ivec3 ind = ivec3(indices.i[3 * gl_PrimitiveID + staticInstanceDataUnit.y], indices.i[3 * gl_PrimitiveID + staticInstanceDataUnit.y + 1],
                    indices.i[3 * gl_PrimitiveID + staticInstanceDataUnit.y + 2]);
    
  const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

  Vertex v0 = unpackVertex(ind.x);
  Vertex v1 = unpackVertex(ind.y);
  Vertex v2 = unpackVertex(ind.z);

  uint materialIdx = staticInstanceDataUnit.x == 0xffffffff ? v0.materialIdx : staticInstanceDataUnit.x;
  uvec4 textureIdxUnit = materials.textureIdx[materialIdx];

  vec3 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;
  vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
  vec3 normal = normalize(transpose(mat3(gl_WorldToObjectNV)) * (v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z));
  
  vec4 alphaIntExtIor = texture(hdrTexSampler, vec3(texCoord, textureIdxUnit.z)); // alphaIntExtIor texture
  payload.diffuseColor = texture(ldrTexSampler, vec3(texCoord, textureIdxUnit.x)) * vec4(color, 1.0f); // diffuse texture
  payload.specularColor = texture(ldrTexSampler, vec3(texCoord, textureIdxUnit.y)); // specular texture
  payload.normal = vec4(normal, alphaIntExtIor.x);
  payload.other = vec4(gl_HitTNV, alphaIntExtIor.yz, textureIdxUnit.w);
}
