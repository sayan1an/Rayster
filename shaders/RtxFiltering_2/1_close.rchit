#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(binding = 8, set = 0) readonly buffer LightVertices { vec4 v[]; } lightVertices;
layout(binding = 12, set = 0) readonly buffer StaticInstanceData { uvec4 i[]; } staticInstanceData;
layout(binding = 13, set = 0) readonly buffer Material { uvec4 textureIdx[]; } materials;
layout(binding = 14, set = 0) readonly buffer Vertices { vec4 v[]; } vertices;
layout(binding = 15, set = 0) readonly buffer Indices { uint i[]; } indices;
layout(binding = 16, set = 0) uniform sampler2DArray ldrTexSampler;
layout(binding = 17, set = 0) readonly buffer LightInstanceToGlobalInstance { uint i[]; } lightInstanceToGlobalInstance;

layout(location = 0) rayPayloadInNV vec3 radiance;
hitAttributeNV vec3 attribs;

struct Vertex 
{
	vec3 pos;
	vec3 color;
	vec3 normal;
	vec2 texCoord;
	uint materialIdx;
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
   vec3 lightDir = radiance;

   radiance = vec3(0, 0, 0);

   // check whether the primitive is an emiiter (generic) source
   if (staticInstanceDataUnit.z > 0) {
      ivec3 ind = ivec3(indices.i[3 * gl_PrimitiveID + staticInstanceDataUnit.y], indices.i[3 * gl_PrimitiveID + staticInstanceDataUnit.y + 1],
                    indices.i[3 * gl_PrimitiveID + staticInstanceDataUnit.y + 2]);
       
      Vertex v0 = unpackVertex(ind.x);
      uint materialIdx = staticInstanceDataUnit.x == 0xffffffff ? v0.materialIdx : staticInstanceDataUnit.x;
      uvec4 textureIdxUnit = materials.textureIdx[materialIdx];

      // check whether the primitive is an area-emiiter
      if (textureIdxUnit.w == 4) {
         const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
         
         Vertex v1 = unpackVertex(ind.y);
         Vertex v2 = unpackVertex(ind.z);
         
         vec3 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;
         vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
         uint primitiveIndex = 3 * gl_PrimitiveID + (staticInstanceDataUnit.z >> 8);
         // shNormal
         //vec3 normal = normalize(transpose(mat3(gl_WorldToObjectNV)) * (v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z));
         // geoNormal        
         vec3 normal = vec3(lightVertices.v[primitiveIndex].w, lightVertices.v[primitiveIndex + 1].w, lightVertices.v[primitiveIndex + 2].w);

         float area = length(normal);
         normal /= area;
         //area *= 0.5f;
        
         radiance = texture(ldrTexSampler, vec3(texCoord, textureIdxUnit.x)).xyz * color * (staticInstanceDataUnit.z & 0xff) * abs(dot(lightDir, normal)); // diffuse texture
      }
   }
}
