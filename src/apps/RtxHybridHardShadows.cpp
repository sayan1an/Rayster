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

class RtxPass {
public:
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkBuffer sbtBuffer;
	VmaAllocation sbtBufferAllocation;
	ShaderBindingTableGenerator sbtGen;

	void createPipeline(const VkDevice& device, const VkPhysicalDeviceRayTracingPropertiesNV& raytracingProperties, const VmaAllocator& allocator, const Model& model, const VkImageView& storageImageView, const Camera& cam)
	{
		descGen.bindTLAS({ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, model.getDescriptorTlas());
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, storageImageView, VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindBuffer({ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, cam.getDescriptorBufferInfo());

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		uint32_t rayGenId = rtxPipeGen.addRayGenShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/01_raygen.spv");
		uint32_t missShaderId = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/01_miss.spv");
		uint32_t hitGroupId = rtxPipeGen.startHitGroup();

		rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/01_close.spv");
		rtxPipeGen.endHitGroup();
		rtxPipeGen.setMaxRecursionDepth(1);

		rtxPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

		sbtGen.addRayGenerationProgram(rayGenId, {});
		sbtGen.addMissProgram(missShaderId, {});
		sbtGen.addHitGroup(hitGroupId, {});

		VkDeviceSize shaderBindingTableSize = sbtGen.computeSBTSize(raytracingProperties);

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = shaderBindingTableSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		// Allocate memory and bind it to the buffer
		VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &sbtBuffer, &sbtBufferAllocation, nullptr),
			"RtxHybridShadows: failed to allocate buffer for shader binding table!");

		sbtGen.populateSBT(device, pipeline, allocator, sbtBufferAllocation);
	}
private:
	DescriptorSetGenerator descGen;
	RayTracingPipelineGenerator rtxPipeGen;
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

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, const Camera& cam, const Model &model)
	{
		descGen.bindBuffer({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }, cam.getDescriptorBufferInfo());
		descGen.bindBuffer({ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT }, model.getMaterialDescriptorBufferInfo());
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { model.hdrTextureSampler,  model.hdrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		auto bindingDescription = Model::getBindingDescription();
		auto attributeDescription = Model::getAttributeDescriptions();

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/gBufVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/gBufFrag.spv");
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
	
		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs.size());
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, FboManager &fboMgr) 
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("diffuseColor"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("specularColor"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("normal"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("other"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		
		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		gfxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_FRAGMENT_BIT , 0, sizeof(PushConstantBlock) });
		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/gShowVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/RtxHybridHardShadows/gShowFrag.spv");
		gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
		gfxPipeGen.addDepthStencilState(VK_FALSE, VK_FALSE);
		gfxPipeGen.addViewportState(swapChainExtent);
	
		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
	}

private:
	VkAttachmentReference colorAttachmentRef;
	std::vector<VkAttachmentReference> inputAttachmentRefs;
	DescriptorSetGenerator descGen;
	GraphicsPipelineGenerator gfxPipeGen;
};

class RtxHybridHardShadows : public WindowApplication {
public:
	RtxHybridHardShadows(const std::vector<const char*>& _instanceExtensions, const std::vector<const char*>& _deviceExtensions) :
		WindowApplication(std::vector<const char*>(), _instanceExtensions, _deviceExtensions, std::vector<const char*>()) {}
private:
	std::vector<VkFramebuffer> swapChainFramebuffers;
	VkFramebuffer renderPass1Fbo;

	VkRenderPass renderPass1;
	VkRenderPass renderPass2;

	Subpass1 subpass1;
	RtxPass rtxPass;
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

	FboManager fboManager1;
	FboManager fboManager2;

	NewGui gui;

