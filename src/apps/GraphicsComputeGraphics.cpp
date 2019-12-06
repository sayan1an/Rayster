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

	const char* items[8] = { "Diffuse Color", "Specular Color", "World-space Normal", "View-space depth", "Internal IOR", "External IOR", "Specular roughness", "Material type" };
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

	void createSubpassDescription(const VkDevice &device, FboManager &fboMgr) {
		// ref to multi-sample color buffer
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("diffuseColor", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("specularColor", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("normal", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("other", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		
		// ref to multi-sample depth buffer
		depthAttachmentRef = fboMgr.getAttachmentReference("depth", VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		
		// ref to color resolve image buffer
		colorAttachmentResolveRefs.push_back(fboMgr.getAttachmentReference("diffuseColorResolve", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentResolveRefs.push_back(fboMgr.getAttachmentReference("specularColorResolve", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentResolveRefs.push_back(fboMgr.getAttachmentReference("normalResolve", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		colorAttachmentResolveRefs.push_back(fboMgr.getAttachmentReference("otherResolve", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
		
		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
		subpassDescription.pColorAttachments = colorAttachmentRefs.data();
		subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;
		subpassDescription.pResolveAttachments = colorAttachmentResolveRefs.data();
	}
	
	void createSubpass(const VkDevice &device, const VkExtent2D &swapChainExtent, const VkSampleCountFlagBits &msaaSamples, const VkRenderPass &renderPass,
		const Camera &cam, const Model& model) {
		
		descGen.bindBuffer({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }, cam.getDescriptorBufferInfo());
		descGen.bindBuffer({ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT }, model.getMaterialDescriptorBufferInfo());
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { model.hdrTextureSampler,  model.hdrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		auto bindingDescription = Model::getBindingDescription();
		auto attributeDescription = Model::getAttributeDescriptions();

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/GBuffer/gBufVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/GBuffer/gBufFrag.spv");
		gfxPipeGen.addVertexInputState(bindingDescription, attributeDescription);
		gfxPipeGen.addViewportState(swapChainExtent);
		gfxPipeGen.addMsaaSate(msaaSamples);
		gfxPipeGen.addColorBlendAttachmentState(4);

		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
	}
private:
	std::vector<VkAttachmentReference> colorAttachmentRefs;
	VkAttachmentReference depthAttachmentRef;
	std::vector<VkAttachmentReference> colorAttachmentResolveRefs;

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

	void createSubpassDescription(const VkDevice& device, FboManager &fboMgr)
	{
		colorAttachmentRef = fboMgr.getAttachmentReference("swapchain", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("csout", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		
		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs.size());
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, const VkImageView& inputImageView) {
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/GraphicsComputeGraphicsApp/gShowVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/GraphicsComputeGraphicsApp/gShowFrag.spv");
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

class ComputePipeline {
public:
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	
	void createPipeline(const VkDevice &device, const VkImageView &inView0, const VkImageView &inView1, const VkImageView &inView2, const VkImageView &inView3, const VkImageView &outView)
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inView0,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inView1,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inView2,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inView3,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outView,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);
		
		compPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		compPipeGen.addComputeShaderStage(device, ROOT + "/shaders/GraphicsComputeGraphicsApp/edgeDetectComp.spv");
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

	struct MSAABuf 
	{
		VkImage image, imageResolve;
		VmaAllocation allocation, allocationResolve;
		VkImageView view, viewResolve;

		void cleanUp(const VkDevice &device, const VmaAllocator allocator)
		{
			vkDestroyImageView(device, view, nullptr);
			vmaDestroyImage(allocator, image, allocation);

			vkDestroyImageView(device, viewResolve, nullptr);
			vmaDestroyImage(allocator, imageResolve, allocationResolve);
		}
	};

	MSAABuf diffuseColor;
	MSAABuf specularColor;
	MSAABuf normal;
	MSAABuf otherInfo;
	
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

	FboManager fboManager;

	NewGui gui;

	void init() 
	{
		fboManager.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, msaaSamples, &diffuseColor.view);
		fboManager.addColorAttachment("specularColor", VK_FORMAT_R8G8B8A8_UNORM, msaaSamples, &specularColor.view);
		fboManager.addColorAttachment("normal", VK_FORMAT_R32G32B32A32_SFLOAT, msaaSamples, &normal.view, 1, { 0.0f, 0.0f, 0.0f, 0.0f });
		fboManager.addColorAttachment("other", VK_FORMAT_R32G32B32A32_SFLOAT, msaaSamples, &otherInfo.view, 1, { -1.0f, 0.0f, 0.0f, -1.0f });
		fboManager.addDepthAttachment("depth", findDepthFormat(physicalDevice), msaaSamples, &depthImageView);
		fboManager.addColorAttachment("diffuseColorResolve", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColor.viewResolve);
		fboManager.addColorAttachment("specularColorResolve", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &specularColor.viewResolve);
		fboManager.addColorAttachment("normalResolve", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &normal.viewResolve);
		fboManager.addColorAttachment("otherResolve", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &otherInfo.viewResolve);
		fboManager.addColorAttachment("swapchain", swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, swapChainImageViews.data(), static_cast<uint32_t>(swapChainImageViews.size()));
		fboManager.addColorAttachment("csout", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &computeShaderOutImageView);

		loadScene(model, cam);

		subpass1.createSubpassDescription(device, fboManager);
		subpass2.createSubpassDescription(device, fboManager);
		createRenderPass();
		
		createFboResources();
		createFramebuffers();
		
		gui.io = &io;
		gui.setStyle();
		gui.createResources(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool, renderPass, 1);
		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		subpass1.createSubpass(device, swapChainExtent, msaaSamples, renderPass, cam, model);
		subpass2.createSubpass(device, swapChainExtent, renderPass, computeShaderOutImageView);
		computePipeline.createPipeline(device, diffuseColor.viewResolve, specularColor.viewResolve, normal.viewResolve, otherInfo.viewResolve, computeShaderOutImageView);
		createCommandBuffers();
		createComputeCommandBuffer();
		createComputeSyncObject();
	}
	   
	void cleanUpAfterSwapChainResize() {
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(allocator, depthImage, depthImageAllocation);

		diffuseColor.cleanUp(device, allocator);
		specularColor.cleanUp(device, allocator);
		normal.cleanUp(device, allocator);
		otherInfo.cleanUp(device, allocator);
		
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
		createFboResources();
		createFramebuffers();

		subpass1.createSubpass(device, swapChainExtent, msaaSamples, renderPass, cam, model);
		subpass2.createSubpass(device, swapChainExtent, renderPass, computeShaderOutImageView);
		computePipeline.createPipeline(device, diffuseColor.viewResolve, specularColor.viewResolve, normal.viewResolve, otherInfo.viewResolve, computeShaderOutImageView);
		createCommandBuffers();
		createComputeCommandBuffer();
	}

	void cleanupFinal() 
	{	
		gui.cleanUp(device, allocator);
		model.cleanUp(device, allocator);
		vkDestroyFence(device, computeShaderFence, nullptr);
	}

	void createRenderPass() {
		// Overall idea: Vulkan will resolve/change multi-sample color image to regular swap chain comaptible/presentable image
		// Hence we have to attach a colorAttachemnt (MSAA image) and colorAttachmentResolve.

		// multi-sampled diffuse color buffer
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
		
		// depth buffer
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		fboManager.updateAttachmentDescription("depth", attachment);

		// swap chain images
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		fboManager.updateAttachmentDescription("swapchain", attachment);

		// diffuse resolve image
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		fboManager.updateAttachmentDescription("diffuseColorResolve", attachment);
		fboManager.updateAttachmentDescription("specularColorResolve", attachment);
		fboManager.updateAttachmentDescription("normalResolve", attachment);
		fboManager.updateAttachmentDescription("otherResolve", attachment);
		
		fboManager.updateAttachmentDescription("csout", attachment);
		
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

	void createFboResources() 
	{
		auto makeImage = [&device = device, &graphicsQueue = graphicsQueue,
			&graphicsCommandPool = graphicsCommandPool, &allocator = allocator,
			&swapChainExtent = swapChainExtent](VkFormat colorFormat, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageLayout layout, VkImage& image, VkImageView& imageView, VmaAllocation& allocation, boolean depthImage = false)
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
			imageCreateInfo.usage = usage;
			imageCreateInfo.samples = samples;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			imageView = createImageView(device, image, colorFormat, depthImage ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, image, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, layout, 1, 1);
		};

		// MSAA buffers
		makeImage(fboManager.getFormat("diffuseColor"), fboManager.getSampleCount("diffuseColor"), VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, diffuseColor.image, diffuseColor.view, diffuseColor.allocation);
		makeImage(fboManager.getFormat("specularColor"), fboManager.getSampleCount("specularColor"), VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, specularColor.image, specularColor.view, specularColor.allocation);
		makeImage(fboManager.getFormat("normal"), fboManager.getSampleCount("normal"), VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, normal.image, normal.view, normal.allocation);
		makeImage(fboManager.getFormat("other"), fboManager.getSampleCount("other"), VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, otherInfo.image, otherInfo.view, otherInfo.allocation);
		makeImage(fboManager.getFormat("depth"), fboManager.getSampleCount("depth"), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthImage, depthImageView, depthImageAllocation, true);

		// resolve buffers
		makeImage(fboManager.getFormat("diffuseColorResolve"), fboManager.getSampleCount("diffuseColorResolve"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, diffuseColor.imageResolve, diffuseColor.viewResolve, diffuseColor.allocationResolve);
		makeImage(fboManager.getFormat("specularColorResolve"), fboManager.getSampleCount("specularColorResolve"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, specularColor.imageResolve, specularColor.viewResolve, specularColor.allocationResolve);
		makeImage(fboManager.getFormat("normalResolve"), fboManager.getSampleCount("normalResolve"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, normal.imageResolve, normal.viewResolve, normal.allocationResolve);
		makeImage(fboManager.getFormat("otherResolve"), fboManager.getSampleCount("otherResolve"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, otherInfo.imageResolve, otherInfo.viewResolve, otherInfo.allocationResolve);
		
		makeImage(fboManager.getFormat("csout"), fboManager.getSampleCount("csout"), VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			VK_IMAGE_LAYOUT_GENERAL, computeShaderOutImage, computeShaderOutImageView, computeShaderOutImageAllocation);
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
			
		}
	}

	void buildGraphicsCommandBuffer(size_t index)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		if (vkBeginCommandBuffer(commandBuffers[index], &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin recording command buffer!");
		}

		model.cmdTransferData(commandBuffers[index]);

		// Image memory barrier to make sure that compute shader writes are finished before sampling from the texture
		// Let the compute shader finish wrting before starting the second subpass
		std::array<VkImageMemoryBarrier, 5> imageMemoryBarriers = {};
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
		imageMemoryBarriers[1].image = diffuseColor.imageResolve;
		imageMemoryBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageMemoryBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		imageMemoryBarriers[2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarriers[2].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarriers[2].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarriers[2].image = specularColor.imageResolve;
		imageMemoryBarriers[2].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageMemoryBarriers[2].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarriers[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		imageMemoryBarriers[3].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarriers[3].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarriers[3].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarriers[3].image = normal.imageResolve;
		imageMemoryBarriers[3].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageMemoryBarriers[3].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarriers[3].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		imageMemoryBarriers[4].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarriers[4].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarriers[4].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarriers[4].image = otherInfo.imageResolve;
		imageMemoryBarriers[4].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageMemoryBarriers[4].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarriers[4].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier(
			commandBuffers[index],
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
		vkCmdDraw(commandBuffers[index], 3, 1, 0, 0);

		gui.cmdDraw(commandBuffers[index]);

		vkCmdEndRenderPass(commandBuffers[index]);

		if (vkEndCommandBuffer(commandBuffers[index]) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
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

	}

 void buildComputeCommandBuffer()
	{
		// Flush the queue if we're rebuilding the command buffer after a pipeline change to ensure it's not currently in use
		//vkQueueWaitIdle(computeQueue);

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT };

		if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin recording compute command buffer!");
		}
		
		vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);
		vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipelineLayout, 0, 1, &computePipeline.descriptorSet, 0, 0);
		vkCmdPushConstants(computeCommandBuffer, computePipeline.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &gui.pcb);
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

		gui.buildGui(io);
		gui.uploadData(device, allocator);
		model.updateMeshData();
		cam.updateProjViewMat(io, swapChainExtent.width, swapChainExtent.height);

		buildGraphicsCommandBuffer(imageIndex);
		submitRenderCmd(commandBuffers[imageIndex]);

		vkWaitForFences(device, 1, &computeShaderFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		buildComputeCommandBuffer();
		VkSubmitInfo computeSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;
		vkResetFences(device, 1, &computeShaderFence);
		if (vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, computeShaderFence) != VK_SUCCESS)
			throw std::runtime_error("failed to submit compute command buffer!");

		frameEnd(imageIndex);
	}
};
/*
int main() 
{
	{
		GraphicsComputeApplication app;

		try {
			app.run(1280, 720, true);
		}
		catch (const std::exception & e) {
			std::cerr << e.what() << std::endl;
			//return EXIT_FAILURE;
		}
	}

	int i;
	std::cin >> i;
	return EXIT_SUCCESS;
}
*/



