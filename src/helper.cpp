#include "helper.h"
#include "vk_mem_alloc.h"

#include <array>

extern std::vector<char> readFile(const std::string& filename) 
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	CHECK(file.is_open(), "failed to open file - " + filename);
	
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

extern VkPhysicalDeviceFeatures checkSupportedDeviceFeatures(const VkPhysicalDevice& physicalDevice, const std::vector<const char*>& requiredFeatures) 
{
	VkPhysicalDeviceFeatures supportedFeatures = {};
	VkPhysicalDeviceFeatures vk_requiredFeatures = {};

	vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
	
	for (const auto requiredFeature : requiredFeatures) {
		if (std::strcmp(requiredFeature, "samplerAnisotropy") == 0 && supportedFeatures.samplerAnisotropy)
			vk_requiredFeatures.samplerAnisotropy = VK_TRUE;
		else if (std::strcmp(requiredFeature, "multiDrawIndirect") == 0 && supportedFeatures.multiDrawIndirect)
			vk_requiredFeatures.multiDrawIndirect = VK_TRUE;
		else if (std::strcmp(requiredFeature, "drawIndirectFirstInstance") == 0 && supportedFeatures.drawIndirectFirstInstance)
			vk_requiredFeatures.drawIndirectFirstInstance = VK_TRUE;
		else if (std::strcmp(requiredFeature, "shaderStorageImageExtendedFormats") == 0 && supportedFeatures.shaderStorageImageExtendedFormats)
			vk_requiredFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
		else
			CHECK(false, std::string("physical device feature '") + requiredFeature + "' not found or unsupported!");
	}

	return vk_requiredFeatures;
}

extern VkCommandBuffer beginSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool) 
{
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

extern void endSingleTimeCommands(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkCommandBuffer& commandBuffer) 
{
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

extern void copyBufferToBuffer(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkBuffer& srcBuffer, const VkBuffer& dstBuffer, VkDeviceSize size) 
{
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(device, queue, commandPool, commandBuffer);
}

extern void* createStagingBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer& stagingBuffer, VmaAllocation& stagingBufferAllocation, VkDeviceSize sizeInBytes)
{
	void* mptrStagingBuffer = nullptr;

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = sizeInBytes;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr),
		"createBuffer: Failed to create staging buffer!");

	vmaMapMemory(allocator, stagingBufferAllocation, &mptrStagingBuffer);
	CHECK(mptrStagingBuffer != nullptr, "createBuffer: Failed to create mapper ptr to staging buffer!");

	return mptrStagingBuffer;
}

extern void* createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkBuffer& stagingBuffer, VmaAllocation& stagingBufferAllocation, VkDeviceSize sizeInBytes)
{
	void* mptrStagingBuffer = nullptr;

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = sizeInBytes;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr),
		"createBuffer: Failed to create staging buffer!");

	vmaMapMemory(allocator, stagingBufferAllocation, &mptrStagingBuffer);
	CHECK(mptrStagingBuffer != nullptr, "createBuffer: Failed to create mapper ptr to staging buffer!")

		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &buffer, &bufferAllocation, nullptr),
		"createBuffer: Failed to create buffer!");

	return mptrStagingBuffer;
}

extern void* createBuffer(const VmaAllocator& allocator, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsageFlags, bool cpuToGpu)
{
	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = bufferSize;
	bufferCreateInfo.usage = bufferUsageFlags;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = cpuToGpu ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_TO_CPU;
	allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocationInfo;
	if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &buffer, &bufferAllocation, &allocationInfo) != VK_SUCCESS)
		throw std::runtime_error("Failed to create uniform buffers!");

	if (allocationInfo.pMappedData == nullptr)
		throw std::runtime_error("Failed to map meomry for buffer!");
	
	return allocationInfo.pMappedData;
}

extern void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsageFlags, uint64_t pattern)
{
	VkDeviceSize nUint64_t = static_cast<VkDeviceSize>(bufferSize / sizeof(uint64_t)) + 1;
	std::vector<uint64_t> data(nUint64_t, pattern);
	createBuffer(device, allocator, queue, commandPool, buffer, bufferAllocation, bufferSize, static_cast<const void*>(data.data()), bufferUsageFlags);
}

