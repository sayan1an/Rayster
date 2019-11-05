#pragma once

#include <algorithm>
#include <vector>
#include <array>

#include "vulkan/vulkan.h"
#include "stb_image.h"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "helper.h"
#include "accelerationStructure.h"

/*
 * Mesh organisation - Think of each mesh having one or more instances. A model is composed of several such meshes and their instanaces. Simply put,
 * a mesh is the largest set of vertices that can be represented by a transformation matrix. For example - A car can have several meshes - body, windshield, doors, wheels etc. Ideally,
 * one should combine the body, windshield into a single mesh, since the combination only require one transformation matrix. We should have seperate mesh for wheels and doors as the each 
 * require one transform matrix.

 * Accelaration Structure notes - We want to minimize the number of bottom level accelaration structures for performance reasons. 
 * Ideally one should categorize the meshes into groups and create one bottom level accelaration structure for each group.
 * In our implementation we will simply associate one BLAS for each mesh. Thus we assume each mesh is already a combination of several other meshes as explained above.
 * Also we simply use graphics queues and buffers for constructing the accelaration structure.
 */

#define VERTEX_BINDING_ID	0
#define STATIC_INSTANCE_BINDING_ID	1
#define DYNAMIC_INSTANCE_BINDING_ID 2 

struct Vertex 
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec3 normal;
	glm::vec2 texCoord;
	float dummy;

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
	glm::uvec4 data; // diffuse texture index, specular texture index, alpha_intIor_extIor texture index, Material brdf type
};

// Per instance data, update at drawtime
struct InstanceData_dynamic {
	glm::mat4 model;
};

struct Image2d 
{
	void *pixels = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	std::string path;

	uint32_t size() const 
	{
		switch (format) {
			case VK_FORMAT_R8G8B8A8_UNORM:
				return height * width * 4 * sizeof(unsigned char);
			case VK_FORMAT_R32G32B32A32_SFLOAT:
				return height * width * 4 * sizeof(float);
			default:
				throw std::runtime_error("Unrecognised texture format.");
		}

		return 0;
	}

	uint32_t mipLevels() 
	{	
		if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
			return 1;

		return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	}

	Image2d(const std::string texturePath) 
	{
		int texChannels, iWidth, iHeight;
		pixels = stbi_load(texturePath.c_str(), &iWidth, &iHeight, &texChannels, STBI_rgb_alpha);
		if (!pixels) {
			throw std::runtime_error("failed to load texture image!");
		}

		width = static_cast<uint32_t> (iWidth);
		height = static_cast<uint32_t> (iHeight);
		format = VK_FORMAT_R8G8B8A8_UNORM;
		path = texturePath;
	}

	void cleanUp() 
	{
		stbi_image_free(pixels);
	}

	Image2d(uint32_t width = 1, uint32_t height = 1, glm::vec4 color = glm::vec4(1.0f), boolean hdr = false)
	{
		this->width = width;
		this->height = height;
		if (hdr) {
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			pixels = new float[(size_t)width * height * 4];

			for (size_t i = 0; i < width * height * 4; i += 4) {
				((float *)pixels)[i] = color.x;
				((float *)pixels)[i + 1] = color.y;
				((float *)pixels)[i + 2] = color.z;
				((float *)pixels)[i + 3] = color.w;
			}
		}
		else {
			format = VK_FORMAT_R8G8B8A8_UNORM;
			pixels = new unsigned char[(size_t)width * height * 4];

			auto floatToUint8 = [](float a)
			{
				return static_cast<unsigned char>(static_cast<uint32_t>(a * 255) & 0xff);
			};

			for (size_t i = 0; i < width * height * 4; i += 4) {
				((unsigned char*)pixels)[i] = floatToUint8(color.x);
				((unsigned char*)pixels)[i + 1] = floatToUint8(color.y);
				((unsigned char*)pixels)[i + 2] = floatToUint8(color.z);
				((unsigned char*)pixels)[i + 3] = floatToUint8(color.w);
			}
		}
				
		path = "";
	}
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

	BottomLevelAccelerationStructure as_bottomLevel;

