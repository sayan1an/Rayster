#pragma once

#include <algorithm>
#include <vector>
#include <array>

#include "vulkan/vulkan.h"
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include "helper.h"

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

static const std::vector<std::string> MODEL_PATHS = { ROOT + "/models/cat.obj", ROOT + "/models/chalet.obj", ROOT + "/models/deer.obj"};
static const std::vector<std::string> TEXTURE_PATHS = { ROOT + "/textures/ubiLogo.jpg",  ROOT + "/textures/chalet.jpg" };

#define VERTEX_BINDING_ID	0
#define STATIC_INSTANCE_BINDING_ID	1
#define DYNAMIC_INSTANCE_BINDING_ID 2 

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec3 normal;
	glm::vec2 texCoord;

	bool operator==(const Vertex& other) const {
		return pos == other.pos && color == other.color && normal == other.normal && texCoord == other.texCoord;
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

// A mesh can have multiple instances.
// Each instance has dynamic data and static data.

// Per instance data, not meant for draw time updates
struct InstanceData_static {
	glm::vec3 translate;
};

// Per instance data, update at drawtime
struct InstanceData_dynamic {
	glm::mat4 model;
};

struct Image2d {
	void *pixels = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	std::string path;

	uint32_t size() const {
		switch (format) {
			case VK_FORMAT_R8G8B8A8_UNORM:
				return height * width * 4 * sizeof(unsigned char);
			default:
				throw std::runtime_error("Unrecognised format.");
		}

		return 0;
	}

	uint32_t mipLevels() {
		return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	}

	Image2d(const std::string texturePath) {
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

	void cleanUp() {
		stbi_image_free(pixels);
	}

	Image2d() {
		// creates a default white texture
		width = height = 512;
		format = VK_FORMAT_R8G8B8A8_UNORM;
		pixels = new unsigned char[width * height * 4];
		memset(pixels, 1, size());
		path = "";
	}
};

// defines a single mesh and its instances
class Mesh {
public:
	// define a single mesh
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	// set of instances for this type of mesh
	std::vector<InstanceData_static> instanceData_static;
	std::vector<InstanceData_dynamic> instanceData_dynamic;
	
	BottomLevelAccelerationStructure as_bottomLevel;
	
	Mesh(const char *meshPath, float trans) {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, meshPath)) {
			throw std::runtime_error(warn + err);
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices = {};

		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex = {};

				vertex.pos = {
					attrib.vertices[3 * (int)index.vertex_index + 0],
					attrib.vertices[3 * (int)index.vertex_index + 1],
					attrib.vertices[3 * (int)index.vertex_index + 2]
				};
				
				/*
				vertex.normal = {
					attrib.normals[3 * (int)index.normal_index + 0],
					attrib.normals[3 * (int)index.normal_index + 1],
					attrib.normals[3 * (int)index.normal_index + 2]
				};
				*/

				vertex.normal = { 0, 1, 0 };

				vertex.texCoord = {
					attrib.texcoords[2 * (int)index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * (int)index.texcoord_index + 1]
				};

				vertex.color = { 1.0f, 1.0f, 1.0f };

				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertex);
				}

				indices.push_back(uniqueVertices[vertex]);
			}
		}

		normailze(0.7f, glm::vec3(0, 0, 0));
		instanceData_static.push_back({ glm::vec3(trans, 0, 0) });
		instanceData_static.push_back({ glm::vec3(0, trans, 0) });
		instanceData_static.push_back({ glm::vec3(0, 0, trans) });

		instanceData_dynamic.push_back({ glm::identity<glm::mat4>() });
		instanceData_dynamic.push_back({ glm::identity<glm::mat4>() });
		instanceData_dynamic.push_back({ glm::identity<glm::mat4>() });
	}
private:
	void normailze(float scale, const glm::vec3 &shift) {
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

	void initAccelarationStructures() {

	}
};

// composed of individual meshes
class Model {
public:
	VkImageView textureImageView;
	VkSampler textureSampler;
	
	Model() {
		for (const auto& texturePath : TEXTURE_PATHS)
			textureCache.push_back(Image2d(texturePath));
		fixTextureCache();

		int ctr = -1;
		for (const auto& modelPath : MODEL_PATHS)
			meshes.push_back(Mesh(modelPath.c_str(), (float)2*ctr++));
		
		updateGlobalBuffers();
	}