extern void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer &buffer, VmaAllocation &bufferAllocation, VkDeviceSize bufferSize, const void *srcData, VkBufferUsageFlags bufferUsageFlags)
{
	CHECK_DBG_ONLY(srcData != nullptr, "createBuffer: data source cannot be null.");

	VkBuffer stagingBuffer;
	VmaAllocation stagingBufferAllocation;

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = bufferSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr),
		"Failed to create staging buffer!");

	void* data;
	vmaMapMemory(allocator, stagingBufferAllocation, &data);
	memcpy(data, srcData, (size_t)bufferSize);
	vmaUnmapMemory(allocator, stagingBufferAllocation);

	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | bufferUsageFlags;
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &buffer, &bufferAllocation, nullptr),
		"Failed to create vertex buffer!");

	copyBufferToBuffer(device, queue, commandPool, stagingBuffer, buffer, bufferSize);

	vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
}

extern VkDeviceSize imageFormatToBytes(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
		return 4 * sizeof(unsigned char);
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return 4 * sizeof(float);
	case VK_FORMAT_R32G32_SFLOAT:
		return 2 * sizeof(float);
	case VK_FORMAT_R32_SFLOAT:
		return sizeof(float);
	case VK_FORMAT_R32G32_UINT:
		return 2 * sizeof(uint32_t);
	case VK_FORMAT_R16G16_SFLOAT:
		return sizeof(float);
	case VK_FORMAT_R16G16_UNORM:
		return sizeof(float);
	default:
		CHECK_DBG_ONLY(false, "imageFormatToBytes : Unrecognised image format.");
	}

	return 0;
}

extern void createImage(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& image, VmaAllocation& imageAllocation,
	const VkExtent2D& extent, const VkImageUsageFlags& usage, const VkFormat format, const VkSampleCountFlagBits& sampleCount, const uint32_t layers)
{
	VkImageCreateInfo imageCreateInfo = {};
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.extent.width = extent.width;
	imageCreateInfo.extent.height = extent.height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = layers;
	imageCreateInfo.format = format;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.usage = usage;
	imageCreateInfo.samples = sampleCount;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &imageAllocation, nullptr),
		"Failed to create image!");
}

extern void createImageP(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& image, VmaAllocation& imageAllocation,
	const VkExtent2D& extent, const VkImageUsageFlags& usage, const VkFormat format, const VkSampleCountFlagBits& sampleCount, const uint64_t pattern, const uint32_t layers, const uint32_t mipLevels)
{	
	VkDeviceSize imageSizeBytes = extent.height * (extent.width * imageFormatToBytes(format));
	VkDeviceSize nUint64_t = static_cast<VkDeviceSize>(imageSizeBytes / sizeof(uint64_t)) + 1;
	std::vector<uint64_t> data(nUint64_t, pattern);
	std::vector<const void*> layerData(layers, static_cast<const void*>(data.data()));

	createImageD(device, allocator, queue, commandPool, image, imageAllocation, extent, usage, layerData, format, sampleCount, mipLevels);
}

