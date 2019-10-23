#pragma once

#include <iostream>
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

class AccelerationStructure {
protected:
	VkBuffer scratchBuffer = VK_NULL_HANDLE;
	VmaAllocation scratchBufferAllocation = VK_NULL_HANDLE;
	VmaAllocation accelerationStructureAllocation = VK_NULL_HANDLE;
	VkAccelerationStructureNV accelerationStructure = VK_NULL_HANDLE;

	bool allowUpdate = false; // Allow for runtime update

	void initProcAddress(const VkDevice& device) {
		if (vkCreateAccelerationStructureNV != nullptr) return;
		vkCreateAccelerationStructureNV = reinterpret_cast<PFN_vkCreateAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureNV"));
		vkGetAccelerationStructureMemoryRequirementsNV = reinterpret_cast<PFN_vkGetAccelerationStructureMemoryRequirementsNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureMemoryRequirementsNV"));
		vkBindAccelerationStructureMemoryNV = reinterpret_cast<PFN_vkBindAccelerationStructureMemoryNV>(vkGetDeviceProcAddr(device, "vkBindAccelerationStructureMemoryNV"));
		vkGetAccelerationStructureHandleNV = reinterpret_cast<PFN_vkGetAccelerationStructureHandleNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureHandleNV"));
		vkCmdBuildAccelerationStructureNV = reinterpret_cast<PFN_vkCmdBuildAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructureNV"));
		vkDestroyAccelerationStructureNV = reinterpret_cast<PFN_vkDestroyAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureNV"));
	}
	// also store function pointers
	PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV = nullptr;
	PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV = nullptr;
	PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV = nullptr;
	PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV = nullptr;
	PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV = nullptr;
	PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV = nullptr;

public:
	void cleanUp(const VkDevice &device, const VmaAllocator &allocator) {
		if (vkDestroyAccelerationStructureNV == nullptr)
			throw std::runtime_error("Accelaration structure is NOT initialized!");

		vmaDestroyBuffer(allocator, scratchBuffer, scratchBufferAllocation);
		vmaFreeMemory(allocator, accelerationStructureAllocation);
		vkDestroyAccelerationStructureNV(device, accelerationStructure, nullptr);
	}
};

class BottomLevelAccelerationStructure : public AccelerationStructure {
public:
	uint64_t handle = 0;
	void create(const VkDevice& device, const VmaAllocator& allocator, const std::vector<VkGeometryNV>& geometries, bool allowUpdate = false);
	void cmdBuild(const VkCommandBuffer& cmdBuf, const std::vector<VkGeometryNV>& geometries, bool partialRebuild = false);
};

// Data layout expected by VK_NV_ray_tracing for top level acceleration structure 
struct TopLevelAccelerationStructureData {
	float transform[12];  // Transform matrix, containing only the top 3 rows
	uint32_t instanceId : 24;  // id of the instance, a unique number
	uint32_t mask : 8; // Visibility mask
	uint32_t instanceOffset : 24; // Index of the hit group which will be invoked when a ray hits the instance
	uint32_t flags : 8; // Instance flags, such as culling
	uint64_t blasHandle; // Opaque handle of the bottom-level acceleration structure
};

static_assert(sizeof(TopLevelAccelerationStructureData) == 64, "The size in bytes of the top level accelaration structure data must be 64 bytes.");

class TopLevelAccelerationStructure : public AccelerationStructure {
private:
	VkBuffer instanceBuffer = VK_NULL_HANDLE;
	VmaAllocation instanceBufferAllocation = VK_NULL_HANDLE;
	VkBuffer instanceStagingBuffer = VK_NULL_HANDLE;
	VmaAllocation instanceStagingBufferAllocation = VK_NULL_HANDLE;
	void* mappedInstanceBuffer = nullptr;
public:
	void create(const VkDevice& device, const VmaAllocator& allocator, const uint32_t instanceCount, bool allowUpdate = true);
	void updateInstanceData(const std::vector<TopLevelAccelerationStructureData>& instances) {
		if (mappedInstanceBuffer == nullptr)
			throw std::runtime_error("Accelaration structure is NOT initialized!");

		memcpy(mappedInstanceBuffer, instances.data(), instances.size() * sizeof(TopLevelAccelerationStructureData));
	}
	void cmdBuild(const VkCommandBuffer& cmdBuf, const uint32_t instanceCount, bool rebuild);
	void cleanUp(const VkDevice& device, const VmaAllocator& allocator) {
		AccelerationStructure::cleanUp(device, allocator);
		vmaUnmapMemory(allocator, instanceStagingBufferAllocation);
		mappedInstanceBuffer = nullptr;
		vmaDestroyBuffer(allocator, instanceStagingBuffer, instanceStagingBufferAllocation);
		vmaDestroyBuffer(allocator, instanceBuffer, instanceBufferAllocation);
	}
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