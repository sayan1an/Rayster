#include "accelerationStructure.h"

void BottomLevelAccelerationStructure::create(const VkDevice& device, const VmaAllocator& allocator, const std::vector<VkGeometryNV>& geometries, bool allowUpdate)
{
	initProcAddress(device);

	VkAccelerationStructureInfoNV accelerationStructureInfo{};
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
	accelerationStructureInfo.instanceCount = 0;
	accelerationStructureInfo.geometryCount = static_cast<uint32_t>(geometries.size());
	accelerationStructureInfo.pGeometries = geometries.data();
	accelerationStructureInfo.flags = allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV : 0;

	this->allowUpdate = allowUpdate;

	VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
	accelerationStructureCreateInfo.info = accelerationStructureInfo;
	VK_CHECK_DBG_ONLY(vkCreateAccelerationStructureNV(device, &accelerationStructureCreateInfo, nullptr, &accelerationStructure),
		"AccelarationStructure: failed to create bottom level accelaration structure!");

	// Find memory requirements for accelaration structure object
	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo{};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.accelerationStructure = accelerationStructure;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;

	VkMemoryRequirements2 memoryRequirements2{};
	vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);

	// Allocate memory for accelaration structure object
	// https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63
	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;

	VmaAllocationInfo allocInfo = {};
	VK_CHECK_DBG_ONLY(vmaAllocateMemory(allocator, &memoryRequirements2.memoryRequirements, &allocCreateInfo, &accelerationStructureAllocation, &allocInfo),
		"AccelarationStructure: failed to allocate memory for bottom level accelaration structure!");

	// Bind Accelaration structure with its memory
	VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo{};
	accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
	accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
	accelerationStructureMemoryInfo.memory = allocInfo.deviceMemory;
	accelerationStructureMemoryInfo.memoryOffset = allocInfo.offset;
	VK_CHECK_DBG_ONLY(vkBindAccelerationStructureMemoryNV(device, 1, &accelerationStructureMemoryInfo),
		"AccelarationStructure: failed to bind memory for bottom level accelaration structure!");
		
	// Get a handle for the acceleration structure
	VK_CHECK_DBG_ONLY(vkGetAccelerationStructureHandleNV(device, accelerationStructure, sizeof(uint64_t), &handle),
		"AccelarationStructur: failed to retrive handle for bottom level accelaration structure!");

	// Find memory requirements for accelaration structure build scratch
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
	memoryRequirements2 = {};
	vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
	size_t scratchSize = memoryRequirements2.memoryRequirements.size;

	// Find memory requirements for accelaration structure update scratch
	if (allowUpdate) {
		uint32_t buildScratchType = memoryRequirements2.memoryRequirements.memoryTypeBits;
		memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV;
		memoryRequirements2 = {};
		vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
		scratchSize = scratchSize > memoryRequirements2.memoryRequirements.size ? scratchSize : memoryRequirements2.memoryRequirements.size;

		VK_CHECK_DBG_ONLY(buildScratchType == memoryRequirements2.memoryRequirements.memoryTypeBits,
			"AccelartionStructure: Build scratch and update scratch type do not match for bottom level AS!");
	}

	// Create scratch buffer
	allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = scratchSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// Allocate memory and bind it to the buffer
	VK_CHECK_DBG_ONLY(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &scratchBuffer, &scratchBufferAllocation, &allocInfo),
		"AccelarationStructure: failed to allocate scratch buffer for bottom level accelaration structure!");
}

void BottomLevelAccelerationStructure::cmdBuild(const VkCommandBuffer& cmdBuf, const std::vector<VkGeometryNV>& geometries, bool update)
{
	CHECK_DBG_ONLY(!update || allowUpdate == update,
		"AccelartionStructure: Partial rebuild for bottom level accelaration structure is not allowed. First create the BLAS with appropriate flag!");

	// Build the actual bottom-level acceleration structure
	VkAccelerationStructureInfoNV buildInfo = {};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	buildInfo.pNext = nullptr;
	buildInfo.flags = allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV : 0;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
	buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
	buildInfo.pGeometries = geometries.data();

	vkCmdBuildAccelerationStructureNV(cmdBuf, &buildInfo, VK_NULL_HANDLE, 0, update,
		accelerationStructure, update ? accelerationStructure : VK_NULL_HANDLE, scratchBuffer,
		0);

	// Wait for the builder to complete by setting a barrier on the resulting buffer. This is
	// particularly important as the construction of the top-level hierarchy may be called right
	// afterwards, before executing the command list.
	VkMemoryBarrier memoryBarrier;
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.pNext = nullptr;
	memoryBarrier.srcAccessMask =
		VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
	memoryBarrier.dstAccessMask =
		VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

	vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier,
		0, nullptr, 0, nullptr);
}