	void init() 
	{
		fboManager1.addDepthAttachment("depth", findDepthFormat(physicalDevice), VK_SAMPLE_COUNT_1_BIT, &depthImageView);
		fboManager1.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColorImageView);
		fboManager1.addColorAttachment("specularColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &specularColorImageView);
		fboManager1.addColorAttachment("normal", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &normalImageView, 1, {0.0f, 0.0f, 0.0f, 0.0f});
		fboManager1.addColorAttachment("other", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &otherInfoImageView, 1, {-1.0f, 0.0f, 0.0f, -1.0f});

		fboManager2.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColorImageView);
		fboManager2.addColorAttachment("specularColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &specularColorImageView);
		fboManager2.addColorAttachment("normal", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &normalImageView, 1, { 0.0f, 0.0f, 0.0f, 0.0f });
		fboManager2.addColorAttachment("other", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &otherInfoImageView, 1, { -1.0f, 0.0f, 0.0f, -1.0f });
		fboManager2.addColorAttachment("swapchain", swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, swapChainImageViews.data(), static_cast<uint32_t>(swapChainImageViews.size()));

		loadScene(model, cam, "spaceship");
		subpass1.createSubpassDescription(device, fboManager1);
		subpass2.createSubpassDescription(device, fboManager2);
		createRenderPass();

		createColorResources();
		createDepthResources();
		createFramebuffers();

		gui.io = &io;
		gui.setStyle();
		gui.createResources(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool, renderPass2, 0);
		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		model.createRtxBuffers(device, allocator, graphicsQueue, graphicsCommandPool);

		subpass1.createSubpass(device, swapChainExtent, renderPass1, cam, model);
		rtxPass.createPipeline(device, raytracingProperties, allocator, model, diffuseColorImageView, cam);
		subpass2.createSubpass(device, swapChainExtent, renderPass2, fboManager2);
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

		vkDestroyFramebuffer(device, renderPass1Fbo, nullptr);
		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}

		vkFreeCommandBuffers(device, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

		vkDestroyPipeline(device, subpass1.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass1.pipelineLayout, nullptr);

		vkDestroyPipeline(device, rtxPass.pipeline, nullptr);
		vkDestroyPipelineLayout(device, rtxPass.pipelineLayout, nullptr);
		vmaDestroyBuffer(allocator, rtxPass.sbtBuffer, rtxPass.sbtBufferAllocation);

		vkDestroyPipeline(device, subpass2.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass2.pipelineLayout, nullptr);

		vkDestroyRenderPass(device, renderPass1, nullptr);
		vkDestroyRenderPass(device, renderPass2, nullptr);

		vkDestroyDescriptorSetLayout(device, subpass1.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, rtxPass.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, subpass2.descriptorSetLayout, nullptr);

		vkDestroyDescriptorPool(device, subpass1.descriptorPool, nullptr);
		vkDestroyDescriptorPool(device, rtxPass.descriptorPool, nullptr);
		vkDestroyDescriptorPool(device, subpass2.descriptorPool, nullptr);
	}

	void recreateAfterSwapChainResize() 
	{
		createRenderPass();
		
		createColorResources();
		createDepthResources();
		createFramebuffers();

		subpass1.createSubpass(device, swapChainExtent, renderPass1, cam, model);
		rtxPass.createPipeline(device, raytracingProperties, allocator, model, diffuseColorImageView, cam);
		subpass2.createSubpass(device, swapChainExtent, renderPass2, fboManager2);
		createCommandBuffers();
	}

	void cleanupFinal() 
	{	
		gui.cleanUp(device, allocator);
		model.cleanUpRtx(device, allocator);
		model.cleanUp(device, allocator);
	}
	