	void initBLAS(const VkDevice& device, const VkCommandBuffer &cmdBuf, const VmaAllocator& allocator, const VkBuffer &vertexBuffer, const VkDeviceSize vertexBufferOffset, const VkBuffer &indexBuffer, const VkDeviceSize indexBufferOffset) 
	{
		if (vertexBuffer == VK_NULL_HANDLE)
			throw std::runtime_error("Vertex buffer for creating BLAS not initialized");

		if (indexBuffer == VK_NULL_HANDLE)
			throw std::runtime_error("Vertex indices for creating BLAS not initialized");

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

	size_t addMesh(Mesh* mesh) 
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
		return meshes.size();
	}

	size_t addLdrTexture(Image2d texture)
	{	
		if (texture.format != VK_FORMAT_R8G8B8A8_UNORM)
			throw std::runtime_error("Ldr texture must be VK_FORMAT_R8G8B8A8_UNORM format type");

		ldrTextureCache.push_back(texture);
		fixTextureCache(ldrTextureCache);

		return ldrTextureCache.size();
	}

	size_t addHdrTexture(Image2d texture)
	{
		if (texture.format != VK_FORMAT_R32G32B32A32_SFLOAT)
			throw std::runtime_error("Hdr texture must be VK_FORMAT_R32G32B32A32_SFLOAT format type");

		hdrTextureCache.push_back(texture);
		fixTextureCache(hdrTextureCache);

		return hdrTextureCache.size();
	}
	
	void addInstance(uint32_t meshIdx, uint32_t textureIdx, glm::mat4 &transform)
	{
		if (meshIdx >= meshes.size())
			throw std::runtime_error("This mesh does not exsist");

		if (textureIdx >= ldrTextureCache.size())
			throw std::runtime_error("This texture does not exsist");

		// assumes instances have meshIdx in groups i.e. aaa-bbbbb-ccccc-dddddd. 
		uint32_t firstInstance = 0;
		while (firstInstance < meshPointers.size() && meshPointers[firstInstance] != meshIdx)
			firstInstance++;

		if (meshPointers.size() < 2 || firstInstance >= meshPointers.size() - 1) {
			instanceData_static.push_back({ glm::uvec4(textureIdx, 0, 0, 0) });
			instanceData_dynamic.push_back({ transform });
			meshPointers.push_back(meshIdx);
		}
		else {
			instanceData_static.insert(instanceData_static.begin() + firstInstance + 1, 1, { glm::uvec4(textureIdx, 0, 0, 0) });
			instanceData_dynamic.insert(instanceData_dynamic.begin() + firstInstance + 1, 1, { transform });
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
		std::array<VkVertexInputAttributeDescription, 9> attributeDescriptions = {};
		// per vertex
		attributeDescriptions[0].binding = VERTEX_BINDING_ID;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);

		attributeDescriptions[1].binding = VERTEX_BINDING_ID;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		attributeDescriptions[2].binding = VERTEX_BINDING_ID;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, normal);

		attributeDescriptions[3].binding = VERTEX_BINDING_ID;
		attributeDescriptions[3].location = 3;
		attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

		// per instance static
		attributeDescriptions[4].binding = STATIC_INSTANCE_BINDING_ID;
		attributeDescriptions[4].location = 4;
		attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_UINT;
		attributeDescriptions[4].offset = offsetof(InstanceData_static, data);

		// per instance dynamic
		// We use next four locations for mat4 or 4 x vec4
		attributeDescriptions[5].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[5].location = 5;
		attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[5].offset = offsetof(InstanceData_dynamic, model);

		attributeDescriptions[6].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[6].location = 6;
		attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[6].offset = offsetof(InstanceData_dynamic, model) + 16;

		attributeDescriptions[7].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[7].location = 7;
		attributeDescriptions[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[7].offset = offsetof(InstanceData_dynamic, model) + 32;

		attributeDescriptions[8].binding = DYNAMIC_INSTANCE_BINDING_ID;
		attributeDescriptions[8].location = 8;
		attributeDescriptions[8].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[8].offset = offsetof(InstanceData_dynamic, model) + 48;

		return std::vector<VkVertexInputAttributeDescription>(attributeDescriptions.begin(), attributeDescriptions.end());
	}