void TopLevelAccelerationStructure::create(const VkDevice& device, const VmaAllocator& allocator, const uint32_t instanceCount, bool allowUpdate)
{
	initProcAddress(device);

	VkAccelerationStructureInfoNV accelerationStructureInfo{};
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
	accelerationStructureInfo.instanceCount = instanceCount;
	accelerationStructureInfo.geometryCount = 0;
	accelerationStructureInfo.flags = allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV : 0;

	this->allowUpdate = allowUpdate;

	VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
	accelerationStructureCreateInfo.info = accelerationStructureInfo;
	VK_CHECK_DBG_ONLY(vkCreateAccelerationStructureNV(device, &accelerationStructureCreateInfo, nullptr, &accelerationStructure),
		"AccelerationStructure: failed to create top level accelaration structure!");

	// Find memory requirements for accelaration structure object
	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo{};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.accelerationStructure = accelerationStructure;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;

	VkMemoryRequirements2 memoryRequirements2{};
	vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
	
	// Allocate memory for accelaration structure object
	// https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63
	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;
	
	VmaAllocationInfo allocInfo = {};
	VK_CHECK_DBG_ONLY(vmaAllocateMemory(allocator, &memoryRequirements2.memoryRequirements, &allocCreateInfo, &accelerationStructureAllocation, &allocInfo),
		"AccelarationStructure: failed to allocate memory for bottom level accelaration structure!");

	// Bind Accelaration structure with its memory
	VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo{};
	accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
	accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
	accelerationStructureMemoryInfo.memory = allocInfo.deviceMemory;
	accelerationStructureMemoryInfo.memoryOffset = allocInfo.offset;
	
	VK_CHECK_DBG_ONLY(vkBindAccelerationStructureMemoryNV(device, 1, &accelerationStructureMemoryInfo),
		"AccelarationStructure: failed to bind memory for top level accelaration structure!");
	
	// Find memory requirements for accelaration structure build scratch
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
	memoryRequirements2 = {};
	vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
	VkDeviceSize scratchSize = memoryRequirements2.memoryRequirements.size;

	// Find memory requirements for accelaration structure update scratch
	if (allowUpdate) {
		uint32_t buildScratchType = memoryRequirements2.memoryRequirements.memoryTypeBits;
		memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV;
		memoryRequirements2 = {};
		vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
		scratchSize = scratchSize > memoryRequirements2.memoryRequirements.size ? scratchSize : memoryRequirements2.memoryRequirements.size;

		CHECK_DBG_ONLY(buildScratchType == memoryRequirements2.memoryRequirements.memoryTypeBits,
			"AccelarationStructure: Build scratch and update scratch type do not match for top level AS!");
	}

	// Create scratch buffer
	allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = scratchSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	allocInfo = {};
	// Allocate memory and bind it to the buffer
	VK_CHECK_DBG_ONLY(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &scratchBuffer, &scratchBufferAllocation, &allocInfo),
		"AccelarationStructure: failed to allocate scratch buffer for top level accelaration structure!");

	// Create instance buffer
	allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = instanceCount * sizeof(TopLevelAccelerationStructureData);
	bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// Allocate memory and bind it to the buffer
	VK_CHECK_DBG_ONLY(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &instanceBuffer, &instanceBufferAllocation, nullptr),
		"AccelarationStructure: failed to allocate instance buffer for top level accelaration structure!");

	// Create staging buffer for instances
	allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = instanceCount * sizeof(TopLevelAccelerationStructureData);
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VK_CHECK_DBG_ONLY(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &instanceStagingBuffer, &instanceStagingBufferAllocation, nullptr),
		"AccelarationStructure: failed to allocate instance buffer for top level accelaration structure!");

	VK_CHECK_DBG_ONLY(vmaMapMemory(allocator, instanceStagingBufferAllocation, &mappedInstanceBuffer),
		"AccelarationStructure: failed to map instance buffer for top level accelaration structure!");
}

void TopLevelAccelerationStructure::cmdBuild(const VkCommandBuffer& cmdBuf, const uint32_t instanceCount, bool update)
{	
	CHECK_DBG_ONLY(!update || allowUpdate == update,
		"AccelartionStructure: Partial rebuild for bottom level accelaration structure is not allowed. First create the BLAS with appropriate flag!");

	VkBufferCopy copyRegion = {};
	copyRegion.size = sizeof(TopLevelAccelerationStructureData) * instanceCount;

	vkCmdCopyBuffer(cmdBuf, instanceStagingBuffer, instanceBuffer, 1, &copyRegion);

	VkBufferMemoryBarrier bufferMemoryBarrier = {};
	bufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	bufferMemoryBarrier.buffer = instanceBuffer;
	bufferMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	bufferMemoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
	bufferMemoryBarrier.size = copyRegion.size;

	vkCmdPipelineBarrier(
		cmdBuf,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
		0,
		0, nullptr,
		1, &bufferMemoryBarrier,
		0, nullptr);
	
	// Build the actual bottom-level acceleration structure
	VkAccelerationStructureInfoNV buildInfo = {};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	buildInfo.pNext = nullptr;
	buildInfo.flags = allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV : 0;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
	buildInfo.instanceCount = instanceCount;
	
	vkCmdBuildAccelerationStructureNV(cmdBuf, &buildInfo, instanceBuffer, 0, update,
		accelerationStructure, update ? accelerationStructure : VK_NULL_HANDLE, scratchBuffer, 0);

	// Wait for the builder to complete by setting a barrier on the resulting buffer. This is
	// particularly important as the construction of the top-level hierarchy may be called right
	// afterwards, before executing the command list.
	VkMemoryBarrier memoryBarrier;
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.pNext = nullptr;
	memoryBarrier.srcAccessMask =
		VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
	memoryBarrier.dstAccessMask =
		VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

	vkCmdPipelineBarrier(cmdBuf,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier,
		0, nullptr, 0, nullptr);
}