	void createRenderPass()
	{	
		// create renderpass 1 - i.e. Rasterized G-Buffer
		{
			VkAttachmentDescription attachment = {};
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			fboManager1.updateAttachmentDescription("diffuseColor", attachment);
			fboManager1.updateAttachmentDescription("specularColor", attachment);
			fboManager1.updateAttachmentDescription("normal", attachment);
			fboManager1.updateAttachmentDescription("other", attachment);

			attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			fboManager1.updateAttachmentDescription("depth", attachment);

			std::array<VkSubpassDescription, 1> subpassDesc = { subpass1.subpassDescription };

			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			std::vector<VkAttachmentDescription> attachments;
			fboManager1.getAttachmentDescriptions(attachments);

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = subpassDesc.data();
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass1),
				"failed to create render pass 1!");
		}
		// create renderpass 2 - i.e. final display and gui
		{
			VkAttachmentDescription attachment = {};
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			fboManager2.updateAttachmentDescription("diffuseColor", attachment);
			fboManager2.updateAttachmentDescription("specularColor", attachment);
			fboManager2.updateAttachmentDescription("normal", attachment);
			fboManager2.updateAttachmentDescription("other", attachment);

			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			fboManager2.updateAttachmentDescription("swapchain", attachment);

			std::array<VkSubpassDescription, 1> subpassDesc = { subpass2.subpassDescription };

			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			std::vector<VkAttachmentDescription> attachments;
			fboManager2.getAttachmentDescriptions(attachments);

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassInfo.pAttachments = attachments.data();
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = subpassDesc.data();
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();

			VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass2),
				"failed to create render pass 2!");
		}
	}

	void createFramebuffers() {
		{
			std::vector<VkImageView> attachments;
			fboManager1.getAttachments(attachments, static_cast<uint32_t>(0));

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass1;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapChainExtent.width;
			framebufferInfo.height = swapChainExtent.height;
			framebufferInfo.layers = 1;

			VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &renderPass1Fbo),
				"failed to create renderpass1 framebuffer!");
		}

		swapChainFramebuffers.resize(swapChainImageViews.size());
		for (size_t i = 0; i < swapChainImageViews.size(); i++) {
			std::vector<VkImageView> attachments;
			fboManager2.getAttachments(attachments, static_cast<uint32_t>(i));

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass2;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapChainExtent.width;
			framebufferInfo.height = swapChainExtent.height;
			framebufferInfo.layers = 1;

			VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]),
				"failed to create renderpass2 framebuffer!");
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
			imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
			imageCreateInfo.samples = samples;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			VK_CHECK(vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr),
				"Failed to create color image!");

			imageView = createImageView(device, image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		};

		makeColorImage(fboManager1.getFormat("diffuseColor") , fboManager1.getSampleCount("diffuseColor"), diffuseColorImage, diffuseColorImageView, diffuseColorImageAllocation);
		makeColorImage(fboManager1.getFormat("specularColor"), fboManager1.getSampleCount("specularColor"), specularColorImage, specularColorImageView, specularColorImageAllocation);
		makeColorImage(fboManager1.getFormat("normal"), fboManager1.getSampleCount("normal"), normalImage, normalImageView, normalImageAllocation);
		makeColorImage(fboManager1.getFormat("other"), fboManager1.getSampleCount("other"), otherInfoImage, otherInfoImageView, otherInfoImageAllocation);	
	}

	void createDepthResources() 
	{
		VkFormat depthFormat = fboManager1.getFormat("depth");

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
		imageCreateInfo.samples = fboManager1.getSampleCount("depth");
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VK_CHECK(vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &depthImage, &depthImageAllocation, nullptr),
			"Failed to create color image!");

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

		VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()),
			"failed to allocate command buffers!");
	}

	void buildCommandBuffer(size_t index)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		VK_CHECK_DBG_ONLY(vkBeginCommandBuffer(commandBuffers[index], &beginInfo),
			"failed to begin recording command buffer!");
		
		model.cmdTransferData(commandBuffers[index]);
		model.cmdUpdateTlas(commandBuffers[index]);

		// begin first render-pass
		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass1;
		renderPassInfo.framebuffer = renderPass1Fbo;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;

		std::vector<VkClearValue> clearValues = fboManager1.getClearValues();
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();
		
		vkCmdBeginRenderPass(commandBuffers[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipeline);
		vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipelineLayout, 0, 1, &subpass1.descriptorSet, 0, nullptr);
		// put model draw
		model.cmdDraw(commandBuffers[index]);
		vkCmdEndRenderPass(commandBuffers[index]);

		// begin second render-pass
		renderPassInfo.renderPass = renderPass2;
		renderPassInfo.framebuffer = swapChainFramebuffers[index];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
		renderPassInfo.clearValueCount = 0;
		renderPassInfo.pClearValues = VK_NULL_HANDLE;

		vkCmdBeginRenderPass(commandBuffers[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			
		vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipeline);
		vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipelineLayout, 0, 1, &subpass2.descriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffers[index], subpass2.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantBlock), &gui.pcb);
		vkCmdDraw(commandBuffers[index], 3, 1, 0, 0);
			
		gui.cmdDraw(commandBuffers[index]);

		vkCmdEndRenderPass(commandBuffers[index]);

		VK_CHECK_DBG_ONLY(vkEndCommandBuffer(commandBuffers[index]),
			"failed to record command buffer!");
	}

	void drawFrame() {
		uint32_t imageIndex = frameBegin();
		if (imageIndex == 0xffffffff)
			return;

		gui.buildGui(io);
		gui.uploadData(device, allocator);
		model.updateMeshData();
		model.updateTlasData();
		cam.updateProjViewMat(io, swapChainExtent.width, swapChainExtent.height);

		buildCommandBuffer(imageIndex);
		submitRenderCmd(commandBuffers[imageIndex]);

		frameEnd(imageIndex);
	}
};

int main() 
{
	{	
		std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
		std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
		RtxHybridHardShadows app(instanceExtensions, deviceExtensions);

		try {
			app.run(1280, 720, false);
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