	static std::vector<VkVertexInputBindingDescription> getBindingDescription() {
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

	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
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
		attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[4].offset = offsetof(InstanceData_static, translate);

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

	void updateMeshData() {
		memcpy(mappedDynamicInstancePtr, instanceData_dynamic.data(), sizeof(instanceData_dynamic[0]) * instanceData_dynamic.size());
	}

	void cmdTransferData(const VkCommandBuffer &cmdBuffer) {
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

	void cmdDraw(const VkCommandBuffer& cmdBuffer) {
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

	void createBuffers(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
		createVertexBuffer(device, allocator, queue, commandPool);
		createStaticInstanceBuffer(device, allocator, queue, commandPool);
		createDynamicInstanceBuffer(device, allocator, queue, commandPool);
		createIndexBuffer(device, allocator, queue, commandPool);
		createIndirectCmdBuffer(device, allocator, queue, commandPool);
		createTextureImage(physicalDevice, device, allocator, queue, commandPool);
		createTextureImageView(device);
		createTextureSampler(device);
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator) {
		vkDestroySampler(device, textureSampler, nullptr);
		vkDestroyImageView(device, textureImageView, nullptr);

		vmaDestroyImage(allocator, textureImage, textureImageAllocation);

		vmaDestroyBuffer(allocator, indexBuffer, indexBufferAllocation);
		vmaDestroyBuffer(allocator, vertexBuffer, vertexBufferAllocation);
		vmaDestroyBuffer(allocator, staticInstanceBuffer, staticInstanceBufferAllocation);
		vmaDestroyBuffer(allocator, dynamicInstanceBuffer, dynamicInstanceBufferAllocation);
		vmaUnmapMemory(allocator, dynamicInstanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, dynamicInstanceStagingBuffer, dynamicInstanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, indirectCmdBuffer, indirectCmdBufferAllocation);
	}
private:
	std::vector<Mesh> meshes; // ideally store unique meshes
	std::vector<Vertex> vertices; // concatenate vertices from all meshes
	std::vector<uint32_t> indices;  // indeces into the above global vertices
	std::vector<InstanceData_static> instanceData_static; // concatenate instances from all meshes. Note each mesh can have multiple instances. 
	std::vector<InstanceData_dynamic> instanceData_dynamic;  // concatenate instances from all meshes. Note each mesh can have multiple instances.
	void *mappedDynamicInstancePtr;
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands; // Its size is meshes.size().
	std::vector<Image2d> textureCache; // ideally only store unique textures.

	VkBuffer vertexBuffer;
	VmaAllocation vertexBufferAllocation;
	VkBuffer staticInstanceBuffer;
	VmaAllocation staticInstanceBufferAllocation;
	VkBuffer dynamicInstanceBuffer;
	VmaAllocation dynamicInstanceBufferAllocation;
	VkBuffer dynamicInstanceStagingBuffer;
	VmaAllocation dynamicInstanceStagingBufferAllocation;
	VkBuffer indexBuffer;
	VmaAllocation indexBufferAllocation;
	VkBuffer indirectCmdBuffer;
	VmaAllocation indirectCmdBufferAllocation;

	uint32_t mipLevels;
	VkImage textureImage;
	VmaAllocation textureImageAllocation;

	/** RTX Data **/
	TopLevelAccelerationStructure as_topLevel;

	void updateGlobalBuffers() {
		indirectCommands.clear();

		size_t meshVertexOffset = 0;
		size_t indexOffset = 0;
		size_t instanceOffset = 0;
				
		for (const auto &mesh : meshes) {
			// Update global buffer from individual meshes
			vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
			indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
			instanceData_static.insert(instanceData_static.end(), mesh.instanceData_static.begin(), mesh.instanceData_static.end());
			instanceData_dynamic.insert(instanceData_dynamic.end(), mesh.instanceData_dynamic.begin(), mesh.instanceData_dynamic.end());
			for (size_t i = indexOffset; i < indices.size(); i++)
				indices[i] += static_cast<uint32_t>(meshVertexOffset);

			// Create on indirect command for each mesh in the scene
			VkDrawIndexedIndirectCommand indirectCmd = {};
			indirectCmd.firstInstance = static_cast<uint32_t>(instanceOffset); // Tells Vulkan which index of the instanceData (Static and Dynamic) to look at
			indirectCmd.instanceCount = static_cast<uint32_t>(mesh.instanceData_static.size());
			indirectCmd.firstIndex = static_cast<uint32_t>(indexOffset);
			indirectCmd.indexCount = static_cast<uint32_t>(mesh.indices.size());

			indirectCommands.push_back(indirectCmd);

			indexOffset += mesh.indices.size();
			meshVertexOffset += mesh.vertices.size();
			instanceOffset += mesh.instanceData_static.size(); // get number of instances per mesh

			if (mesh.instanceData_static.size() != mesh.instanceData_dynamic.size())
				throw std::runtime_error("Cannot determine instance count");
		}
	}

	void fixTextureCache() {
		if (textureCache.empty())
			textureCache.push_back(Image2d());
		else {
			// All images must have same image format and size. We can relax this restriction to having same aspect ratio and rescaling the
			// images to the largest one.
			
			// for now let's just throw an error when size and format are not same.
			// TODO : Check if the images have same aspect ratio and format. Upscale all images to the size of the largest one.
			VkFormat desiredFormat = textureCache[0].format;
			uint32_t desiredWidth = textureCache[0].width;
			uint32_t desiredHeight = textureCache[0].height;
			for (const auto &image : textureCache)
				if (image.format != desiredFormat || image.width != desiredWidth || image.height != desiredHeight)
					throw std::runtime_error("Size and format for all texture images must be same.");
		}
	}

	void createVertexBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
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
			throw std::runtime_error("Failed to create staging buffer for vertex buffer!");

		void* data;
		vmaMapMemory(allocator, stagingBufferAllocation, &data);
		memcpy(data, vertices.data(), (size_t)bufferSize);
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &vertexBuffer, &vertexBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create vertex buffer!");

		copyBuffer(device, queue, commandPool, stagingBuffer, vertexBuffer, bufferSize);

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
	}

	void createIndexBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
		VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

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
		memcpy(data, indices.data(), (size_t)bufferSize);
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &indexBuffer, &indexBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create index buffer!");

		copyBuffer(device, queue, commandPool, stagingBuffer, indexBuffer, bufferSize);

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
	}

	void createStaticInstanceBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
		VkDeviceSize bufferSize = sizeof(instanceData_static[0]) * instanceData_static.size();
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
			throw std::runtime_error("Failed to create staging buffer for static instances!");

		void* data;
		vmaMapMemory(allocator, stagingBufferAllocation, &data);
		memcpy(data, instanceData_static.data(), (size_t)bufferSize);
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &staticInstanceBuffer, &staticInstanceBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create static instance buffer!");

		copyBuffer(device, queue, commandPool, stagingBuffer, staticInstanceBuffer, bufferSize);

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
	}

