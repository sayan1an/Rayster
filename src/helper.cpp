#include "helper.h"
#include "vk_mem_alloc.h"

extern std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

extern VkPhysicalDeviceFeatures checkSupportedDeviceFeatures(const VkPhysicalDevice& physicalDevice, const std::vector<const char*>& requiredFeatures) {
	VkPhysicalDeviceFeatures supportedFeatures = {};
	VkPhysicalDeviceFeatures vk_requiredFeatures = {};

	vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

	for (const auto requiredFeature : requiredFeatures) {
		if (std::strcmp(requiredFeature, "samplerAnisotropy") == 0 && supportedFeatures.samplerAnisotropy)
			vk_requiredFeatures.samplerAnisotropy = VK_TRUE;
		else if (std::strcmp(requiredFeature, "multiDrawIndirect") == 0 && supportedFeatures.multiDrawIndirect)
			vk_requiredFeatures.multiDrawIndirect = VK_TRUE;
		else
			throw std::runtime_error(std::string("physical device feature '") + requiredFeature + "' not found or unsupported!");
	}

	return vk_requiredFeatures;
}

extern VkCommandBuffer beginSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool) {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

extern void endSingleTimeCommands(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkCommandBuffer& commandBuffer) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

extern void copyBuffer(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkBuffer& srcBuffer, const VkBuffer& dstBuffer, VkDeviceSize size) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(device, queue, commandPool, commandBuffer);
}

extern VkImageView createImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t layerCount) {
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = layerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = layerCount;

	VkImageView imageView;
	if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
		throw std::runtime_error("failed to create texture image view!");
	}

	return imageView;
}

