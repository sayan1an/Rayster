#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <stdexcept>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>

#include "stb_image.h"
#include "tiny_obj_loader.h"
#include "vk_mem_alloc.h"

#include "../sceneManager.h"
#include "../model.hpp"
#include "../io.hpp"
#include "../camera.hpp"
#include "../appBase.hpp"
#include "../generator.h"

class Subpass1 {
public:
	VkSubpassDescription subpassDescription;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	void createSubpassDescription(const VkDevice &device) {
		// ref to multi-sample color buffer
		colorAttachmentRef = {};
		colorAttachmentRef.attachment = 0; // index to frame buffer
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// ref to multi-sample depth buffer
		depthAttachmentRef = {};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// ref to color resolve image buffer
		colorAttachmentResolveRef = {};
		colorAttachmentResolveRef.attachment = 3;
		colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;
		subpassDescription.pResolveAttachments = &colorAttachmentResolveRef;
	}
	
	void createSubpass(const VkDevice &device, const VkExtent2D &swapChainExtent, const VkSampleCountFlagBits &msaaSamples, const VkRenderPass &renderPass,
		const Camera &cam, const VkImageView &textureImageView, const VkSampler &textureSampler) {
		
		descGen.bindBuffer({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT }, cam.getDescriptorBufferInfo());
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { textureSampler,  textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		auto bindingDescription = Model::getBindingDescription();
		auto attributeDescription = Model::getAttributeDescriptions();

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/01_vert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/01_frag.spv");
		gfxPipeGen.addVertexInputState(bindingDescription, attributeDescription);
		gfxPipeGen.addViewportState(swapChainExtent);
		gfxPipeGen.addMsaaSate(msaaSamples);

		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
	}
private:
	VkAttachmentReference colorAttachmentRef;
	VkAttachmentReference depthAttachmentRef;
	VkAttachmentReference colorAttachmentResolveRef;

	GraphicsPipelineGenerator gfxPipeGen;
	DescriptorSetGenerator descGen;
};

class Subpass2 {
public:
	VkSubpassDescription subpassDescription;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	void createSubpassDescription(const VkDevice& device)
	{
		colorAttachmentRef = {};
		colorAttachmentRef.attachment = 2; // index to framebuffer
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		inputAttachmentRefs.push_back({ 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		inputAttachmentRefs.push_back({ 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = 2;
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, const VkImageView& inputImageView) {
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/02_vert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/02_frag.spv");
		gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
		gfxPipeGen.addDepthStencilState(VK_FALSE);
		gfxPipeGen.addViewportState(swapChainExtent);

		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 1, &pipeline, &pipelineLayout);
	}
private:
	VkAttachmentReference colorAttachmentRef;
	std::vector<VkAttachmentReference> inputAttachmentRefs;
	DescriptorSetGenerator descGen;
	GraphicsPipelineGenerator gfxPipeGen;
};

class ComputePipeline {
public:
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	
	void createPipeline(const VkDevice &device, const VkImageView &inView, const VkImageView &outView) 
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outView,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		compPipeGen.addComputeShaderStage(device, ROOT + "/shaders/03_comp.spv");
		compPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}
private:
	DescriptorSetGenerator descGen;
	ComputePipelineGenerator compPipeGen;
};

class GraphicsComputeApplication : public WindowApplication {
public:
	GraphicsComputeApplication() : WindowApplication(std::vector<const char*>(), std::vector<const char*>(), std::vector<const char*>(), std::vector<const char*>()) {}
private:
	const int MAX_FRAMES_IN_FLIGHT = 2;
	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkRenderPass renderPass;

	Subpass1 subpass1;
	Subpass2 subpass2;
	ComputePipeline computePipeline;

	VkImage colorImage;
	VmaAllocation colorImageAllocation;
	VkImageView colorImageView;

	VkImage colorResolveImage;
	VmaAllocation colorResolveImageAllocation;
	VkImageView colorResolveImageView;

	VkImage computeShaderOutImage;
	VmaAllocation computeShaderOutImageAllocation;
	VkImageView computeShaderOutImageView;

	VkImage depthImage;
	VmaAllocation depthImageAllocation;
	VkImageView depthImageView;

	Model model;
	
	std::vector<VkCommandBuffer> commandBuffers;
	VkCommandBuffer computeCommandBuffer;
	VkFence computeShaderFence;

	void init() {
		loadScene(model, cam);

		subpass1.createSubpassDescription(device);
		subpass2.createSubpassDescription(device);
		createRenderPass();
		
		createColorResources();
		createDepthResources();
		createFramebuffers();
		
		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		subpass1.createSubpass(device, swapChainExtent, msaaSamples, renderPass, cam, model.textureImageView, model.textureSampler);
		subpass2.createSubpass(device, swapChainExtent, renderPass, computeShaderOutImageView);
		computePipeline.createPipeline(device, colorResolveImageView, computeShaderOutImageView);
		createCommandBuffers();
		createComputeCommandBuffer();
		createComputeSyncObject();
	}
	   
	void cleanUpAfterSwapChainResize() {
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(allocator, depthImage, depthImageAllocation);

		vkDestroyImageView(device, colorImageView, nullptr);
		vmaDestroyImage(allocator, colorImage, colorImageAllocation);

		vkDestroyImageView(device, colorResolveImageView, nullptr);
		vmaDestroyImage(allocator, colorResolveImage, colorResolveImageAllocation);

		vkDestroyImageView(device, computeShaderOutImageView, nullptr);
		vmaDestroyImage(allocator, computeShaderOutImage, computeShaderOutImageAllocation);

		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}

		vkFreeCommandBuffers(device, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

		vkDestroyPipeline(device, subpass1.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass1.pipelineLayout, nullptr);

		vkDestroyPipeline(device, subpass2.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass2.pipelineLayout, nullptr);

		vkDestroyRenderPass(device, renderPass, nullptr);

		vkDestroyPipeline(device, computePipeline.pipeline, nullptr);
		vkDestroyPipelineLayout(device, computePipeline.pipelineLayout, nullptr);
		
		vkDestroyDescriptorSetLayout(device, computePipeline.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, subpass2.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, subpass1.descriptorSetLayout, nullptr);
		
		vkDestroyDescriptorPool(device, subpass1.descriptorPool, nullptr);
		vkDestroyDescriptorPool(device, subpass2.descriptorPool, nullptr);
		vkDestroyDescriptorPool(device, computePipeline.descriptorPool, nullptr);
	}

	void recreateAfterSwapChainResize() {
		createRenderPass();
		createColorResources();
		createDepthResources();
		createFramebuffers();

		subpass1.createSubpass(device, swapChainExtent, msaaSamples, renderPass, cam, model.textureImageView, model.textureSampler);
		subpass2.createSubpass(device, swapChainExtent, renderPass, computeShaderOutImageView);
		computePipeline.createPipeline(device, colorResolveImageView, computeShaderOutImageView);
		createCommandBuffers();
		createComputeCommandBuffer();
	}

	void cleanupFinal() 
	{
		model.cleanUp(device, allocator);
		vkDestroyFence(device, computeShaderFence, nullptr);
	}

	void createRenderPass() {
		// Overall idea: Vulkan will resolve/change multi-sample color image to regular swap chain comaptible/presentable image
		// Hence we have to attach a colorAttachemnt (MSAA image) and colorAttachmentResolve(swap chain image).

		// This corresponds to the multi-sampled color buffer
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = msaaSamples; // multiple samples
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // since we are not directly presenting the color attachment we use K_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL instead of VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		
		VkAttachmentDescription depthAttachment = {};
		depthAttachment.format = findDepthFormat(physicalDevice);
		depthAttachment.samples = msaaSamples; // multiple samples
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// this corresponds to the swap chain images
		VkAttachmentDescription swapChainAttachment = {};
		swapChainAttachment.format = swapChainImageFormat;
		swapChainAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		swapChainAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		swapChainAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		swapChainAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		swapChainAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		swapChainAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		swapChainAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription colorAttachmentResolve = {};
		colorAttachmentResolve.format = swapChainImageFormat;
		colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkAttachmentDescription computeShaderOutputAttachment = {};
		computeShaderOutputAttachment.format = swapChainImageFormat;
		computeShaderOutputAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		computeShaderOutputAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		computeShaderOutputAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		computeShaderOutputAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		computeShaderOutputAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		computeShaderOutputAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		computeShaderOutputAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		
		std::array<VkSubpassDescription, 2> subpassDesc = { subpass1.subpassDescription, subpass2.subpassDescription };
				
		std::array<VkSubpassDependency, 3> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// This dependency transitions the input attachment from color attachment to shader read
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = 1;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[2].srcSubpass = 0;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		std::array<VkAttachmentDescription, 5> attachments = { colorAttachment, depthAttachment, swapChainAttachment, colorAttachmentResolve, computeShaderOutputAttachment }; 
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 2;
		renderPassInfo.pSubpasses = subpassDesc.data();
		renderPassInfo.dependencyCount = 3;
		renderPassInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass!");
		}
	}

	void createFramebuffers() {
		swapChainFramebuffers.resize(swapChainImageViews.size());

		for (size_t i = 0; i < swapChainImageViews.size(); i++) {
			std::array<VkImageView, 5> attachments = {
				colorImageView, // Multi sample color image
				depthImageView, // Multi sample depth image
				swapChainImageViews[i], // Singe sample swap chain image
				colorResolveImageView, // single sample color resolve image
				computeShaderOutImageView // compute shader output
			};

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapChainExtent.width;
			framebufferInfo.height = swapChainExtent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to create framebuffer!");
			}
		}
	}

