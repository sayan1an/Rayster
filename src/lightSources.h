#pragma once

#include <vector>
#include "model.hpp"
#include "helper.h"
#include "random.h"

class DiscretePdf
{
public:
	void add(float value)
	{	
		if (value > max)
			max = value;
		if (value < min)
			min = value;
		
		float cumSum = dCdf[dCdf.size() - 1] + value;
		dCdf.push_back(cumSum);
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
	
	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
	{	
		CHECK(dCdf.size() > 1, "DiscretePdf: Cannot create buffer.");
		
		convertDcdf2LightIndex();

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
		max = 0;
		min = std::numeric_limits<float>::max();
	}
	
private:
	std::vector<float> dCdf;
	std::vector<float> dCdfNormalized;
	std::vector<uint32_t> emitterIndexMap; // dcdf stored as a table of light indices. i.e. map from (0 to 1) -> light index 
	
	VkBuffer dCdfNormBuffer;
	VmaAllocation dCdfNormBufferAllocation;

	VkBuffer dCdfBuffer;
	VmaAllocation dCdfBufferAllocation;

	float max;
	float min;

	// fill out emitterIndex
	void convertDcdf2LightIndex(float badSectorTolrance = 0.01, uint32_t subSample = 5, float storageSizeInMB = 5)
	{	
		// convert even to odd
		subSample += !(subSample & 1);

		dCdfNormalized.clear();

		// Normalize dCdf
		for (const float entry : dCdf)
			dCdfNormalized.push_back(entry / dCdf.back());

		min /= dCdf.back();
		max /= dCdf.back();

		// initial guess for grid inteval 
		uint32_t nElements = static_cast<uint32_t>(2.0f / max);

		// Optimize the size of emitterIndexMap
		// Increase nElements such that number of bad sectors is less than badSectorTolrance
		// Bad sector is defined as a bin containing more than one index.
		bool cond = false;
		do {
			float delta = 1.0f / nElements;
			uint32_t lastIndex = 0;
			cond = false;
			uint32_t badSectors = 0;
			for (uint32_t i = 0; i < nElements; i++) {
				float intervalEnd = (i + 1) * delta;
				uint32_t index = searchDpdf(intervalEnd, lastIndex);
				uint32_t numIndecesInBin = index - lastIndex + 1;
				badSectors += (numIndecesInBin > 1 ? 1 : 0);
				lastIndex = index;
			}

			if (badSectors > badSectorTolrance * nElements) {
				cond = true;
				nElements *= 2;
			}
		} while (cond);

		nElements = std::min(nElements, static_cast<uint32_t>(1.0f / min));
		nElements = std::min(nElements, static_cast<uint32_t>(storageSizeInMB * 1024 * 1024 / sizeof(uint32_t)));
		
		//std::cout << nElements << " " <<  static_cast<uint32_t>(1.0f/min) << std::endl;
		emitterIndexMap.resize(nElements);

		// grid inteval
		float delta = 1.0f / nElements;

		uint32_t lastIndex = 0;
		std::unordered_map<uint32_t, uint32_t> hash; // store the subsampled items and find the most common item in a given interval
		RandomGenerator rand;

		// Populate emitterIndexMap
		for (uint32_t i = 0; i < nElements; i++) {
			float start = i * delta;
			float end = (i + 1) * delta;

			hash.clear();
			uint32_t minIndex = std::numeric_limits<uint32_t>::max();

			// sample the interval (start, end) and find the most common light instance
			for (uint32_t j = 0; j < subSample; j++) {
				float subRand = rand.getNextUint32_t() / float(0xffffffff);
				float rand = start * subRand + end * (1.0f - subRand);

				uint32_t index = searchDpdf(rand, lastIndex);
				hash[index]++;
				if (index < minIndex)
					minIndex = index;
			}
						
			// find the max frequency 
			uint32_t max_count = 0, mostCommonElement = minIndex;
			for (auto k : hash) {
				if (max_count < k.second) {
					mostCommonElement = k.first;
					max_count = k.second;
				}
			}

			emitterIndexMap[i] = mostCommonElement;
			
			lastIndex = minIndex >= 2 ? minIndex - 2 : 0;
		}

		//for (auto i : emitterIndexMap)
			//std::cout << i << " ";
		//std::cout << std::endl;
	}

	uint32_t searchDpdf(float rand, uint32_t startIdx) 
	{	
		uint32_t index = startIdx;
		for (uint32_t j = startIdx; j < dCdfNormalized.size(); j++)
			index = dCdfNormalized[j] < rand ? j : index;

		return index;
	}
};

class AreaLightSources
{
public:
	DiscretePdf dPdf;

