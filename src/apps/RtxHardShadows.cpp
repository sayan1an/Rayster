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
	glm::vec3 lightPosition;
};

class NewGui : public Gui
{
public:
	const IO* io;
	PushConstantBlock pcb;
private:

	const char* items[8] = { "Diffuse Color", "Specular Color", "World-space Normal", "View-space depth", "Internal IOR", "External IOR", "Specular roughness", "Material type" };
	const char* currentItem = items[0];
	float lightX = 1;
	float lightY = 1;
	float lightZ = 1;
	float scale = 10;

	void guiSetup()
	{
		io->frameRateWidget();
		ImGui::SetCursorPos(ImVec2(5, 135));
		ImGui::SliderFloat("Light direction - x", &lightX, -1.0f, 1.0f);
		ImGui::SetCursorPos(ImVec2(5, 160));
		ImGui::SliderFloat("Light direction - y", &lightY, -1.0f, 1.0f);
		ImGui::SetCursorPos(ImVec2(5, 185));
		ImGui::SliderFloat("Light direction - z", &lightZ, -1.0f, 1.0f);
		ImGui::SetCursorPos(ImVec2(5, 210));
		ImGui::SliderFloat("Scale", &scale, 1.0f, 200.0f);
		pcb.lightPosition = glm::vec3(lightX, lightY, lightZ) * scale;
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

	void createPipeline(const VkDevice& device, const VkPhysicalDeviceRayTracingPropertiesNV& raytracingProperties, const VmaAllocator& allocator, const Model& model, const Camera& cam, FboManager &fboMgr)
	{
		descGen.bindTLAS({ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, model.getDescriptorTlas());
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, fboMgr.getImageView("diffuseColor"), VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindBuffer({ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, cam.getDescriptorBufferInfo());
		descGen.bindBuffer({ 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getMaterialDescriptorBufferInfo());
		descGen.bindBuffer({ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getVertexDescriptorBufferInfo());
		descGen.bindBuffer({ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getIndexDescriptorBufferInfo());
		descGen.bindBuffer({ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getStaticInstanceDescriptorBufferInfo());
		descGen.bindImage({ 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, { model.hdrTextureSampler,  model.hdrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		
		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		uint32_t rayGenId = rtxPipeGen.addRayGenShaderStage(device, ROOT + "/shaders/RtxHardShadows/01_raygen.spv");
		uint32_t missShaderId0 = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxHardShadows/01_miss.spv");
		uint32_t missShaderId1 = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxHardShadows/02_miss.spv");
		uint32_t hitGroupId0 = rtxPipeGen.startHitGroup();
		rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxHardShadows/01_close.spv");
		rtxPipeGen.endHitGroup();

		// Add an empty hit group
		uint32_t hitGroupId1 = rtxPipeGen.startHitGroup();
		//rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxHardShadows/02_close.spv");
		rtxPipeGen.endHitGroup();

		rtxPipeGen.setMaxRecursionDepth(1);
		rtxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_RAYGEN_BIT_NV , 0, sizeof(PushConstantBlock) });

		rtxPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

		sbtGen.addRayGenerationProgram(rayGenId, {});
		sbtGen.addMissProgram(missShaderId0, {});
		sbtGen.addMissProgram(missShaderId1, {});
		sbtGen.addHitGroup(hitGroupId0, {});
		sbtGen.addHitGroup(hitGroupId1, {});

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
			"failed to allocate buffer for shader binding table!");

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

	void createSubpassDescription(const VkDevice& device, FboManager& fboMgr)
	{
		colorAttachmentRef = fboMgr.getAttachmentReference("swapchain", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("diffuseColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
			
		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs.size());
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, FboManager& fboMgr)
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("diffuseColor"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/RtxHardShadows/gShowVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/RtxHardShadows/gShowFrag.spv");
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

class RTXApplication : public WindowApplication 
{
public:
	RTXApplication(const std::vector<const char*>& _instanceExtensions, const std::vector<const char*>& _deviceExtensions) : 
		WindowApplication(std::vector<const char*>(), _instanceExtensions, _deviceExtensions, std::vector<const char*>()) {}
private:
	VkPhysicalDeviceRayTracingPropertiesNV raytracingProperties = {};

	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkRenderPass renderPass;

	RtxPass rtxPass;
	Subpass1 subpass1;
	
	VkImage diffuseColorImage;
	VmaAllocation diffuseColorImageAllocation;
	VkImageView diffuseColorImageView;

	Model model;

	FboManager fboManager;

	NewGui gui;

	std::vector<VkCommandBuffer> commandBuffers;
	PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV = nullptr;

	void init() 
	{
		fboManager.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColorImageView);
		fboManager.addColorAttachment("swapchain", swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, swapChainImageViews.data(), static_cast<uint32_t>(swapChainImageViews.size()));

		getRtxProperties();
		loadScene(model, cam);
		
		vkCmdTraceRaysNV = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysNV"));
		subpass1.createSubpassDescription(device, fboManager);
		createRenderPass();

		createColorResources();
		createFramebuffers();

		gui.io = &io;
		gui.setStyle();
		gui.createResources(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool, renderPass, 0);

		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		model.createRtxBuffers(device, allocator, graphicsQueue, graphicsCommandPool);
		
		rtxPass.createPipeline(device, raytracingProperties, allocator, model, cam, fboManager);
		subpass1.createSubpass(device, swapChainExtent, renderPass, fboManager);
		
		createCommandBuffers();
	}

	void getRtxProperties()
	{
		// Query the values of shaderHeaderSize and maxRecursionDepth in current implementation
		raytracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
		raytracingProperties.pNext = nullptr;
		raytracingProperties.maxRecursionDepth = 0;
		raytracingProperties.shaderGroupHandleSize = 0;
		
		VkPhysicalDeviceProperties2 props;
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props.pNext = &raytracingProperties;
		props.properties = {};
		vkGetPhysicalDeviceProperties2(physicalDevice, &props);
	}
	
	void cleanUpAfterSwapChainResize() 
	{
		vkDestroyImageView(device, diffuseColorImageView, nullptr);
		vmaDestroyImage(allocator, diffuseColorImage, diffuseColorImageAllocation);
		
		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}

		vkFreeCommandBuffers(device, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

		vkDestroyPipeline(device, subpass1.pipeline, nullptr);
		vkDestroyPipelineLayout(device, subpass1.pipelineLayout, nullptr);

		vkDestroyPipeline(device, rtxPass.pipeline, nullptr);
		vkDestroyPipelineLayout(device, rtxPass.pipelineLayout, nullptr);

		vmaDestroyBuffer(allocator, rtxPass.sbtBuffer, rtxPass.sbtBufferAllocation);

		vkDestroyRenderPass(device, renderPass, nullptr);

		vkDestroyDescriptorSetLayout(device, rtxPass.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, subpass1.descriptorSetLayout, nullptr);
		
		vkDestroyDescriptorPool(device, rtxPass.descriptorPool, nullptr);
		vkDestroyDescriptorPool(device, subpass1.descriptorPool, nullptr);
	}

	void recreateAfterSwapChainResize() 
	{
		createRenderPass();
		createColorResources();
		createFramebuffers();

		rtxPass.createPipeline(device, raytracingProperties, allocator, model, cam, fboManager);
		subpass1.createSubpass(device, swapChainExtent, renderPass, fboManager);
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
		// this corresponds to the input attachment image after RTX pass
		VkAttachmentDescription attachment = {};
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		fboManager.updateAttachmentDescription("diffuseColor", attachment);
	
		// this corresponds to the color attachment image
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		fboManager.updateAttachmentDescription("swapchain", attachment);

		std::array<VkSubpassDescription, 1> subpassDesc = { subpass1.subpassDescription };

		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		std::vector<VkAttachmentDescription> attachments;
		fboManager.getAttachmentDescriptions(attachments);

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDesc.size());
		renderPassInfo.pSubpasses = subpassDesc.data();
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass),
			"RtxGBuffer: failed to create render pass!");
	}

	void createFramebuffers() 
	{
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

			VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]),
				"RtxGBufferApp:failed to create framebuffer!");
		}
	}

	void createColorResources() 
	{
		auto makeColorImage = [&device = device, &graphicsQueue = graphicsQueue,
			&graphicsCommandPool = graphicsCommandPool, &allocator = allocator,
			&swapChainExtent = swapChainExtent](VkFormat colorFormat, VkSampleCountFlagBits samples, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
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
			imageCreateInfo.usage = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imageCreateInfo.samples = samples;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			imageView = createImageView(device, image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, image, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
		};
		
		makeColorImage(fboManager.getFormat("diffuseColor"), fboManager.getSampleCount("diffuseColor"), diffuseColorImage, diffuseColorImageView, diffuseColorImageAllocation);
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
			"RtxGBufferApp: failed to allocate command buffers!");
	}

	void buildCommandBuffer(uint32_t index)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		VK_CHECK_DBG_ONLY(vkBeginCommandBuffer(commandBuffers[index], &beginInfo),
			"RtxGBufferApp: failed to begin recording command buffer!");
		
		model.cmdTransferData(commandBuffers[index]);
		model.cmdUpdateTlas(commandBuffers[index]);

		vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtxPass.pipeline);
		vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtxPass.pipelineLayout, 0, 1, &rtxPass.descriptorSet, 0, nullptr);
		vkCmdPushConstants(commandBuffers[index], rtxPass.pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(PushConstantBlock), &gui.pcb);

		// Calculate shader binding offsets, which is pretty straight forward in our example
		VkDeviceSize rayGenOffset = rtxPass.sbtGen.getRayGenOffset();
		VkDeviceSize missOffset = rtxPass.sbtGen.getMissOffset();
		VkDeviceSize missStride = rtxPass.sbtGen.getMissEntrySize();
		VkDeviceSize hitGroupOffset = rtxPass.sbtGen.getHitGroupOffset();
		VkDeviceSize hitGroupStride = rtxPass.sbtGen.getHitGroupEntrySize();

		vkCmdTraceRaysNV(commandBuffers[index], rtxPass.sbtBuffer, rayGenOffset,
			rtxPass.sbtBuffer, missOffset, missStride,
			rtxPass.sbtBuffer, hitGroupOffset, hitGroupStride,
			VK_NULL_HANDLE, 0, 0, swapChainExtent.width,
			swapChainExtent.height, 1);
					
		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapChainFramebuffers[index];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
						
		vkCmdBeginRenderPass(commandBuffers[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipeline);
		vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipelineLayout, 0, 1, &subpass1.descriptorSet, 0, nullptr);
		vkCmdDraw(commandBuffers[index], 3, 1, 0, 0);

		gui.cmdDraw(commandBuffers[index]);

		vkCmdEndRenderPass(commandBuffers[index]);
			
		VK_CHECK_DBG_ONLY(vkEndCommandBuffer(commandBuffers[index]),
			"RtxGBufferApp : failed to record command buffer!");
	}

	void drawFrame() 
	{
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
		std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME};
		std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
		RTXApplication app(instanceExtensions, deviceExtensions);

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