	void createDynamicInstanceBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
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

	void createIndirectCmdBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
		VkDeviceSize bufferSize = indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand);

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
			throw std::runtime_error("Failed to create staging buffer for indirect command buffer!");

		void* data;
		vmaMapMemory(allocator, stagingBufferAllocation, &data);
		memcpy(data, indirectCommands.data(), (size_t)bufferSize);
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &indirectCmdBuffer, &indirectCmdBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create indirect command buffer!");

		copyBuffer(device, queue, commandPool, stagingBuffer, indirectCmdBuffer, bufferSize);

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
	}

	void createTextureImage(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool) {
		mipLevels = textureCache[0].mipLevels();
		
		uint32_t bufferSize = 0;
		for (const auto &image : textureCache)
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
		for (auto &image : textureCache) {
			byte *start = static_cast<byte *>(data);
			memcpy(&start[bufferOffset], image.pixels, static_cast<size_t>(image.size()));
			bufferOffset += static_cast<size_t>(image.size());
			image.cleanUp();
		}
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		bufferOffset = 0;
		for (uint32_t layer = 0; layer < textureCache.size(); layer++) {
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = textureCache[0].width;
			bufferCopyRegion.imageExtent.height = textureCache[0].height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = bufferOffset;

			bufferCopyRegions.push_back(bufferCopyRegion);

			// Increase offset into staging buffer for next level / face
			bufferOffset += textureCache[layer].size();
		}

		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = textureCache[0].width;
		imageCreateInfo.extent.height = textureCache[0].height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = static_cast<uint32_t>(textureCache.size());
		imageCreateInfo.format = textureCache[0].format;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &textureImage, &textureImageAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create texture image!");

		transitionImageLayout(device, queue, commandPool, textureImage, textureCache[0].format, 
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, static_cast<uint32_t>(textureCache.size()));
		VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		endSingleTimeCommands(device, queue, commandPool, commandBuffer);
		//transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);

		generateMipmaps(physicalDevice, device, queue, commandPool, textureImage, 
			textureCache[0].format, textureCache[0].width, textureCache[0].height, mipLevels, static_cast<uint32_t>(textureCache.size()));
	}

	void generateMipmaps(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
		VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels, uint32_t layerCount) {
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

	void createTextureImageView(const VkDevice& device) {
		textureImageView = createImageView(device, textureImage, textureCache[0].format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, static_cast<uint32_t>(textureCache.size()));
	}

	void createTextureSampler(const VkDevice& device) {
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

		if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
			throw std::runtime_error("failed to create texture sampler!");
		}
	}
};

#undef VERTEX_BINDING_ID
#undef STATIC_INSTANCE_BINIDING_ID
#undef DYNAMIC_INSTANCE_BINDING_ID