	void init(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const Model *_model)
	{
		model = _model;

		CHECK(model->instanceData_static.size() > 0, "AreaLightSources::init() - Model (static instances) are not initialized");
		CHECK(model->materials.size() > 0, "AreaLightSources::init() - Model (Materials) are not initialized");
				
		uint32_t globalInstanceIdx = 0;
		uint32_t areaLightPrimitiveOffsetCounter = 0;
		for (const auto& instance : model->instanceData_static) {
			
			const Mesh* mesh = nullptr;
			
			// Check whether the instance is a light source and material information is per-vertex
			// Note that if the material is per vertex, then all vertex should be of type light source. Checked
			// in model.hpp when adding an instance
			if (instance.data.z > 0 && instance.data.x >= 0xffffffff) {
				uint32_t meshIdx = model->meshPointers[globalInstanceIdx];
				mesh = model->meshes[meshIdx];
			} // Check whether the instance is a light source and material information is per-instance
			else if (instance.data.z > 0) {
				uint32_t materialIdx = instance.data.x;
				Material mat = model->materials[materialIdx];

				if (mat.materialType == AREA) {
					uint32_t meshIdx = model->meshPointers[globalInstanceIdx];
					mesh = model->meshes[meshIdx];
				}
			}
			
			glm::mat4 l2w = model->instanceData_dynamic[globalInstanceIdx].model;
			glm::mat3 l2w_3 = glm::mat3(l2w);

			if (mesh != nullptr) {
				glm::vec4 center = l2w * glm::vec4(glm::vec3(mesh->boundingSphere), 1.0f);
				// multiply radius with largest eigenvalue of orthogonal matrix
				center.w = std::max(std::max(glm::length(l2w_3[0]), glm::length(l2w_3[1])), glm::length(l2w_3[2])) * mesh->boundingSphere.w;
				boundingSpheres.push_back(center);
				boundingSphereInstanceIndexes.push_back(globalInstanceIdx);
			}

			CHECK(globalInstanceIdx <= 0xffff, "AreaLightSources : Number of globalInstances must be <= 0xffff.");
			for (uint32_t i = 0, primitiveIdx = 0; mesh != nullptr && i < mesh->indices.size(); i += 3, primitiveIdx++) {
				dPdf.add(computeArea(l2w_3 * (mesh->vertices[mesh->indices[i]].pos),
					l2w_3 * (mesh->vertices[mesh->indices[i + 1]].pos),
					l2w_3 * (mesh->vertices[mesh->indices[i + 2]].pos)));
				CHECK(primitiveIdx <= 0xffff, "AreaLightSources : Number of primitives must be <= 0xffff.");
				triangleIdxs.push_back(globalInstanceIdx << 16 | primitiveIdx);
			}

			// Verify whether primitive offsets for the area emitters are correct
			if (mesh != nullptr) {
				CHECK(areaLightPrimitiveOffsetCounter == (instance.data.z >> 8), "AreaLightSources: areaLight primitive offsets are incorrect.");
				areaLightPrimitiveOffsetCounter += static_cast<uint32_t>(mesh->indices.size());
			}
			
			globalInstanceIdx++;
		}

		lightVertices.resize(triangleIdxs.size() * 3);

		mptrLightVertices = createBuffer(device, allocator, queue, commandPool, lightVerticesBuffer, lightVerticesBufferAllocation, lightVerticesStagingBuffer, lightVerticesStagingBufferAllocation, sizeof(lightVertices[0]) * lightVertices.size());
		mptrBoundingSpheres = createBuffer(device, allocator, queue, commandPool, bndSphBuffer, bndSphBufferAllocation, bndSphStagingBuffer, bndSphStagingBufferAllocation, sizeof(boundingSpheres[0]) * boundingSpheres.size());
		dPdf.createBuffers(device, allocator, queue, commandPool);

		// create the TLAS solely for light sources
		// We use reference the same BLAS from model.hpp for light meshes.
		as_topLevel.create(device, allocator, static_cast<uint32_t>(boundingSpheres.size()), false);
		createBuffer(device, allocator, queue, commandPool, lightInstanceToGlobalInstanceBuffer, lightInstanceToGlobalInstanceAllocation, boundingSphereInstanceIndexes.size() * sizeof(boundingSphereInstanceIndexes[0]), boundingSphereInstanceIndexes.data(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	}

	void cmdTransferData(const VkCommandBuffer& cmdBuffer)
	{	
		as_topLevel.cmdBuild(cmdBuffer, static_cast<uint32_t>(boundingSpheres.size()), false);

		VkBufferCopy copyRegion = {};
		copyRegion.size = sizeof(lightVertices[0]) * lightVertices.size();
		vkCmdCopyBuffer(cmdBuffer, lightVerticesStagingBuffer, lightVerticesBuffer, 1, &copyRegion);
		copyRegion.size = sizeof(boundingSpheres[0]) * boundingSpheres.size();
		vkCmdCopyBuffer(cmdBuffer, bndSphStagingBuffer, bndSphBuffer, 1, &copyRegion);

		std::array<VkBufferMemoryBarrier, 2> bufferMemoryBarriers = {};
		bufferMemoryBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferMemoryBarriers[0].buffer = lightVerticesBuffer;
		bufferMemoryBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		bufferMemoryBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		bufferMemoryBarriers[1] = bufferMemoryBarriers[0];
		bufferMemoryBarriers[1].buffer = bndSphBuffer;
	
		vkCmdPipelineBarrier(
			cmdBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
			0,
			0, nullptr,
			static_cast<uint32_t>(bufferMemoryBarriers.size()), bufferMemoryBarriers.data(),
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
			
			// also save un normalized normal as it also gives the area i.e area = length(normal) * 0.5
			glm::vec3 normal = glm::cross(glm::vec3(lightVertices[lightIndex] - lightVertices[lightIndex + 1]), glm::vec3(lightVertices[lightIndex] - lightVertices[lightIndex + 2]));
			lightVertices[lightIndex].w = normal.x;
			lightVertices[lightIndex + 1].w = normal.y;
			lightVertices[lightIndex + 2].w = normal.z;
			
			lightIndex += 3;
		}

		uint32_t boundingSphereIdx = 0;
		tlas_instanceData.clear();
		for (uint32_t bndSphInstIdx : boundingSphereInstanceIndexes) {
			uint32_t meshIdx = model->meshPointers[bndSphInstIdx];
			const Mesh* mesh = model->meshes[meshIdx];
			glm::mat4 l2w = model->instanceData_dynamic[bndSphInstIdx].model;
			glm::vec4 center = l2w * glm::vec4(glm::vec3(mesh->boundingSphere), 1.0f);
			glm::mat3 l2w_3 = glm::mat3(l2w);
			// multiply radius with largest eigenvalue of orthogonal matrix
			center.w = std::max(std::max(glm::length(l2w_3[0]), glm::length(l2w_3[1])), glm::length(l2w_3[2])) * mesh->boundingSphere.w;
			boundingSpheres[boundingSphereIdx] = center;
			
			
			TopLevelAccelerationStructureData data;
			// Copy first three rows of transformation matrix of each instance
			glm::mat4 modelTrans = glm::transpose(l2w);
			memcpy(data.transform, &modelTrans, sizeof(data.transform));
			data.instanceId = boundingSphereIdx;
			data.mask = 0xff;
			data.instanceOffset = 0; // Since this is used to determine hit group index compuation, this may change
			data.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
			data.blasHandle = mesh->as_bottomLevel.handle;
			tlas_instanceData.push_back(data);
						
			boundingSphereIdx++;
		}

		CHECK_DBG_ONLY(boundingSphereIdx == boundingSpheres.size(),
			"LightSources: Number of instances for Top Level Accelaration structure should match dynamic instance data count.");

		as_topLevel.updateInstanceData(tlas_instanceData);

		memcpy(mptrLightVertices, lightVertices.data(), sizeof(lightVertices[0]) * lightVertices.size());
		memcpy(mptrBoundingSpheres, boundingSpheres.data(), sizeof(boundingSpheres[0]) * boundingSpheres.size());
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
	{	
		vmaDestroyBuffer(allocator, lightVerticesBuffer, lightVerticesBufferAllocation);
		vmaUnmapMemory(allocator, lightVerticesStagingBufferAllocation);
		vmaDestroyBuffer(allocator, lightVerticesStagingBuffer, lightVerticesStagingBufferAllocation);

		vmaDestroyBuffer(allocator, bndSphBuffer, bndSphBufferAllocation);
		vmaUnmapMemory(allocator, bndSphStagingBufferAllocation);
		vmaDestroyBuffer(allocator, bndSphStagingBuffer, bndSphStagingBufferAllocation);

		vmaDestroyBuffer(allocator, lightInstanceToGlobalInstanceBuffer, lightInstanceToGlobalInstanceAllocation);

		as_topLevel.cleanUp(device, allocator);

		dPdf.cleanUp(allocator);
	}

	uint32_t getNumSources() const 
	{
		return static_cast<uint32_t>(boundingSpheres.size());
	}

	VkDescriptorBufferInfo getVerticesDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = lightVerticesBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkDescriptorBufferInfo getBndSphDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = bndSphBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkDescriptorBufferInfo getLightInstanceDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = lightInstanceToGlobalInstanceBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkWriteDescriptorSetAccelerationStructureNV getDescriptorTlas() const
	{
		return as_topLevel.getDescriptorTlasInfo();
	}

private:
	const Model* model;
	std::vector<uint32_t> triangleIdxs; // MSB 16 bit - instance index, LSB 16 bit primitive index
	std::vector<glm::vec4> lightVertices;
	std::vector<glm::vec4> boundingSpheres;
	std::vector<uint32_t> boundingSphereInstanceIndexes;
	
	VkBuffer lightVerticesStagingBuffer;
	VmaAllocation lightVerticesStagingBufferAllocation;
	void* mptrLightVertices;
	VkBuffer lightVerticesBuffer;
	VmaAllocation lightVerticesBufferAllocation;

	VkBuffer bndSphStagingBuffer;
	VmaAllocation  bndSphStagingBufferAllocation;
	void* mptrBoundingSpheres;
	VkBuffer bndSphBuffer;
	VmaAllocation bndSphBufferAllocation;

	inline float computeArea(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2)
	{	
		return glm::length(glm::cross(v0 - v1, v0 - v2)) * 0.5f;
	}

	// Top level AS for meshes containing light sources
	TopLevelAccelerationStructure as_topLevel;
	std::vector<TopLevelAccelerationStructureData> tlas_instanceData;

	// map gl_InstanceId to global instance index
	VkBuffer lightInstanceToGlobalInstanceBuffer;
	VmaAllocation lightInstanceToGlobalInstanceAllocation;
};