extern bool hasStencilComponent(VkFormat format) {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

extern void transitionImageLayout(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount) {

	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;

	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (hasStencilComponent(format)) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = layerCount;

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = 0;
		sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
	else {
		throw std::invalid_argument("unsupported layout transition!");
	}

	vkCmdPipelineBarrier(
		commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	endSingleTimeCommands(device, queue, commandPool, commandBuffer);
}

extern void copyBufferToImage(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = {
		width,
		height,
		1
	};

	vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	endSingleTimeCommands(device, queue, commandPool, commandBuffer);
}

extern VkFormat findSupportedFormat(VkPhysicalDevice& physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("failed to find supported format!");
}

extern VkFormat findDepthFormat(VkPhysicalDevice& physicalDevice) {
	return findSupportedFormat(physicalDevice,
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

extern SwapChainSupportDetails querySwapChainSupport(const VkPhysicalDevice& device, const VkSurfaceKHR& surface) {
	SwapChainSupportDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

	if (formatCount != 0) {
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

	if (presentModeCount != 0) {
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
	}

	return details;
}

extern QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& device, const VkSurfaceKHR& surface) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport) {
			indices.presentFamily = i;
		}

		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
			indices.computeFamily = i;
		}

		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	if (!indices.isComplete())
		throw std::runtime_error("Couldn't find all necessary queues!");

	return indices;
}

extern void createBottomLevelAccelerationStructure(const VkDevice &device, const VmaAllocator &allocator, const std::vector<VkGeometryNV> &geometries, BottomLevelAccelerationStructure &blas, bool allowUpdate)
{
	PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV = reinterpret_cast<PFN_vkCreateAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureNV"));
	PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV = reinterpret_cast<PFN_vkGetAccelerationStructureMemoryRequirementsNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureMemoryRequirementsNV"));
	PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV = reinterpret_cast<PFN_vkBindAccelerationStructureMemoryNV>(vkGetDeviceProcAddr(device, "vkBindAccelerationStructureMemoryNV"));
	PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV = reinterpret_cast<PFN_vkGetAccelerationStructureHandleNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureHandleNV"));
	blas.vkCmdBuildAccelerationStructureNV = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructureNV"));

	VkAccelerationStructureInfoNV accelerationStructureInfo{};
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
	accelerationStructureInfo.instanceCount = 0;
	accelerationStructureInfo.geometryCount = static_cast<uint32_t>(geometries.size());
	accelerationStructureInfo.pGeometries = geometries.data();
	accelerationStructureInfo.flags = allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV : 0;

	blas.allowUpdate = allowUpdate;

	VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
	accelerationStructureCreateInfo.info = accelerationStructureInfo;
	if (vkCreateAccelerationStructureNV(device, &accelerationStructureCreateInfo, nullptr, &blas.accelerationStructure) != VK_SUCCESS)
		throw std::runtime_error("failed to create bottom level accelaration structure!");

	// Find memory requirements for accelaration structure object
	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo{};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.accelerationStructure = blas.accelerationStructure;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;

	VkMemoryRequirements2 memoryRequirements2{};
	vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);

	// Allocate memory for accelaration structure object
	// https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63
	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;

	VmaAllocationInfo allocInfo = {};
	if (vmaAllocateMemory(allocator, &memoryRequirements2.memoryRequirements, &allocCreateInfo, &blas.accelerationStructureAllocation, &allocInfo) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate memory for bottom level accelaration structure!");
	
	// Bind Accelaration structure with its memory
	VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo{};
	accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
	accelerationStructureMemoryInfo.accelerationStructure = blas.accelerationStructure;
	accelerationStructureMemoryInfo.memory = allocInfo.deviceMemory;
	accelerationStructureMemoryInfo.memoryOffset = allocInfo.offset;
	if (vkBindAccelerationStructureMemoryNV(device, 1, &accelerationStructureMemoryInfo) != VK_SUCCESS)
		throw std::runtime_error("failed to bind memory for bottom level accelaration structure!");

	// Get a handle for the acceleration structure
	if (vkGetAccelerationStructureHandleNV(device, blas.accelerationStructure, sizeof(uint64_t), &blas.handle) != VK_SUCCESS)
		throw std::runtime_error("failed to retrive handle for bottom level accelaration structure!");

	
	// Find memory requirements for accelaration structure build scratch
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
	memoryRequirements2 = {};
	vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
	size_t scratchSize = memoryRequirements2.memoryRequirements.size;

	// Find memory requirements for accelaration structure update scratch
	if (blas.allowUpdate) {
		uint32_t buildScratchType = memoryRequirements2.memoryRequirements.memoryTypeBits;
		memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV;
		memoryRequirements2 = {};
		vkGetAccelerationStructureMemoryRequirementsNV(device, &memoryRequirementsInfo, &memoryRequirements2);
		scratchSize = scratchSize > memoryRequirements2.memoryRequirements.size ? scratchSize : memoryRequirements2.memoryRequirements.size;

		if (buildScratchType != memoryRequirements2.memoryRequirements.memoryTypeBits)
			throw std::runtime_error("Build scratch and update scratch type do not match!");
	}
	
	// Create scratch buffer
	// https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63
	allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = scratchSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// Allocate memory and bind it to the buffer
	if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &blas.scratchBuffer, &blas.scratchBufferAllocation, nullptr)  != VK_SUCCESS)
		throw std::runtime_error("failed to allocate scratch buffer for bottom level accelaration structure!");
}

void cmdBuildBotttomLevelAccelarationStructure(VkCommandBuffer& cmdBuf, const std::vector<VkGeometryNV>& geometries, const BottomLevelAccelerationStructure& blas, bool partialRebuild, const BottomLevelAccelerationStructure& prevBlas) 
{	
	if (blas.allowUpdate != partialRebuild || prevBlas.allowUpdate != partialRebuild)
		throw std::runtime_error("Partial rebuild for bottom level accelaration structure is not allowed. First create the BLAS with appropriate flag!");

	// Build the actual bottom-level acceleration structure
	VkAccelerationStructureInfoNV buildInfo;
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	buildInfo.pNext = nullptr;
	buildInfo.flags = blas.allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV : 0;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
	buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
	buildInfo.pGeometries = geometries.data();
	buildInfo.instanceCount = 0;

	blas.vkCmdBuildAccelerationStructureNV(cmdBuf, &buildInfo, VK_NULL_HANDLE, 0, partialRebuild,
		blas.accelerationStructure, partialRebuild ? prevBlas.accelerationStructure : VK_NULL_HANDLE, blas.scratchBuffer,
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