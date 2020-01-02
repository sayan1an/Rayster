#pragma once

#include <vector>
#include "model.hpp"
#include "helper.h"

class DiscretePdf
{
public:
	void add(float value)
	{
		float cumSum = dCdf[dCdf.size() - 1] + value;
		dCdf.push_back(cumSum);

		dCdfNormalized.clear();

		for (const float entry : dCdf)
			dCdfNormalized.push_back(entry / cumSum);
	}

	VkDescriptorBufferInfo getCdfDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = dCdfBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkDescriptorBufferInfo getCdfNormDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = dCdfNormBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}
	
	void createBuffers(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
	{	
		CHECK(dCdf.size() > 1, "DiscretePdf: Cannot create buffer.");
		CHECK(dCdfNormalized.size() > 1, "DiscretePdf: Cannot create buffer.");

		createBuffer(device, allocator, queue, commandPool, dCdfNormBuffer, dCdfNormBufferAllocation, sizeof(dCdfNormalized[0]) * dCdfNormalized.size(), dCdfNormalized.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		createBuffer(device, allocator, queue, commandPool, dCdfBuffer, dCdfBufferAllocation, sizeof(dCdf[0]) * dCdf.size(), dCdf.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	}

	void cleanUp(const VmaAllocator& allocator)
	{
		vmaDestroyBuffer(allocator, dCdfNormBuffer, dCdfNormBufferAllocation);
		vmaDestroyBuffer(allocator, dCdfBuffer, dCdfBufferAllocation);
	}

	uint32_t size()
	{	
		return static_cast<uint32_t>(dCdfNormalized.size() - 1);
	}

	DiscretePdf()
	{
		dCdf.reserve(100);
		dCdfNormalized.reserve(100);
		dCdf.push_back(0.0f);
		dCdfNormalized.push_back(0.0f);
	}
	
private:
	std::vector<float> dCdf;
	std::vector<float> dCdfNormalized;
	
	VkBuffer dCdfNormBuffer;
	VmaAllocation dCdfNormBufferAllocation;

	VkBuffer dCdfBuffer;
	VmaAllocation dCdfBufferAllocation;
};

class AreaLightSources
{
public:
	DiscretePdf dPdf;

	void init(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const Model *_model)
	{
		model = _model;

		CHECK(model->instanceData_static.size() > 0, "AreaLightSources::init() - Model (static instances) are not initialized");
		CHECK(model->materials.size() > 0, "AreaLightSources::init() - Model (Materials) are not initialized");
				
		uint32_t globalInstanceIdx = 0;
		for (const auto& instance : model->instanceData_static) {
			
			const Mesh* mesh = nullptr;
			
			if (instance.data.z > 0 && instance.data.x >= 0xffffffff) {
				uint32_t meshIdx = model->meshPointers[globalInstanceIdx];
				mesh = model->meshes[meshIdx];
			}
			else if (instance.data.z > 0) {
				uint32_t materialIdx = instance.data.x;
				Material mat = model->materials[materialIdx];

				if (mat.materialType == AREA) {
					uint32_t meshIdx = model->meshPointers[globalInstanceIdx];
					mesh = model->meshes[meshIdx];
				}
			}
			
			glm::mat3 l2w = glm::mat3(model->instanceData_dynamic[globalInstanceIdx].model);

			for (uint32_t i = 0, primitiveIdx = 0; mesh != nullptr && i < mesh->indices.size(); i += 3, primitiveIdx++) {
				dPdf.add(computeArea(l2w * (mesh->vertices[mesh->indices[i]].pos),
					l2w * (mesh->vertices[mesh->indices[i + 1]].pos),
					l2w * (mesh->vertices[mesh->indices[i + 2]].pos)));
				triangleIdxs.push_back(globalInstanceIdx << 16 | primitiveIdx);
			}

			globalInstanceIdx++;
		}

		lightVertices.resize(triangleIdxs.size() * 3);

		createBuffer(device, allocator, queue, commandPool);
		dPdf.createBuffers(physicalDevice, device, allocator, queue, commandPool);
	}

	void cmdTransferData(const VkCommandBuffer& cmdBuffer)
	{
		VkBufferCopy copyRegion = {};
		copyRegion.size = sizeof(lightVertices[0]) * lightVertices.size();

		vkCmdCopyBuffer(cmdBuffer, lightVerticesStagingBuffer, lightVerticesBuffer, 1, &copyRegion);

		VkBufferMemoryBarrier bufferMemoryBarrier = {};
		bufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferMemoryBarrier.buffer = lightVerticesBuffer;
		bufferMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		bufferMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(
			cmdBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
			0,
			0, nullptr,
			1, &bufferMemoryBarrier,
			0, nullptr);
	}

	void updateData()
	{	
		uint32_t lightIndex = 0;
		for (uint32_t triIdx : triangleIdxs) {
			uint32_t instanceIdx = triIdx >> 16;
			uint32_t primitiveIdx = 3 * (triIdx & 0xffff);

			uint32_t meshIdx = model->meshPointers[instanceIdx];
			const Mesh* mesh = model->meshes[meshIdx];
			
			//std::cout << determinant(model->instanceData_dynamic[instanceIdx].model) << std::endl;

			lightVertices[lightIndex] = model->instanceData_dynamic[instanceIdx].model * glm::vec4(mesh->vertices[mesh->indices[primitiveIdx]].pos, 1.0f);
			lightVertices[lightIndex + 1] = model->instanceData_dynamic[instanceIdx].model * glm::vec4(mesh->vertices[mesh->indices[primitiveIdx + 1]].pos, 1.0f);
			lightVertices[lightIndex + 2] = model->instanceData_dynamic[instanceIdx].model * glm::vec4(mesh->vertices[mesh->indices[primitiveIdx + 2]].pos, 1.0f);
			
			lightIndex += 3;
		}

		memcpy(mappedLightVerticesPtr, lightVertices.data(), sizeof(lightVertices[0]) * lightVertices.size());
	}

	void cleanUp(const VmaAllocator& allocator)
	{	
		vmaDestroyBuffer(allocator, lightVerticesBuffer, lightVerticesBufferAllocation);
		vmaUnmapMemory(allocator, lightVerticesStagingBufferAllocation);
		vmaDestroyBuffer(allocator, lightVerticesStagingBuffer, lightVerticesStagingBufferAllocation);
		dPdf.cleanUp(allocator);
	}

	VkDescriptorBufferInfo getDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = lightVerticesBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

private:
	const Model* model;
	std::vector<uint32_t> triangleIdxs; // MSB 16 bit - instance index, LSB 16 bit primitive index
	std::vector<glm::vec4> lightVertices;

	VkBuffer lightVerticesStagingBuffer;
	VmaAllocation lightVerticesStagingBufferAllocation;
	void* mappedLightVerticesPtr;

	VkBuffer lightVerticesBuffer;
	VmaAllocation lightVerticesBufferAllocation;

	inline float computeArea(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2)
	{	
		return glm::length(glm::cross(v0 - v1, v0 - v2)) * 0.5f;
	}

	void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
	{
		VkDeviceSize bufferSize = sizeof(lightVertices[0]) * lightVertices.size();

		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

		VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &lightVerticesStagingBuffer, &lightVerticesStagingBufferAllocation, nullptr),
			"AreaLightSources: Failed to create staging buffer for light vertices buffer!");

		vmaMapMemory(allocator, lightVerticesStagingBufferAllocation, &mappedLightVerticesPtr);

		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &lightVerticesBuffer, &lightVerticesBufferAllocation, nullptr),
			"AreaLightSources: Failed to create staging buffer for light vertices buffer!");
	}
};