	void updateMeshData() 
	{
		for (auto& instance : instanceData_dynamic)
			instance.model = glm::translate<float>(instance.model,glm::vec3(0.0, 0.0, 0.0));

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
		createBuffer(device, allocator, queue, commandPool, vertexBuffer, vertexBufferAllocation, sizeof(Vertex) * vertices.size(), vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createBuffer(device, allocator, queue, commandPool, staticInstanceBuffer, staticInstanceBufferAllocation, sizeof(instanceData_static[0]) * instanceData_static.size(), instanceData_static.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		createDynamicInstanceBuffer(device, allocator, queue, commandPool);
		createBuffer(device, allocator, queue, commandPool, indexBuffer, indexBufferAllocation, sizeof(indices[0]) * indices.size(), indices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
		createBuffer(device, allocator, queue, commandPool, indirectCmdBuffer, indirectCmdBufferAllocation, sizeof(VkDrawIndexedIndirectCommand) * meshes.size(), indirectCommands.data(), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		createTextureImage(physicalDevice, device, allocator, queue, commandPool);
		createTextureImageHdr(physicalDevice, device, allocator, queue, commandPool);
		createTextureImageView(device);
		createTextureSampler(device, ldrTextureSampler, mipLevels);
		createTextureSampler(device, hdrTextureSampler, 1);
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

		as_topLevel.create(device, allocator, static_cast<uint32_t>(instanceData_dynamic.size()));
		updateTlasData();
		as_topLevel.cmdBuild(cmdBuf, static_cast<uint32_t>(instanceData_dynamic.size()), false);
		endSingleTimeCommands(device, queue, commandPool, cmdBuf);
	}

	void cmdUpdateTlas(const VkCommandBuffer& cmdBuf)
	{
		as_topLevel.cmdBuild(cmdBuf, static_cast<uint32_t>(instanceData_dynamic.size()), true);
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

		if (globalInstanceId != instanceData_dynamic.size())
			throw std::runtime_error("Number of instances for Top Level Accelaration structure should match dynamic instance data count.");

		as_topLevel.updateInstanceData(tlas_instanceData);
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator) 
	{
		vkDestroySampler(device, ldrTextureSampler, nullptr);
		vkDestroyImageView(device, ldrTextureImageView, nullptr);

		vmaDestroyImage(allocator, ldrTextureImage, ldrTextureImageAllocation);

		vkDestroySampler(device, hdrTextureSampler, nullptr);
		vkDestroyImageView(device, hdrTextureImageView, nullptr);

		vmaDestroyImage(allocator, hdrTextureImage, ldrTextureImageAllocation);

		vmaDestroyBuffer(allocator, indexBuffer, indexBufferAllocation);
		vmaDestroyBuffer(allocator, vertexBuffer, vertexBufferAllocation);
		vmaDestroyBuffer(allocator, staticInstanceBuffer, staticInstanceBufferAllocation);
		vmaDestroyBuffer(allocator, dynamicInstanceBuffer, dynamicInstanceBufferAllocation);
		vmaUnmapMemory(allocator, dynamicInstanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, dynamicInstanceStagingBuffer, dynamicInstanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, indirectCmdBuffer, indirectCmdBufferAllocation);
	}

	void cleanUpRtx(const VkDevice& device, const VmaAllocator& allocator) 
	{
		for (auto& mesh : meshes)
			mesh->as_bottomLevel.cleanUp(device, allocator);

		as_topLevel.cleanUp(device, allocator);
		vmaDestroyBuffer(allocator, indexBufferRtx, indexBufferAllocation);
	}

	VkWriteDescriptorSetAccelerationStructureNV getDescriptorTlas() const
	{
		return as_topLevel.getDescriptorTlasInfo();
	}
private:
	std::vector<Mesh *> meshes; // ideally store unique meshes
	std::vector<Vertex> vertices; // concatenate vertices from all meshes
	std::vector<uint32_t> indices;  // indeces into the above global vertices
	std::vector<InstanceData_static> instanceData_static; // concatenate instances from all meshes. Note each mesh can have multiple instances. 
	std::vector<InstanceData_dynamic> instanceData_dynamic;  // concatenate instances from all meshes. Note each mesh can have multiple instances.
	std::vector<uint32_t> meshPointers; // Pointer to the mesh for each instance.
	
	void *mappedDynamicInstancePtr;
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands; // Its size is meshes.size().
	std::vector<Image2d> ldrTextureCache; // ideally only store unique ldr textures.
	std::vector<Image2d> hdrTextureCache; // ideally only store unique hdr textures.

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

	uint32_t mipLevels;
	VkImage ldrTextureImage;
	VmaAllocation ldrTextureImageAllocation;

	VkImage hdrTextureImage;
	VmaAllocation hdrTextureImageAllocation;

	/** RTX Data **/
	TopLevelAccelerationStructure as_topLevel;
	std::vector<TopLevelAccelerationStructureData> tlas_instanceData;
	VkBuffer indexBufferRtx = VK_NULL_HANDLE;
	VmaAllocation indexBufferRtxAllocation = VK_NULL_HANDLE;
	std::vector<uint32_t> indicesRtx;

	void fixTextureCache(const std::vector<Image2d> &texCache) 
	{
		if (texCache.empty())
			throw std::runtime_error("Provided texture cache is empty");
		else {
			// All images must have same image format and size. We can relax this restriction to having same aspect ratio and rescaling the
			// images to the largest one.
			
			// for now let's just throw an error when size and format are not same.
			// TODO : Check if the images have same aspect ratio and format. Upscale all images to the size of the largest one.
			VkFormat desiredFormat = texCache[0].format;
			uint32_t desiredWidth = texCache[0].width;
			uint32_t desiredHeight = texCache[0].height;
			for (const auto &image : texCache)
				if (image.format != desiredFormat || image.width != desiredWidth || image.height != desiredHeight)
					throw std::runtime_error("Size and format for all texture images must be same.");
		}
	}
	
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

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &dynamicInstanceStagingBuffer, &dynamicInstanceStagingBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create staging buffer for static instances!");
		
		vmaMapMemory(allocator, dynamicInstanceStagingBufferAllocation, &mappedDynamicInstancePtr);
		
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &dynamicInstanceBuffer, &dynamicInstanceBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create vertex buffer!");
	}

	void createTextureImage(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) 
	{
		mipLevels = ldrTextureCache[0].mipLevels();
		
		uint32_t bufferSize = 0;
		for (const auto &image : ldrTextureCache)
			bufferSize += image.size();

		VkBuffer stagingBuffer;
		VmaAllocation stagingBufferAllocation;

		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create staging buffer for index buffer!");

		void* data;
		vmaMapMemory(allocator, stagingBufferAllocation, &data);
		uint32_t bufferOffset = 0;
		for (auto &image : ldrTextureCache) {
			byte *start = static_cast<byte *>(data);
			memcpy(&start[bufferOffset], image.pixels, static_cast<size_t>(image.size()));
			bufferOffset += static_cast<size_t>(image.size());
			image.cleanUp();
		}
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		bufferOffset = 0;
		for (uint32_t layer = 0; layer < ldrTextureCache.size(); layer++) {
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = ldrTextureCache[0].width;
			bufferCopyRegion.imageExtent.height = ldrTextureCache[0].height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = bufferOffset;

			bufferCopyRegions.push_back(bufferCopyRegion);

			// Increase offset into staging buffer for next level / face
			bufferOffset += ldrTextureCache[layer].size();
		}

		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = ldrTextureCache[0].width;
		imageCreateInfo.extent.height = ldrTextureCache[0].height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = static_cast<uint32_t>(ldrTextureCache.size());
		imageCreateInfo.format = ldrTextureCache[0].format;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &ldrTextureImage, &ldrTextureImageAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create texture image!");

		transitionImageLayout(device, queue, commandPool, ldrTextureImage, ldrTextureCache[0].format, 
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, static_cast<uint32_t>(ldrTextureCache.size()));
		VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, ldrTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		endSingleTimeCommands(device, queue, commandPool, commandBuffer);
		//transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);

		generateMipmaps(physicalDevice, device, queue, commandPool, ldrTextureImage, 
			ldrTextureCache[0].format, ldrTextureCache[0].width, ldrTextureCache[0].height, mipLevels, static_cast<uint32_t>(ldrTextureCache.size()));
	}

	void createTextureImageHdr(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
	{
		uint32_t bufferSize = 0;
		for (const auto& image : hdrTextureCache)
			bufferSize += image.size();

		VkBuffer stagingBuffer;
		VmaAllocation stagingBufferAllocation;

		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create staging buffer for index buffer!");

		void* data;
		vmaMapMemory(allocator, stagingBufferAllocation, &data);
		uint32_t bufferOffset = 0;
		for (auto& image : hdrTextureCache) {
			byte* start = static_cast<byte*>(data);
			memcpy(&start[bufferOffset], image.pixels, static_cast<size_t>(image.size()));
			bufferOffset += static_cast<size_t>(image.size());
			image.cleanUp();
		}
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		bufferOffset = 0;
		for (uint32_t layer = 0; layer < hdrTextureCache.size(); layer++) {
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = hdrTextureCache[0].width;
			bufferCopyRegion.imageExtent.height = hdrTextureCache[0].height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = bufferOffset;

			bufferCopyRegions.push_back(bufferCopyRegion);

			// Increase offset into staging buffer for next level / face
			bufferOffset += hdrTextureCache[layer].size();
		}

		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = hdrTextureCache[0].width;
		imageCreateInfo.extent.height = hdrTextureCache[0].height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = static_cast<uint32_t>(hdrTextureCache.size());
		imageCreateInfo.format = hdrTextureCache[0].format;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &hdrTextureImage, &hdrTextureImageAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create texture image!");

		transitionImageLayout(device, queue, commandPool, hdrTextureImage, hdrTextureCache[0].format,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, static_cast<uint32_t>(hdrTextureCache.size()));
		VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, hdrTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		endSingleTimeCommands(device, queue, commandPool, commandBuffer);
		//transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);

		generateMipmaps(physicalDevice, device, queue, commandPool, hdrTextureImage,
			hdrTextureCache[0].format, hdrTextureCache[0].width, hdrTextureCache[0].height, 1, static_cast<uint32_t>(hdrTextureCache.size()));
	}

	void generateMipmaps(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
		VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels, uint32_t layerCount) 
	{	
		if (mipLevels == 1) {
			transitionImageLayout(device, queue, commandPool, image, imageFormat,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, layerCount);
			return;
		}

		// Check if image format supports linear blitting
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
			throw std::runtime_error("texture image format does not support linear blitting!");
		}

		VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layerCount;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < mipLevels; i++) {
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = layerCount;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = layerCount;

			vkCmdBlitImage(commandBuffer,
				image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		endSingleTimeCommands(device, queue, commandPool, commandBuffer);
	}

	void createTextureImageView(const VkDevice& device) 
	{
		ldrTextureImageView = createImageView(device, ldrTextureImage, ldrTextureCache[0].format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, static_cast<uint32_t>(ldrTextureCache.size()));
		hdrTextureImageView = createImageView(device, hdrTextureImage, hdrTextureCache[0].format, VK_IMAGE_ASPECT_COLOR_BIT, 1, static_cast<uint32_t>(hdrTextureCache.size()));
	}

	void createTextureSampler(const VkDevice& device, VkSampler &sampler, uint32_t mipLevels) 
	{
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0;
		samplerInfo.maxLod = static_cast<float>(mipLevels);
		samplerInfo.mipLodBias = 0;

		if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
			throw std::runtime_error("failed to create texture sampler!");
		}
	}
};

#undef VERTEX_BINDING_ID
#undef STATIC_INSTANCE_BINIDING_ID
#undef DYNAMIC_INSTANCE_BINDING_ID