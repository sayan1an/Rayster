#pragma once

#include <algorithm>
#include <vector>
#include <array>

#include "vulkan/vulkan.h"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "helper.h"
#include "accelerationStructure.h"
#include "generator.h"

/*
 * Mesh organisation philosphy - Think of each mesh having one or more instances. A model is composed of several such meshes and their instanaces. Simply put,
 * a mesh is the largest set of vertices that can be represented by a transformation matrix. For example - A car can have several meshes - body, windshield, doors, wheels etc. Ideally,
 * one should combine the body, windshield into a single mesh, since the combination only require one transformation matrix. We should have seperate mesh for the wheel and door and create 
 * multiple instances with differnt transforms.

 * Material loading philosphy - Refering back to the car example, let's consider the body and windshield as a single mesh. Then the problem is body and windshield has different 
 * materials, although they share same rigid transform. To solve this problem, we specify per vertex material. This is currently the default route.
 * As alternative, we can also specify per instance material. Consider the case when you want to have a glass ball and a steel ball. Although they share same geometric mesh, they are 
 * different in terms of material. It would be super-wasteful to have two seperate mesh just to have different materials. Specifying per insatnce material overrides the per vertex 
 * material values if any.

 * Accelaration Structure notes - We want to minimize the number of bottom level accelaration structures for performance reasons. 
 * Ideally one should categorize the meshes into groups and create one bottom level accelaration structure for each group.
 * In our implementation we will simply associate one BLAS for each mesh. Thus we assume each mesh is already a combination of several other meshes as explained above.
 * Also we simply use graphics queues and buffers for constructing the accelaration structure.
 */

#define VERTEX_BINDING_ID	0
#define STATIC_INSTANCE_BINDING_ID	1
#define DYNAMIC_INSTANCE_BINDING_ID 2 

// Do not shuffle the order, otherwise shaders must updated 
enum BRDF_TYPE { DIFFUSE, BECKMANN, GGX, DIELECTRIC, AREA };

struct Material
{
	uint32_t diffuseTextureIdx;
	uint32_t specularTextureIdx;
	uint32_t alphaIntExtIorTextureIdx;
	uint32_t materialType;
};

struct Vertex 
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec3 normal;
	glm::vec2 texCoord;
	uint32_t materialIndex;

	bool operator==(const Vertex& other) const 
	{
		return pos == other.pos && color == other.color && normal == other.normal && texCoord == other.texCoord;
	}
};