static void cmdCpyBufImg(const VkCommandBuffer& cmdBuf, const VkBuffer& buffer, const VkImage& image, VkExtent2D extent, VkFormat imageFormat, uint32_t layers, bool cpyBufToImg)
{

	VkDeviceSize imageSizeBytes = extent.height * (extent.width * imageFormatToBytes(imageFormat));

	std::vector<VkBufferImageCopy> bufferCopyRegions;
	for (uint32_t layer = 0; layer < layers; layer++) {
		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = extent.width;
		bufferCopyRegion.imageExtent.height = extent.height;
		bufferCopyRegion.imageExtent.depth = 1;
		bufferCopyRegion.bufferOffset = layer * imageSizeBytes;

		bufferCopyRegions.push_back(bufferCopyRegion);
	}

	if (cpyBufToImg)
		vkCmdCopyBufferToImage(cmdBuf, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
	else
		vkCmdCopyImageToBuffer(cmdBuf, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
}

extern void cmdCopyBufferToImage(const VkCommandBuffer& cmdBuf, const VkBuffer& srcBuf, const VkImage &dstImage,  VkExtent2D extent, VkFormat imageFormat, uint32_t layers)
{	
	cmdCpyBufImg(cmdBuf, srcBuf, dstImage, extent, imageFormat, layers, true);
}

extern void cmdCopyImageToBuffer(const VkCommandBuffer& cmdBuf, const VkImage& srcImage, const VkBuffer& dstBuffer, VkExtent2D extent, VkFormat imageFormat, uint32_t layers)
{
	cmdCpyBufImg(cmdBuf, dstBuffer, srcImage, extent, imageFormat, layers, false);
}

extern void createImageD(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& image, VmaAllocation& imageAllocation, 
	const VkExtent2D& extent, const VkImageUsageFlags& usage, const std::vector<const void*>& layerData, 
	const VkFormat format, const VkSampleCountFlagBits& sampleCount, const uint32_t mipLevels)
{	
	bool srcDataCheck = true;
	for (const auto& data : layerData)
		srcDataCheck = srcDataCheck && (data != nullptr);

	uint32_t layers = static_cast<uint32_t>(layerData.size());
	VkBuffer stagingBuffer;
	VmaAllocation stagingBufferAllocation;

	CHECK_DBG_ONLY(layers > 0 && srcDataCheck, "createImage: data source cannot be null.");

	VkDeviceSize imageSizeBytes = extent.height * (extent.width * imageFormatToBytes(format));

	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = layers * imageSizeBytes;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr),
		"Failed to create image: failed to create staging buffer for texture image!");

	void* data;
	vmaMapMemory(allocator, stagingBufferAllocation, &data);
	
	for (uint32_t i = 0; i < layers; i++) {
		std::byte* start = static_cast<std::byte *>(data);
		memcpy(&start[i * imageSizeBytes], layerData[i], static_cast<size_t>(imageSizeBytes));
	}
	vmaUnmapMemory(allocator, stagingBufferAllocation);
	
	VkImageCreateInfo imageCreateInfo = {};
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.extent.width = extent.width;
	imageCreateInfo.extent.height = extent.height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = mipLevels;
	imageCreateInfo.arrayLayers = layers;
	imageCreateInfo.format = format;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage;
	imageCreateInfo.samples = sampleCount;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	VK_CHECK(vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &imageAllocation, nullptr),
		"Failed to create image!");

	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);
	cmdTransitionImageLayout(commandBuffer, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, layers);
	cmdCopyBufferToImage(commandBuffer, stagingBuffer, image, extent, format, layers);
	endSingleTimeCommands(device, queue, commandPool, commandBuffer);

	vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
}

extern VkImageView createImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t layerCount) 
{
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
	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView),
		"failed to create texture image view!");
	
	return imageView;
}

extern bool hasStencilComponent(VkFormat format) 
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

extern void cmdTransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount)
{	
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
	else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = 0;
		sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = 0;
		sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
	else {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = 0;
		sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}

	vkCmdPipelineBarrier(
		commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);
}

extern void transitionImageLayout(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount) 
{

	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);
	cmdTransitionImageLayout(commandBuffer, image, format, oldLayout, newLayout, mipLevels, layerCount);
	endSingleTimeCommands(device, queue, commandPool, commandBuffer);
}

extern void copyBufferToImage(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	const VkBuffer& srcBuffer, const VkImage& dstImage, VkExtent2D extent, VkFormat imageFormat, uint32_t layers) 
{
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);
	cmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, extent, imageFormat, layers);
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

	CHECK(false, "Failed to find supported format!");
}

extern VkFormat findDepthFormat(VkPhysicalDevice& physicalDevice) 
{
	return findSupportedFormat(physicalDevice,
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

extern SwapChainSupportDetails querySwapChainSupport(const VkPhysicalDevice& device, const VkSurfaceKHR& surface) 
{
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

extern QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& device, const VkSurfaceKHR& surface) 
{
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

	CHECK(indices.isComplete(), "Couldn't find all necessary queues!");

	return indices;
}

extern uint32_t queryComputeSharedMemSize(const VkPhysicalDevice& device)
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);

	return physicalDeviceProperties.limits.maxComputeSharedMemorySize;
}

extern inline glm::vec3 sphericalToCartesian(const glm::vec3 &polar)
{
	float sintheta = sin(polar.y);
	return glm::vec3(sintheta * cos(polar.z), sintheta * sin(polar.z), cos(polar.y)) * polar.x;
}

extern inline glm::vec3 cartesianToSpherical(const glm::vec3& cartesian)
{	
	float phi = atan2(cartesian.y, cartesian.x);
	return glm::vec3(glm::length(cartesian), atan2(glm::length(glm::vec2(cartesian)), cartesian.z), phi >= 0 ? phi : 2 * PI + phi);
}