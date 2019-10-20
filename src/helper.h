#pragma once

#include <vector>
#include <fstream>
#include <optional>

#include "vulkan/vulkan.h"

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

struct AccelerationStructure {
	VmaAllocation accelerationStructureAllocation;
	VkAccelerationStructureNV accelerationStructure;
	uint64_t handle;
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