	void createColorResources() {
		{
			VkFormat colorFormat = swapChainImageFormat;

			// change number of samples to msaaSamples
			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.extent.width = swapChainExtent.width;
			imageCreateInfo.extent.height = swapChainExtent.width;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.format = colorFormat;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			imageCreateInfo.samples = msaaSamples; // number of msaa samples
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &colorImage, &colorImageAllocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			colorImageView = createImageView(device, colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, colorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
		}
		{
			VkFormat colorFormat = swapChainImageFormat;

			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.extent.width = swapChainExtent.width;
			imageCreateInfo.extent.height = swapChainExtent.width;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.format = colorFormat;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT; 
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &colorResolveImage, &colorResolveImageAllocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			colorResolveImageView = createImageView(device, colorResolveImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, colorResolveImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
		}
		{
			VkFormat colorFormat = swapChainImageFormat;

			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.extent.width = swapChainExtent.width;
			imageCreateInfo.extent.height = swapChainExtent.width;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.format = colorFormat;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.usage = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &computeShaderOutImage, &computeShaderOutImageAllocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			computeShaderOutImageView = createImageView(device, computeShaderOutImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, computeShaderOutImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
		}
	}

	void createDepthResources() {
		VkFormat depthFormat = findDepthFormat(physicalDevice);

		// change number of samples to msaaSamples
		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = swapChainExtent.width;
		imageCreateInfo.extent.height = swapChainExtent.width;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.format = depthFormat;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageCreateInfo.samples = msaaSamples; // number of msaa samples
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create color image!");
		
		depthImageView = createImageView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1);

		transitionImageLayout(device, graphicsQueue, graphicsCommandPool, depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, 1);
	}

	void createCommandBuffers() {
		commandBuffers.resize(swapChainFramebuffers.size());
		
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = graphicsCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

		if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command buffers!");
		}

		for (size_t i = 0; i < commandBuffers.size(); i++) {
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
				throw std::runtime_error("failed to begin recording command buffer!");
			}

			model.cmdTransferData(commandBuffers[i]);

			// Image memory barrier to make sure that compute shader writes are finished before sampling from the texture
			// Let the compute shader finish wrting before starting the second subpass
			std::array<VkImageMemoryBarrier, 2> imageMemoryBarriers = {};
			imageMemoryBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarriers[0].image = computeShaderOutImage;
			imageMemoryBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			imageMemoryBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			imageMemoryBarriers[0].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

			// Let the compute shader finish reading before staring the first subpass
			imageMemoryBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarriers[1].image = colorResolveImage;
			imageMemoryBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			imageMemoryBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			imageMemoryBarriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			
			vkCmdPipelineBarrier(
				commandBuffers[i],
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0,
				0, nullptr,           
				0, nullptr,
				static_cast<uint32_t>(imageMemoryBarriers.size()),
				imageMemoryBarriers.data());
		
			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = swapChainFramebuffers[i];
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = swapChainExtent;

			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
			clearValues[1].depthStencil = { 1.0f, 0 };

			renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipeline);

			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipelineLayout, 0, 1, &subpass1.descriptorSet, 0, nullptr);

