#pragma once

#include <iostream>
#include <vector>
#include <fstream>
#include <optional>
#include <algorithm>
#include <string>

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize.h"
#include "imgui.h"
#include <glm/glm.hpp>

#define ROOT std::string("D:/projects/VkExperiment")

#define NDEBUG

#ifdef NDEBUG
static const bool enableValidationLayers = true;
#else
static const bool enableValidationLayers = false;
#endif

#define CHECK(cond, stringVal) \
{ \
	if (!(cond)) throw std::runtime_error("Error: " + std::string(stringVal) + " FILE: " + std::string(__FILE__) + " LINE: " + std::to_string(__LINE__));\
}

#define WARN(cond, stringVal) \
{ \
	if (!(cond)) std::cerr << "Warning: " + std::string(stringVal) + " FILE: " + std::string(__FILE__) + " LINE: " + std::to_string(__LINE__) << std::endl;\
}

#ifdef NDEBUG
#define CHECK_DBG_ONLY(cond, stringVal) CHECK(cond, stringVal)
#else
#define CHECK_DBG_ONLY(cond, stringVal) {}
#endif

#ifdef NDEBUG
#define WARN_DBG_ONLY(cond, stringVal) WARN(cond, stringVal)
#else
#define WARN_DBG_ONLY(cond, stringVal) {}
#endif

#define VK_CHECK(result, stringVal) CHECK(((result) == VK_SUCCESS), stringVal)

#ifdef NDEBUG
#define VK_CHECK_DBG_ONLY(result, stringVal) CHECK(((result) == VK_SUCCESS), stringVal)
#else
#define VK_CHECK_DBG_ONLY(result, stringVal) {}
#endif

#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif

#ifndef IS_POWER_2
#define IS_POWER_2(x) ((x) > 0 && ((x) & ((x)-1)) == 0)
#endif

#ifndef PRINT_VECTOR4
#define PRINT_VECTOR4(v) (std::cout << "(" << (v).x << ", " << (v).y << ", " << (v).z << ", " << (v).w << ")" << std::endl)
#endif

#ifndef PRINT_VECTOR3
#define PRINT_VECTOR3(v) (std::cout << "(" << (v).x << ", " << (v).y << ", " << (v).z << ")" << std::endl)
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

	uint32_t mipLevels() const
	{	
		if (forceMipLevelToOne)
			return 1;

		if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
			return 1;

		return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	}

	Image2d(const std::string texturePath)
	{
		int texChannels, iWidth, iHeight;
		pixels = stbi_load(texturePath.c_str(), &iWidth, &iHeight, &texChannels, STBI_rgb_alpha);
		CHECK(pixels, "Image2d : Failed to load texture image - " + texturePath);
		
		width = static_cast<uint32_t> (iWidth);
		height = static_cast<uint32_t> (iHeight);
		format = VK_FORMAT_R8G8B8A8_UNORM;
		path = texturePath;
	}

	void cleanUp()
	{	
		if (!externalAlocation)
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

	void resize(uint32_t newWidth, uint32_t newHeight)
	{
		if (format == VK_FORMAT_R8G8B8A8_UNORM) {
			void* newPixels = new unsigned char[(size_t)newWidth * newHeight * 4];
			CHECK(stbir_resize_uint8((const unsigned char*)pixels, width, height, 0, (unsigned char *)newPixels, newWidth, newHeight, 0, 4) > 0,
				"Image2d: Failed to resize image.");

			if (!externalAlocation)
				stbi_image_free(pixels);
			else
				WARN(false, "Image2d: Memory for image - " + path + " is allocated by user. Cannot free memory for resize.");
				
			pixels = newPixels;
			
			width = newWidth;
			height = newHeight;

			externalAlocation = false;
		}
		else if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
			void* newPixels = new float[(size_t)newWidth * newHeight * 4];
			CHECK (stbir_resize_float((const float*)pixels, width, height, 0, (float *)newPixels, newWidth, newHeight, 0, 4) >0,
				"Image2d: Failed to resize image.");
			
			if (!externalAlocation)
				stbi_image_free(pixels);
			else
				WARN(false, "Image2d: Memory for image - " + path + " is allocated by user. Cannot free memory for resize.");

			pixels = newPixels;

			width = newWidth;
			height = newHeight;

			externalAlocation = false;
		}
		else
			CHECK(false, "Image2d: Failed to resize image. Format is unsupported.");
	}

	// This is specific to ImGui fonts
	Image2d(bool forceMipLevelToOne)
	{	
		CHECK(ImGui::GetCurrentContext() != NULL,
			"Image2d : ImGui context is null, cannot create font image.");

		ImGuiIO& io = ImGui::GetIO();
		
		unsigned char* fontData;
		int textWidth;
		int textHeight;
		io.Fonts->GetTexDataAsRGBA32(&fontData, &textWidth, &textHeight);
		
		pixels = static_cast<void*>(fontData);
		width = static_cast<uint32_t>(textWidth);
		height = static_cast<uint32_t>(textHeight);
		format = VK_FORMAT_R8G8B8A8_UNORM;

		this->forceMipLevelToOne = forceMipLevelToOne;
		this->externalAlocation = true;

		path = "";
	}
private:
	bool externalAlocation = false;
	bool forceMipLevelToOne = false;
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
VkDeviceSize imageFormatToBytes(VkFormat format);
// create image without initialization
void createImage(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& image, VmaAllocation& imageAllocation,
	const VkExtent2D& extent, const VkImageUsageFlags& usage, const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT, const VkSampleCountFlagBits& sampleCount = VK_SAMPLE_COUNT_1_BIT);
// create image with initialized with 64bit pattern
void createImageP(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& image, VmaAllocation& imageAllocation,
	const VkExtent2D& extent, const VkImageUsageFlags& usage, const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT, const VkSampleCountFlagBits& sampleCount = VK_SAMPLE_COUNT_1_BIT,
	const uint64_t pattern = 0, const uint32_t layers = 1, const uint32_t mipLevels = 1);
// create image with initialized with raw binary data
void createImageD(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& image, VmaAllocation& imageAllocation,
	const VkExtent2D& extent, const VkImageUsageFlags& usage, const std::vector<const void*>& srcData, const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT, 
	const VkSampleCountFlagBits& sampleCount = VK_SAMPLE_COUNT_1_BIT, const uint32_t mipLevels = 1);
// create memory mapper buffer, use for small sized buffers
void* createBuffer(const VmaAllocator& allocator, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsageFlags, bool cpuToGpu = true);
// create buffer with initialized with 64bit pattern
void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsageFlags, uint64_t pattern = 0);
// create buffer with initialized with raw binary data
void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkBuffer& buffer, VmaAllocation& bufferAllocation, VkDeviceSize bufferSize, const void* srcData, VkBufferUsageFlags bufferUsageFlags);
uint32_t queryComputeSharedMemSize(const VkPhysicalDevice& device);