namespace std 
{
	template<> struct hash<Vertex> 
	{
		size_t operator()(Vertex const& vertex) const 
		{
			return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

// A mesh can have multiple instances.
// Each instance has dynamic data and static data.

// Per instance data, not meant for draw time updates
struct InstanceData_static 
{
	glm::uvec4 data; // material index, primitive start offset, area light offset (24 bit) | radiance of light source (8 bit), 0
};

// Per instance data, update at drawtime
struct InstanceData_dynamic {
	glm::mat4 model;
	glm::mat4 modelIT; // inverse transpose for normal
	glm::mat4 modelPrev;
};

// defines a single mesh and its instances
class Mesh 
{
public:
	// define a single mesh
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	// set of instance count for this type of mesh
	uint32_t instanceCount = 0;
	// compute bounding sphere, store center and radius
	glm::vec4 boundingSphere;

	BottomLevelAccelerationStructure as_bottomLevel;

	void initBLAS(const VkDevice& device, const VkCommandBuffer &cmdBuf, const VmaAllocator& allocator, const VkBuffer &vertexBuffer, const VkDeviceSize vertexBufferOffset, const VkBuffer &indexBuffer, const VkDeviceSize indexBufferOffset) 
	{
		CHECK(vertexBuffer != VK_NULL_HANDLE,
			"Model: Vertex buffer for creating BLAS not initialized");

		CHECK(indexBuffer != VK_NULL_HANDLE,
			"Model: Vertex indices for creating BLAS not initialized");

		std::vector<VkGeometryNV> vGeometry;

		VkGeometryNV geometry;
		geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
		geometry.pNext = nullptr;
		geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
		geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
		geometry.geometry.triangles.pNext = nullptr;
		geometry.geometry.triangles.vertexData = vertexBuffer;
		geometry.geometry.triangles.vertexOffset = vertexBufferOffset;
		geometry.geometry.triangles.vertexCount = static_cast<uint32_t>(vertices.size());
		geometry.geometry.triangles.vertexStride = sizeof(Vertex);
		// Limitation to 3xfloat32 for vertices
		geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry.geometry.triangles.indexData = indexBuffer;
		geometry.geometry.triangles.indexOffset = indexBufferOffset;
		geometry.geometry.triangles.indexCount = static_cast<uint32_t>(indices.size());
		// Limitation to 32-bit indices
		geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
		geometry.geometry.triangles.transformOffset = 0;
		geometry.geometry.aabbs = { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

		vGeometry.push_back(geometry);
		as_bottomLevel.create(device, allocator, vGeometry);
		as_bottomLevel.cmdBuild(cmdBuf, vGeometry);
	}

	void normailze(float scale, const glm::vec3 &shift = glm::vec3(0.0f))
	{
		glm::vec3 centroid(0.0f);
		for (const auto& vertex : vertices)
			centroid += vertex.pos;

		centroid /= vertices.size();

		float std = 0;
		for (size_t i = 0; i < vertices.size(); i++) {
			vertices[i].pos -= centroid;
			std += glm::length(vertices[i].pos);
		}

		std /= vertices.size();

		for (auto& vertex : vertices) {
			vertex.pos /= std;
			vertex.pos *= scale;
			vertex.pos += shift;
		}
	}

	void computeBoundingSphere()
	{
		glm::vec3 centroid(0.0f);
		for (const auto& vertex : vertices)
			centroid += vertex.pos;

		centroid /= vertices.size();
		float r = 0;
		for (const auto& vertex : vertices)
			r = glm::max(r, glm::length(vertex.pos - centroid));

		boundingSphere = glm::vec4(centroid, r);
	}
};

// composed of individual meshes
class Model {
public:
	VkImageView ldrTextureImageView;
	VkSampler ldrTextureSampler = VK_NULL_HANDLE;

	VkImageView hdrTextureImageView;
	VkSampler hdrTextureSampler = VK_NULL_HANDLE;
	
	~Model()
	{
		for (auto mesh : meshes)
			delete mesh;
	}

	uint32_t addMesh(Mesh* mesh) 
	{	
		size_t indexOffset = 0;
		size_t meshVertexOffset = 0;

		for (auto& mesh : meshes) {
			indexOffset += mesh->indices.size();
			meshVertexOffset += mesh->vertices.size();
		}
		
		vertices.insert(vertices.end(), mesh->vertices.begin(), mesh->vertices.end());
		indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
		indicesRtx.insert(indicesRtx.end(), mesh->indices.begin(), mesh->indices.end());
		
		for (size_t i = indexOffset; i < indices.size(); i++)
			indices[i] += static_cast<uint32_t>(meshVertexOffset);

		VkDrawIndexedIndirectCommand indirectCmd = {};
		indirectCmd.firstInstance = 0; // Tells Vulkan which index of the instanceData (Static and Dynamic) to look at, this must be updated after adding each instance
		indirectCmd.instanceCount = mesh->instanceCount; // also update after adding each insatnce
		indirectCmd.firstIndex = static_cast<uint32_t>(indexOffset);
		indirectCmd.indexCount = static_cast<uint32_t>(mesh->indices.size());

		indirectCommands.push_back(indirectCmd);
		
		meshes.push_back(mesh);

		return static_cast<uint32_t>(meshes.size());
	}

	uint32_t addLdrTexture(Image2d texture)
	{	
		CHECK(texture.format == VK_FORMAT_R8G8B8A8_UNORM,
			"Model : Ldr texture must be VK_FORMAT_R8G8B8A8_UNORM format type");

		return static_cast<uint32_t>(ldrTexGen.addTexture(texture));
	}

	uint32_t addHdrTexture(Image2d texture)
	{
		CHECK(texture.format == VK_FORMAT_R32G32B32A32_SFLOAT,
			"Model: Hdr texture must be VK_FORMAT_R32G32B32A32_SFLOAT format type");

		return static_cast<uint32_t>(hdrTexGen.addTexture(texture));
	}

	uint32_t addMaterial(uint32_t diffuseTextureIdx, uint32_t specularTextureIdx, uint32_t alphaIorTextureIdx, uint32_t materialfType)
	{
		CHECK(diffuseTextureIdx < ldrTexGen.size(),
			"Model: This ldr texture does not exsist");

		CHECK(specularTextureIdx < ldrTexGen.size(),
			"Model : This ldr texture does not exsist");

		CHECK(alphaIorTextureIdx < hdrTexGen.size(),
			"Model : This hdr texture does not exsist");

		materials.push_back({ diffuseTextureIdx, specularTextureIdx, alphaIorTextureIdx, materialfType });

		return static_cast<uint32_t>(materials.size());
	}
	
	// When non-default materialIndex is provided, it will override the per vertex material.
	uint32_t addInstance(uint32_t meshIdx, glm::mat4 &transform, uint32_t materialIndex = 0xffffffff, uint32_t radiance = 0)
	{
		CHECK(meshIdx < meshes.size(), "Model: This mesh does not exsist");
		CHECK(materialIndex >= 0xffffffff || materialIndex < materials.size(), "Model: This material does not exsist");
		CHECK(radiance < 256, "Model: Radiance value should be less than 256, since we have 8 bits to represent radiance.");

		uint32_t indexOffset = 0;
		for (uint32_t i = 0; i < meshIdx; i++)
			indexOffset += static_cast<uint32_t>(meshes[i]->indices.size());
		
		InstanceData_static instanceDataStatic;
		instanceDataStatic.data = glm::uvec4(materialIndex, indexOffset, radiance, 0);

		// When the instance is of type light source, ensure the matrial type is AREA (or other emitter type)
		if (radiance) {
			const Mesh* mesh = meshes[meshIdx];
			if (materialIndex >= 0xffffffff) {

				// Ensure all vertices have a materialIndex that points to a material of type AREA
				for (const auto& v : mesh->vertices) {
					if (materials[v.materialIndex].materialType != AREA) {
						WARN(false, "Model: Material type must be AREA when radiance param is non zero!");
						materials[v.materialIndex].materialType = AREA;
					}
				}
			}
			else if (materials[materialIndex].materialType != AREA) {
				WARN(false, "Model: Material type must be AREA (or other emitter type) when radiance param is non zero!");
				materials[materialIndex].materialType = AREA; // TODO :: change to appropriate emiiter type in future
			}

			instanceDataStatic.data.z = instanceDataStatic.data.z | (areaLightPrimitiveOffsetCounter << 8);
			areaLightPrimitiveOffsetCounter += static_cast<uint32_t>(mesh->indices.size());
		}
		
		// assumes instances have meshIdx in groups i.e. aaa-bbbbb-ccccc-dddddd. 
		uint32_t firstInstance = 0;
		while (firstInstance < meshPointers.size() && meshPointers[firstInstance] != meshIdx)
			firstInstance++;

		if (meshPointers.size() < 2 || firstInstance >= meshPointers.size() - 1) {
			instanceData_static.push_back(instanceDataStatic);
			instanceData_dynamic.push_back({ transform, glm::transpose(glm::inverse(transform)), transform });
			meshPointers.push_back(meshIdx);
		}
		else {
			instanceData_static.insert(instanceData_static.begin() + firstInstance + 1, 1, instanceDataStatic);
			instanceData_dynamic.insert(instanceData_dynamic.begin() + firstInstance + 1, 1, { transform, glm::transpose(glm::inverse(transform)), transform });
			meshPointers.insert(meshPointers.begin() + firstInstance + 1, 1, meshIdx);
		}
		
		meshes[meshIdx]->instanceCount++;

		for (uint32_t meshIdx = 0; meshIdx < meshes.size(); meshIdx++) {
			firstInstance = 0;
			while (firstInstance < meshPointers.size() && meshPointers[firstInstance] != meshIdx)
				firstInstance++;

			indirectCommands[meshIdx].firstInstance = firstInstance;
			indirectCommands[meshIdx].instanceCount = meshes[meshIdx]->instanceCount;

		}

		return static_cast<uint32_t>(instanceData_static.size());
	}

	static std::vector<VkVertexInputBindingDescription> getBindingDescription() 
	{
		std::array<VkVertexInputBindingDescription, 3> bindingDescription = {};
		bindingDescription[0].binding = VERTEX_BINDING_ID;
		bindingDescription[0].stride = sizeof(Vertex);
		bindingDescription[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		bindingDescription[1].binding = STATIC_INSTANCE_BINDING_ID;
		bindingDescription[1].stride = sizeof(InstanceData_static);
		bindingDescription[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
		
		bindingDescription[2].binding = DYNAMIC_INSTANCE_BINDING_ID;
		bindingDescription[2].stride = sizeof(InstanceData_dynamic);
		bindingDescription[2].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
		
		return std::vector<VkVertexInputBindingDescription>(bindingDescription.begin(), bindingDescription.end());
	}

	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() 
	{
		std::array<VkVertexInputAttributeDescription, 18> attributeDescriptions = {};
		uint32_t location = 0;
		// per vertex
		attributeDescriptions[location].binding = VERTEX_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(Vertex, pos);

		location++;

		attributeDescriptions[location].binding = VERTEX_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(Vertex, color);

		location++;

		attributeDescriptions[location].binding = VERTEX_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(Vertex, normal);

		location++;

		attributeDescriptions[location].binding = VERTEX_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(Vertex, texCoord);

		location++;

		// per instance static
		attributeDescriptions[location].binding = STATIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_UINT;
		attributeDescriptions[location].offset = offsetof(InstanceData_static, data);

		location++;

		// per instance dynamic
		// We use next four locations for mat4 or 4 x vec4
		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, model);

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, model) + 16;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, model) + 32;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, model) + 48;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelIT);

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelIT) + 16;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelIT) + 32;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelIT) + 48;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelPrev);

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelPrev) + 16;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelPrev) + 32;

		location++;

		attributeDescriptions[location].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[location].offset = offsetof(InstanceData_dynamic, modelPrev) + 48;

		location++;

		// per vertex
		attributeDescriptions[location].binding = VERTEX_BINDING_ID;
		attributeDescriptions[location].location = location;
		attributeDescriptions[location].format = VK_FORMAT_R32_UINT;
		attributeDescriptions[location].offset = offsetof(Vertex, materialIndex);

		return std::vector<VkVertexInputAttributeDescription>(attributeDescriptions.begin(), attributeDescriptions.end());
	}

	VkDescriptorBufferInfo getMaterialDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = materialBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	// Used to transfer vetex data to Rtx shader as raw buffer
	VkDescriptorBufferInfo getVertexDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = vertexBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;
		
		return descriptorBufferInfo;
	}

	// Used to transfer index data to Rtx shader as raw buffer
	VkDescriptorBufferInfo getIndexDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = indexBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	// Used to transfer per instance static data to Rtx shader as raw buffer
	VkDescriptorBufferInfo getStaticInstanceDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = staticInstanceBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	void updateMeshData(bool animate = false)
	{	
		if (animate) {
			uint32_t idx = 0;
			for (auto& instance : instanceData_dynamic) {
				instance.modelPrev = instance.model;
				instance.model = glm::translate<float>(instance.model, glm::vec3(0.0, 0.0, 0.0));
				glm::mat4 rotate = glm::identity<glm::mat4>();
				if (idx == 1) {
					instance.model = glm::rotate<float>(instance.model, 0.05f, glm::vec3(1, 1, 0));
				}
				else if (idx == 2 || idx == 3) {
					rotate = glm::rotate<float>(rotate, 0.05f, glm::vec3(0, 1, 0));
					instance.model = rotate * instance.model;
				}
				instance.modelIT = glm::transpose(glm::inverse(instance.model));
				idx++;
			}
		}
		memcpy(mappedDynamicInstancePtr, instanceData_dynamic.data(), sizeof(instanceData_dynamic[0]) * instanceData_dynamic.size());
	}

	void cmdTransferData(const VkCommandBuffer &cmdBuffer) 
	{
		VkBufferCopy copyRegion = {};
		copyRegion.size = sizeof(instanceData_dynamic[0]) * instanceData_dynamic.size();

		vkCmdCopyBuffer(cmdBuffer, dynamicInstanceStagingBuffer, dynamicInstanceBuffer, 1, &copyRegion);

		VkBufferMemoryBarrier bufferMemoryBarrier = {};
		bufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferMemoryBarrier.buffer = dynamicInstanceBuffer;
		bufferMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		bufferMemoryBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		bufferMemoryBarrier.size = copyRegion.size;
		
		vkCmdPipelineBarrier(
			cmdBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			0,
			0, nullptr,
			1, &bufferMemoryBarrier,
			0, nullptr);
	}

	void cmdDraw(const VkCommandBuffer& cmdBuffer) 
	{
		VkBuffer vertexBuffers[] = { vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, VERTEX_BINDING_ID, 1, vertexBuffers, offsets);
		VkBuffer staticInstanceBuffers[] = { staticInstanceBuffer };
		vkCmdBindVertexBuffers(cmdBuffer, STATIC_INSTANCE_BINDING_ID, 1, staticInstanceBuffers, offsets);
		VkBuffer dynamicInstanceBuffers[] = { dynamicInstanceBuffer };
		vkCmdBindVertexBuffers(cmdBuffer, DYNAMIC_INSTANCE_BINDING_ID, 1, dynamicInstanceBuffers, offsets);
		vkCmdBindIndexBuffer(cmdBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		
		vkCmdDrawIndexedIndirect(cmdBuffer, indirectCmdBuffer, 0, static_cast<uint32_t>(meshes.size()), sizeof(VkDrawIndexedIndirectCommand));
		//vkCmdDrawIndexed(cmdBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
	}

	void createBuffers(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) 
	{	
		CHECK(ldrTexGen.size() != 0, "Model: LDR textures have not been added.");

		CHECK(hdrTexGen.size() != 0, "Model: HDR textures have not been added.");

		CHECK(materials.size() != 0, "Model: Materials have not been added.");

		CHECK(meshes.size() != 0, "Model: Meshes have not been added.");

		createBuffer(device, allocator, queue, commandPool, materialBuffer, materialBufferAllocation, sizeof(Material) * materials.size(), materials.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createBuffer(device, allocator, queue, commandPool, vertexBuffer, vertexBufferAllocation, sizeof(Vertex) * vertices.size(), vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createBuffer(device, allocator, queue, commandPool, staticInstanceBuffer, staticInstanceBufferAllocation, sizeof(instanceData_static[0]) * instanceData_static.size(), instanceData_static.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createDynamicInstanceBuffer(device, allocator, queue, commandPool);
		createBuffer(device, allocator, queue, commandPool, indexBuffer, indexBufferAllocation, sizeof(indices[0]) * indices.size(), indices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createBuffer(device, allocator, queue, commandPool, indirectCmdBuffer, indirectCmdBufferAllocation, sizeof(VkDrawIndexedIndirectCommand) * meshes.size(), indirectCommands.data(), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		ldrTexGen.createTexture(physicalDevice, device, allocator, queue, commandPool, ldrTextureImage, ldrTextureImageView, ldrTextureSampler, ldrTextureImageAllocation);
		hdrTexGen.createTexture(physicalDevice, device, allocator, queue, commandPool, hdrTextureImage, hdrTextureImageView, hdrTextureSampler, hdrTextureImageAllocation);
	}

	void createRtxBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) 
	{
		VkDeviceSize vertexOffsetInBytes = 0;
		VkDeviceSize indexOffsetInBytes = 0;
		createBuffer(device, allocator, queue, commandPool, indexBufferRtx, indexBufferRtxAllocation, sizeof(indicesRtx[0]) * indicesRtx.size(), indicesRtx.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		VkCommandBuffer cmdBuf = beginSingleTimeCommands(device, commandPool);
		for (auto &mesh : meshes) {
			mesh->initBLAS(device, cmdBuf, allocator, vertexBuffer, vertexOffsetInBytes, indexBufferRtx, indexOffsetInBytes);
			vertexOffsetInBytes += static_cast<VkDeviceSize>(mesh->vertices.size() * sizeof(Vertex));
			indexOffsetInBytes += static_cast<VkDeviceSize>(mesh->indices.size() * sizeof(uint32_t));
		}

		as_topLevel.create(device, allocator, static_cast<uint32_t>(instanceData_dynamic.size()), false);
		updateTlasData();
		as_topLevel.cmdBuild(cmdBuf, static_cast<uint32_t>(instanceData_dynamic.size()), false);
		endSingleTimeCommands(device, queue, commandPool, cmdBuf);
	}

	void cmdUpdateTlas(const VkCommandBuffer& cmdBuf)
	{
		as_topLevel.cmdBuild(cmdBuf, static_cast<uint32_t>(instanceData_dynamic.size()), false);
	}

	void updateTlasData() 
	{
		tlas_instanceData.clear();
		uint32_t globalInstanceId = 0;
		for (auto& instance :instanceData_dynamic) {
			TopLevelAccelerationStructureData data;
			// Copy first three rows of transformation matrix of each instance
			glm::mat4 modelTrans = glm::transpose(instance.model);
			memcpy(data.transform, &modelTrans, sizeof(data.transform));
			data.instanceId = globalInstanceId;
			data.mask = 0xff;
			data.instanceOffset = 0; // Since this is used to determine hit group index compuation, this may change
			data.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
			data.blasHandle = meshes[meshPointers[globalInstanceId]]->as_bottomLevel.handle;
			tlas_instanceData.push_back(data);
			globalInstanceId++;
		}

		CHECK_DBG_ONLY(globalInstanceId == instanceData_dynamic.size(),
			"Model: Number of instances for Top Level Accelaration structure should match dynamic instance data count.");

		as_topLevel.updateInstanceData(tlas_instanceData);
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator) 
	{	
		vkDestroySampler(device, hdrTextureSampler, nullptr);
		vkDestroyImageView(device, hdrTextureImageView, nullptr);
		vmaDestroyImage(allocator, hdrTextureImage, hdrTextureImageAllocation);

		vkDestroySampler(device, ldrTextureSampler, nullptr);
		vkDestroyImageView(device, ldrTextureImageView, nullptr);
		vmaDestroyImage(allocator, ldrTextureImage, ldrTextureImageAllocation);
		
		vmaDestroyBuffer(allocator, indirectCmdBuffer, indirectCmdBufferAllocation);
		vmaDestroyBuffer(allocator, indexBuffer, indexBufferAllocation);
		vmaDestroyBuffer(allocator, dynamicInstanceBuffer, dynamicInstanceBufferAllocation);
		vmaUnmapMemory(allocator, dynamicInstanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, dynamicInstanceStagingBuffer, dynamicInstanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, staticInstanceBuffer, staticInstanceBufferAllocation);
		vmaDestroyBuffer(allocator, vertexBuffer, vertexBufferAllocation);
		vmaDestroyBuffer(allocator, materialBuffer, materialBufferAllocation);
	}

	void cleanUpRtx(const VkDevice& device, const VmaAllocator& allocator) 
	{
		for (auto& mesh : meshes)
			mesh->as_bottomLevel.cleanUp(device, allocator);

		as_topLevel.cleanUp(device, allocator);
		vmaDestroyBuffer(allocator, indexBufferRtx, indexBufferRtxAllocation);
	}

	VkWriteDescriptorSetAccelerationStructureNV getDescriptorTlas() const
	{
		return as_topLevel.getDescriptorTlasInfo();
	}
private:
	friend class AreaLightSources;

	std::vector<Material> materials; // store matrials
	std::vector<Mesh *> meshes; // ideally store unique meshes
	std::vector<Vertex> vertices; // concatenate vertices from all meshes
	std::vector<uint32_t> indices;  // indeces into the above global vertices
	std::vector<InstanceData_static> instanceData_static; // concatenate instances from all meshes. Note each mesh can have multiple instances. 
	std::vector<InstanceData_dynamic> instanceData_dynamic;  // concatenate instances from all meshes. Note each mesh can have multiple instances.
	std::vector<uint32_t> meshPointers; // Pointer to the mesh for each instance.
	
	void *mappedDynamicInstancePtr;
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands; // Its size is meshes.size().
	
	VkBuffer materialBuffer = VK_NULL_HANDLE;
	VmaAllocation materialBufferAllocation = VK_NULL_HANDLE;
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
	VkBuffer staticInstanceBuffer = VK_NULL_HANDLE;
	VmaAllocation staticInstanceBufferAllocation = VK_NULL_HANDLE;
	VkBuffer dynamicInstanceBuffer = VK_NULL_HANDLE;
	VmaAllocation dynamicInstanceBufferAllocation = VK_NULL_HANDLE;
	VkBuffer dynamicInstanceStagingBuffer = VK_NULL_HANDLE;
	VmaAllocation dynamicInstanceStagingBufferAllocation = VK_NULL_HANDLE;
	VkBuffer indexBuffer = VK_NULL_HANDLE;
	VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
	VkBuffer indirectCmdBuffer = VK_NULL_HANDLE;
	VmaAllocation indirectCmdBufferAllocation = VK_NULL_HANDLE;

	TextureGenerator ldrTexGen = TextureGenerator("Model: LDR texture");
	VkImage ldrTextureImage;
	VmaAllocation ldrTextureImageAllocation;

	TextureGenerator hdrTexGen = TextureGenerator("Model: HDR texture");;
	VkImage hdrTextureImage;
	VmaAllocation hdrTextureImageAllocation;

	uint32_t areaLightPrimitiveOffsetCounter = 0;

	/** RTX Data **/
	TopLevelAccelerationStructure as_topLevel;
	std::vector<TopLevelAccelerationStructureData> tlas_instanceData;
	VkBuffer indexBufferRtx = VK_NULL_HANDLE;
	VmaAllocation indexBufferRtxAllocation = VK_NULL_HANDLE;
	std::vector<uint32_t> indicesRtx;
	
	void createDynamicInstanceBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) 
	{
		VkDeviceSize bufferSize = sizeof(instanceData_dynamic[0]) * instanceData_dynamic.size();
	
		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

		VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &dynamicInstanceStagingBuffer, &dynamicInstanceStagingBufferAllocation, nullptr),
			"Model: Failed to create staging buffer for dynamic instances!");
		
		vmaMapMemory(allocator, dynamicInstanceStagingBufferAllocation, &mappedDynamicInstancePtr);
		
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &dynamicInstanceBuffer, &dynamicInstanceBufferAllocation, nullptr),
			"Model : Failed to create buffer for dynamic instances!");
	}
};

#undef VERTEX_BINDING_ID
#undef STATIC_INSTANCE_BINIDING_ID
#undef DYNAMIC_INSTANCE_BINDING_ID