			// put model draw
			model.cmdDraw(commandBuffers[i]);

			vkCmdNextSubpass(commandBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipelineLayout, 0, 1, &subpass2.descriptorSet, 0, nullptr);
			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);

			if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to record command buffer!");
			}
		}
	}

	void createComputeCommandBuffer()
	{	
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = computeCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		if (vkAllocateCommandBuffers(device, &allocInfo, &computeCommandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate compute command buffers!");
		}

		// Flush the queue if we're rebuilding the command buffer after a pipeline change to ensure it's not currently in use
		vkQueueWaitIdle(computeQueue);

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT };

		if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin recording compute command buffer!");
		}
		
		vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);
		vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipelineLayout, 0, 1, &computePipeline.descriptorSet, 0, 0);

		vkCmdDispatch(computeCommandBuffer, swapChainExtent.width / 16, swapChainExtent.height / 16, 1);

		vkEndCommandBuffer(computeCommandBuffer);
	}

	void createComputeSyncObject() {
		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		if (vkCreateFence(device, &fenceInfo, nullptr, &computeShaderFence) != VK_SUCCESS)
			throw std::runtime_error("failed to create synchronization objects for compute shader!");
	}
	
	void drawFrame() {
		
		uint32_t imageIndex = frameBegin();
		if (imageIndex == 0xffffffff)
			return;

		model.updateMeshData();
		cam.updateProjViewMat(io, swapChainExtent.width, swapChainExtent.height);

		submitRenderCmd(commandBuffers[imageIndex]);

		VkSubmitInfo computeSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;

		if (vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, nullptr) != VK_SUCCESS)
			throw std::runtime_error("failed to submit compute command buffer!");

		frameEnd(imageIndex);
	}
};

/*
int main() {
	{
		GraphicsComputeApplication app;

		try {
			app.run(1280, 720, true);
		}
		catch (const std::exception & e) {
			std::cerr << e.what() << std::endl;
			return EXIT_FAILURE;
		}
	}

	int i;
	std::cin >> i;
	return EXIT_SUCCESS;
}
*/



