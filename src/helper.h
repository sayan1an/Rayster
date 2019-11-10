#pragma once

#include <iostream>
#include <vector>
#include <fstream>
#include <optional>
#include <algorithm>

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include "stb_image.h"
#include <glm/glm.hpp>

#define ROOT std::string("D:/projects/VkExperiment")

#ifdef NDEBUG
static const bool enableValidationLayers = false;
#else
static const bool enableValidationLayers = true;
#endif

#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif

#ifndef PRINT_VECTOR
#define PRINT_VECTOR(v) (std::cout << "(" << (v).x << ", " << (v).y << ", " << (v).z << ", " << (v).w << ")" << std::endl)
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

struct Image2d
{
	void* pixels = nullptr;
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

	uint32_t mipLevels() const
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
				((float*)pixels)[i] = color.x;
				((float*)pixels)[i + 1] = color.y;
				((float*)pixels)[i + 2] = color.z;
				((float*)pixels)[i + 3] = color.w;
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


std::vector<char> readFile(const std::string& filename); 
VkPhysicalDeviceFeatures checkSupportedDeviceFeatures(const VkPhysicalDevice& physicalDevice, const std::vector<const char*>& requiredFeatures);
VkCommandBuffer beginSingleTimeCommands(const VkDevice& device, const VkCommandPool& commandPool);
void endSingleTimeCommands(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkCommandBuffer& commandBuffer);
void copyBuffer(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool, const VkBuffer& srcBuffer, const VkBuffer& dstBuffer, VkDeviceSize size);
VkImageView createImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t layerCount);
bool hasStencilComponent(VkFormat format);
void cmdTransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount);
void transitionImageLayout(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount);
void copyBufferToImage(const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
	VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
VkFormat findSupportedFormat(VkPhysicalDevice& physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
VkFormat findDepthFormat(VkPhysicalDevice& physicalDevice);
SwapChainSupportDetails querySwapChainSupport(const VkPhysicalDevice& device, const VkSurfaceKHR& surface);
QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& device, const VkSurfaceKHR& surface);
void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkDeviceSize bufferSize, const void* srcData, VkBufferUsageFlags bufferUsageFlags);