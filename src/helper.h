#pragma once

#include <vector>
#include <fstream>
#include <optional>

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include <glm/glm.hpp>

#define ROOT std::string("D:/projects/VkExperiment")

#ifdef NDEBUG
static const bool enableValidationLayers = false;
#else
static const bool enableValidationLayers = true;
#endif

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	std::optional<uint32_t> computeFamily;

	bool isComplete() {
		return graphicsFamily.has_value() && presentFamily.has_value() && computeFamily.has_value();
	}
};


struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

struct BottomLevelAccelerationStructure {
	VkBuffer scratchBuffer = VK_NULL_HANDLE;
	VmaAllocation scratchBufferAllocation = VK_NULL_HANDLE;
	VmaAllocation accelerationStructureAllocation = VK_NULL_HANDLE;
	VkAccelerationStructureNV accelerationStructure = VK_NULL_HANDLE;
	uint64_t handle = 0;
	bool allowUpdate = false; // Allow for runtime update

	// also store function pointers
	PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV = nullptr;
	PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV = nullptr;
	PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV = nullptr;
	PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV = nullptr;
	PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV = nullptr;
};

// Data layout expected by VK_NV_ray_tracing for top level acceleration structure 
struct TopLevelAccelerationStructureData {
	glm::mat3x4 transform;  // Transform matrix, containing only the top 3 rows
	uint32_t instanceId : 24;  // id of the instance, a unique number
	uint32_t mask : 8; // Visibility mask
	uint32_t instanceOffset : 24; // Index of the hit group which will be invoked when a ray hits the instance
	uint32_t flags : 8; // Instance flags, such as culling
	uint64_t blasHandle; // Opaque handle of the bottom-level acceleration structure
};

struct TopLevelAccelerationStructure {
	BottomLevelAccelerationStructure& blas;
	VkBuffer scratchBuffer = VK_NULL_HANDLE;
	VmaAllocation scratchBufferAllocation = VK_NULL_HANDLE;
	VkBuffer instanceBuffer = VK_NULL_HANDLE;
	VmaAllocation instanceBufferAllocation = VK_NULL_HANDLE;
	void* mappedInstanceBuffer = nullptr;
	VmaAllocation accelerationStructureAllocation = VK_NULL_HANDLE;
	VkAccelerationStructureNV accelerationStructure = VK_NULL_HANDLE;
	bool allowUpdate = false; // Allow for runtime update
};

std::vector<char> readFile(const std::string& filename); 
VkPhysicalDeviceFeatures checkSupportedDeviceFeatures(const VkPhysicalDevice& physicalDevice, const std::vector<const char*>& requiredFeatures);
VkCommandBuffer beginSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool);
void endSingleTimeCommands(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkCommandBuffer& commandBuffer);
void copyBuffer(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkBuffer& srcBuffer, const VkBuffer& dstBuffer, VkDeviceSize size);
VkImageView createImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t layerCount);
bool hasStencilComponent(VkFormat format);
void transitionImageLayout(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount);
void copyBufferToImage(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
VkFormat findSupportedFormat(VkPhysicalDevice& physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
VkFormat findDepthFormat(VkPhysicalDevice& physicalDevice);
SwapChainSupportDetails querySwapChainSupport(const VkPhysicalDevice& device, const VkSurfaceKHR& surface);
QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& device, const VkSurfaceKHR& surface);
void createBottomLevelAccelerationStructure(const VkDevice& device, const VmaAllocator& allocator, const std::vector<VkGeometryNV>& geometries, BottomLevelAccelerationStructure& blas, bool allowUpdate = false);
void cmdBuildBotttomLevelAccelarationStructure(const VkCommandBuffer& cmdBuf, const std::vector<VkGeometryNV>& geometries, const BottomLevelAccelerationStructure& blas, bool partialRebuild = false, const BottomLevelAccelerationStructure& prevBlas = {});
void createTopLevelAccelerationStructure(const VkDevice& device, const VmaAllocator& allocator, const std::vector<TopLevelAccelerationStructureData>& instances, TopLevelAccelerationStructure& tlas, bool allowUpdate = true);
void cmdBuildTopLevelAccelarationStructure(VkCommandBuffer& cmdBuf, const uint32_t instanceCount, const TopLevelAccelerationStructure& tlas, bool partialRebuild, const TopLevelAccelerationStructure* prevTlas);