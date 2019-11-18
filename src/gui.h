#pragma once

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include "generator.h"
#include "io.hpp"

// Usage: add the gui commands in the final raterization pass.
// All gui commands and buffers must be updated at runtime as gui elements can change at runtime.
class Gui
{
public:
	Gui();
	~Gui();

	// builds the gui data
	void buildGui(IO& io)
	{
		ioSetup(io);
		ImGui::NewFrame();
		guiSetup();
		// Render to generate draw buffers
		ImGui::Render();
	}
	
	// upload data to device and create new buffers if required
	void uploadData(const VkDevice& device, const VmaAllocator& allocator);
	// draw commands
	void cmdDraw(const VkCommandBuffer& cmdBuf);
	// creates gui resources like font texture, descriptor sets and pipeline
	void createResources(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& cmdPool, const VkRenderPass& renderPass, uint32_t subpassIdx);
	// cleans up gui resources
	void cleanUp(const VkDevice& device, const VmaAllocator& allocator);
	
	// Override to change gui style.
	virtual void setStyle();

protected:
	// Override this to setup gui elements
	virtual void guiSetup()
	{
		ImGui::ShowDemoWindow();
	}
private:
	
	TextureGenerator fontTexGen;
	VkImage fontTexImage;
	VkImageView fontTexImageView;
	VkSampler fontTexSampler;
	VmaAllocation fontTexAllocation;

	DescriptorSetGenerator descGen;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	GraphicsPipelineGenerator gfxPipeGen;

	struct PushConstBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstBlock;
	std::vector<VkPushConstantRange> pushConstatRanges;

	std::vector<VkDynamicState> dynamicStates;

	const uint32_t VERTEX_BINDING_ID = 0;
	std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
	void* vertexBufferPtr = nullptr;
	int vertexCount = 0;

	VkBuffer indexBuffer = VK_NULL_HANDLE;
	VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
	void* indexBufferPtr = nullptr;
	int indexCount = 0;

	const int bufferAllocMultiplier = 5;
	
	// connects ImGui-io to Glfw-io
	void ioSetup(IO& io);
};