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
#include "../gui.h"

struct PushConstantBlock
{
	uint32_t select = 0;
	float scale = 1;
};

class NewGui : public Gui
{	
public:
	const IO* io;
	PushConstantBlock pcb;
private:
	
	const char* items[9] = { "Diffuse Color", "Specular Color", "World-space Normal", "View-space depth", "Clip-space depth", "Internal IOR", "External IOR", "Specular roughness", "Material type" };
	const char* currentItem = items[0];
	float scaleCoarse = 1;
	float scaleFine = 1;
	
	void guiSetup()
	{
		io->frameRateWidget();
		ImGui::SetCursorPos(ImVec2(5, 110));

		if (ImGui::BeginCombo("Select", currentItem)) // The second parameter is the label previewed before opening the combo.
		{
			for (int n = 0; n < IM_ARRAYSIZE(items); n++)
			{
				bool is_selected = (currentItem == items[n]); // You can store your selection however you want, outside or inside your objects
				if (ImGui::Selectable(items[n], is_selected)) {
					currentItem = items[n];
					pcb.select = static_cast<uint32_t>(n);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
			}
			ImGui::EndCombo();
		}
		ImGui::SetCursorPos(ImVec2(5, 135));
		ImGui::SliderFloat("Scale - Coarse", &scaleCoarse, 0.01f, 10.0f);
		ImGui::SetCursorPos(ImVec2(5, 160));
		ImGui::SliderFloat("Scale - Fine", &scaleFine, 0.01f, 1.0f);
		pcb.scale = scaleCoarse * scaleFine;
	}
};

class Subpass1 {
public:
	VkSubpassDescription subpassDescription;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	void createSubpassDescription(const VkDevice& device, FboManager &fboMgr) {
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("diffuseColor", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("specularColor", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("normal", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("other", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		depthAttachmentRef = fboMgr.getAttachmentReference("depth", VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			
		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
		subpassDescription.pColorAttachments = colorAttachmentRefs.data();
		subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, const Camera& cam, const VkImageView& ldrTextureImageView, const VkSampler& ldrTextureSampler, const VkImageView& hdrTextureImageView, const VkSampler& hdrTextureSampler)
	{
		descGen.bindBuffer({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }, cam.getDescriptorBufferInfo());
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { ldrTextureSampler,  ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { hdrTextureSampler,  hdrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		auto bindingDescription = Model::getBindingDescription();
		auto attributeDescription = Model::getAttributeDescriptions();

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/GBuffer/gBufVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/GBuffer/gBufFrag.spv");
		gfxPipeGen.addVertexInputState(bindingDescription, attributeDescription);
		gfxPipeGen.addViewportState(swapChainExtent);
		gfxPipeGen.addColorBlendAttachmentState(4);
		
		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
	}
private:
	std::vector<VkAttachmentReference> colorAttachmentRefs;
	VkAttachmentReference depthAttachmentRef;
	DescriptorSetGenerator descGen;
	GraphicsPipelineGenerator gfxPipeGen;
};

class Subpass2 {
public:
	VkSubpassDescription subpassDescription = {};

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

	void createSubpassDescription(const VkDevice& device, FboManager &fboMgr)
	{
		colorAttachmentRef = fboMgr.getAttachmentReference("swapchain", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("diffuseColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("specularColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("normal", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("other", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs.size());
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, 
			const VkImageView& inputImageView0, const VkImageView& inputImageView1, const VkImageView& inputImageView2, 
			const VkImageView& inputImageView3, const VkImageView& inputImageView4) 
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView0,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView1,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView2,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView3,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView4,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		gfxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_FRAGMENT_BIT , 0, sizeof(PushConstantBlock) });
		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/GBuffer/gShowVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/GBuffer/gShowFrag.spv");
		gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
		gfxPipeGen.addDepthStencilState(VK_FALSE, VK_FALSE);
		gfxPipeGen.addViewportState(swapChainExtent);
	
		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 1, &pipeline, &pipelineLayout);
	}

private:
	VkAttachmentReference colorAttachmentRef;
	std::vector<VkAttachmentReference> inputAttachmentRefs;
	DescriptorSetGenerator descGen;
	GraphicsPipelineGenerator gfxPipeGen;
};

class GBufferApplication : public WindowApplication {
public:
	GBufferApplication() : WindowApplication(std::vector<const char*>(), std::vector<const char*>(), std::vector<const char*>(), std::vector<const char*>()) {}
private:
	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkRenderPass renderPass;

	Subpass1 subpass1;
	Subpass2 subpass2;

	VkImage diffuseColorImage;
	VmaAllocation diffuseColorImageAllocation;
	VkImageView diffuseColorImageView;

	VkImage specularColorImage;
	VmaAllocation specularColorImageAllocation;
	VkImageView specularColorImageView;

	// conatins normal + specular alpha
	VkImage normalImage;
	VmaAllocation normalImageAllocation;
	VkImageView normalImageView;

	// contains distance from camera, Int ior, Ext ior and material bsdf type
	VkImage otherInfoImage;
	VmaAllocation otherInfoImageAllocation;
	VkImageView otherInfoImageView;

	VkImage depthImage;
	VmaAllocation depthImageAllocation;
	VkImageView depthImageView;

	Model model;

	std::vector<VkCommandBuffer> commandBuffers;

	FboManager fboManager;

	NewGui gui;

	void init() 
	{
		fboManager.addDepthAttachment("depth", findDepthFormat(physicalDevice), VK_SAMPLE_COUNT_1_BIT, &depthImageView);
		fboManager.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColorImageView);
		fboManager.addColorAttachment("specularColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &specularColorImageView);
		fboManager.addColorAttachment("normal", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &normalImageView, 1, {0.0f, 0.0f, 0.0f, 0.0f});
		fboManager.addColorAttachment("other", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &otherInfoImageView, 1, {-1.0f, 0.0f, 0.0f, -1.0f});
		fboManager.addColorAttachment("swapchain", swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, swapChainImageViews.data(), static_cast<uint32_t>(swapChainImageViews.size()));

		loadScene(model, cam, "spaceship");
		subpass1.createSubpassDescription(device, fboManager);
		subpass2.createSubpassDescription(device, fboManager);
		createRenderPass();

		createColorResources();
		createDepthResources();
		createFramebuffers();

		gui.io = &io;
		gui.setStyle();
		gui.createResources(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool, renderPass, 1);
		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		subpass1.createSubpass(device, swapChainExtent, renderPass, cam, model.ldrTextureImageView, model.ldrTextureSampler, model.hdrTextureImageView, model.hdrTextureSampler);
		subpass2.createSubpass(device, swapChainExtent, renderPass, diffuseColorImageView, specularColorImageView, normalImageView, otherInfoImageView, depthImageView);
		createCommandBuffers();
	}

	void cleanUpAfterSwapChainResize() {
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(allocator, depthImage, depthImageAllocation);

		vkDestroyImageView(device, diffuseColorImageView, nullptr);
		vmaDestroyImage(allocator, diffuseColorImage, diffuseColorImageAllocation);

		vkDestroyImageView(device, specularColorImageView, nullptr);
		vmaDestroyImage(allocator, specularColorImage, specularColorImageAllocation);

		vkDestroyImageView(device, normalImageView, nullptr);
		vmaDestroyImage(allocator, normalImage, normalImageAllocation);

		vkDestroyImageView(device, otherInfoImageView, nullptr);
		vmaDestroyImage(allocator, otherInfoImage, otherInfoImageAllocation);

		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}

		vkFreeCommandBuffers(device, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

		vkDestroyPipeline(device, subpass1.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass1.pipelineLayout, nullptr);

		vkDestroyPipeline(device, subpass2.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass2.pipelineLayout, nullptr);

		vkDestroyRenderPass(device, renderPass, nullptr);

		vkDestroyDescriptorSetLayout(device, subpass1.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, subpass2.descriptorSetLayout, nullptr);

		vkDestroyDescriptorPool(device, subpass1.descriptorPool, nullptr);
		vkDestroyDescriptorPool(device, subpass2.descriptorPool, nullptr);
	}

	void recreateAfterSwapChainResize() {
		createRenderPass();
		createColorResources();
		createDepthResources();
		createFramebuffers();

		subpass1.createSubpass(device, swapChainExtent, renderPass, cam, model.ldrTextureImageView, model.ldrTextureSampler, model.hdrTextureImageView, model.hdrTextureSampler);
		subpass2.createSubpass(device, swapChainExtent, renderPass, diffuseColorImageView, specularColorImageView, normalImageView, otherInfoImageView, depthImageView);
		createCommandBuffers();
	}

	void cleanupFinal() 
	{	
		gui.cleanUp(device, allocator);
		model.cleanUp(device, allocator);
	}

	void createRenderPass() 
	{
		VkAttachmentDescription attachment = {};
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		fboManager.updateAttachmentDescription("diffuseColor", attachment);
		fboManager.updateAttachmentDescription("specularColor", attachment);
		fboManager.updateAttachmentDescription("normal", attachment);
		fboManager.updateAttachmentDescription("other", attachment);

		attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		fboManager.updateAttachmentDescription("depth", attachment);
		
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		fboManager.updateAttachmentDescription("swapchain", attachment);

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

		std::vector<VkAttachmentDescription> attachments;
		fboManager.getAttachmentDescriptions(attachments);
		
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
			std::vector<VkImageView> attachments;
			fboManager.getAttachments(attachments, static_cast<uint32_t>(i));

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

	void createColorResources() 
	{	
		auto makeColorImage = [&device = device, &graphicsQueue = graphicsQueue, 
				&graphicsCommandPool = graphicsCommandPool, &allocator = allocator, 
				&swapChainExtent = swapChainExtent](VkFormat colorFormat, VkSampleCountFlagBits samples, VkImage &image, VkImageView &imageView, VmaAllocation &allocation)
		{
			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.extent.width = swapChainExtent.width;
			imageCreateInfo.extent.height = swapChainExtent.height;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.format = colorFormat;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
			imageCreateInfo.samples = samples;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			imageView = createImageView(device, image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, image, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
		};

		makeColorImage(fboManager.getFormat("diffuseColor") , fboManager.getSampleCount("diffuseColor"), diffuseColorImage, diffuseColorImageView, diffuseColorImageAllocation);
		makeColorImage(fboManager.getFormat("specularColor"), fboManager.getSampleCount("specularColor"), specularColorImage, specularColorImageView, specularColorImageAllocation);
		makeColorImage(fboManager.getFormat("normal"), fboManager.getSampleCount("normal"), normalImage, normalImageView, normalImageAllocation);
		makeColorImage(fboManager.getFormat("other"), fboManager.getSampleCount("other"), otherInfoImage, otherInfoImageView, otherInfoImageAllocation);	
	}

	void createDepthResources() 
	{
		VkFormat depthFormat = fboManager.getFormat("depth");

		// change number of samples to msaaSamples
		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = swapChainExtent.width;
		imageCreateInfo.extent.height = swapChainExtent.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.format = depthFormat;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		imageCreateInfo.samples = fboManager.getSampleCount("depth");
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &depthImage, &depthImageAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create color image!");

		depthImageView = createImageView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1);

		transitionImageLayout(device, graphicsQueue, graphicsCommandPool, depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, 1);
	}

	void createCommandBuffers()
	{
		commandBuffers.resize(swapChainFramebuffers.size());

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = graphicsCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

		if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command buffers!");
		}
	}

	void buildCommandBuffer(size_t index)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		if (vkBeginCommandBuffer(commandBuffers[index], &beginInfo) != VK_SUCCESS)
			throw std::runtime_error("failed to begin recording command buffer!");
		
		model.cmdTransferData(commandBuffers[index]);

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapChainFramebuffers[index];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;

		std::vector<VkClearValue> clearValues = fboManager.getClearValues();
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();
		
		vkCmdBeginRenderPass(commandBuffers[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipeline);

		vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipelineLayout, 0, 1, &subpass1.descriptorSet, 0, nullptr);

		// put model draw
		model.cmdDraw(commandBuffers[index]);
		
		vkCmdNextSubpass(commandBuffers[index], VK_SUBPASS_CONTENTS_INLINE);
			
		vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipeline);
		vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipelineLayout, 0, 1, &subpass2.descriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffers[index], subpass2.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantBlock), &gui.pcb);
		vkCmdDraw(commandBuffers[index], 3, 1, 0, 0);
			
		gui.cmdDraw(commandBuffers[index]);

		vkCmdEndRenderPass(commandBuffers[index]);

		if (vkEndCommandBuffer(commandBuffers[index]) != VK_SUCCESS)
			throw std::runtime_error("failed to record command buffer!");
	}

	void drawFrame() {
		uint32_t imageIndex = frameBegin();
		if (imageIndex == 0xffffffff)
			return;

		gui.buildGui(io);
		gui.uploadData(device, allocator);
		model.updateMeshData();
		cam.updateProjViewMat(io, swapChainExtent.width, swapChainExtent.height);

		buildCommandBuffer(imageIndex);
		submitRenderCmd(commandBuffers[imageIndex]);

		frameEnd(imageIndex);
	}
};

/*
int main() 
{
	{
		GBufferApplication app;

		try {
			app.run(1280, 720, false);
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

