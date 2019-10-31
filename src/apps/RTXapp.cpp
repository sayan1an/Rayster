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

		uint32_t rayGenId = rtxPipeGen.addRayGenShaderStage(device, ROOT + "/shaders/RTXApp/01_rgen.spv");
		uint32_t missShaderId = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RTXApp/01_rmiss.spv");
		uint32_t hitGroupId = rtxPipeGen.startHitGroup();

		rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RTXApp/01_rchit.spv");
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
		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &sbtBuffer, &sbtBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("failed to allocate buffer for shader binding table!");

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

	void createSubpassDescription(const VkDevice& device) 
	{
		colorAttachmentRef = {};
		colorAttachmentRef.attachment = 2; // index to framebuffer
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		inputAttachmentRefs.push_back({ 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		inputAttachmentRefs.push_back({ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = 2;
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, const VkImageView& inputImageView0, const VkImageView &inputImageView1) {
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView0,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE , inputImageView1,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);
		
		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/02_vert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/02_frag.spv");
		gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
		gfxPipeGen.addDepthStencilState(VK_FALSE);
		gfxPipeGen.addViewportState(swapChainExtent);

		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
	}
private:
	VkAttachmentReference colorAttachmentRef;
	std::vector<VkAttachmentReference> inputAttachmentRefs;
	DescriptorSetGenerator descGen;
	GraphicsPipelineGenerator gfxPipeGen;
};



class RTXApplication : public WindowApplication {
public:
	RTXApplication(const std::vector<const char*>& _instanceExtensions, const std::vector<const char*>& _deviceExtensions) : 
		WindowApplication(std::vector<const char*>(), _instanceExtensions, _deviceExtensions, std::vector<const char*>()) {}
private:
	VkPhysicalDeviceRayTracingPropertiesNV raytracingProperties = {};

	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkRenderPass renderPass;

	RtxPass rtxPass;
	Subpass1 subpass1;
	
	VkImage colorImage;
	VmaAllocation colorImageAllocation;
	VkImageView colorImageView;

	VkImage depthImage;
	VmaAllocation depthImageAllocation;
	VkImageView depthImageView;

	Model model;

	std::vector<VkCommandBuffer> commandBuffers;
	VkCommandBuffer computeCommandBuffer;
	PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV = nullptr;
	void init() {
		getRtxProperties();
		loadScene(model, cam);
		
		vkCmdTraceRaysNV = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysNV"));
		subpass1.createSubpassDescription(device);
		createRenderPass();

		createColorResources();
		createDepthResources();
		createFramebuffers();

		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		model.createRtxBuffers(device, allocator, graphicsQueue, graphicsCommandPool);
		
		rtxPass.createPipeline(device, raytracingProperties, allocator, model, colorImageView, cam);
		subpass1.createSubpass(device, swapChainExtent, renderPass, depthImageView, colorImageView);
		
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
	

	void cleanUpAfterSwapChainResize() {
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(allocator, depthImage, depthImageAllocation);

		vkDestroyImageView(device, colorImageView, nullptr);
		vmaDestroyImage(allocator, colorImage, colorImageAllocation);

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

	void recreateAfterSwapChainResize() {
		createRenderPass();
		createColorResources();
		createDepthResources();
		createFramebuffers();

		rtxPass.createPipeline(device, raytracingProperties, allocator, model, colorImageView, cam);
		subpass1.createSubpass(device, swapChainExtent, renderPass, depthImageView, colorImageView);
		createCommandBuffers();
	}

	void cleanupFinal() {
		model.cleanUpRtx(device, allocator);
		model.cleanUp(device, allocator);
	}

	void createRenderPass() {
		// Overall idea: Vulkan will resolve/change multi-sample color image to regular swap chain comaptible/presentable image
		// Hence we have to attach a colorAttachemnt (MSAA image) and colorAttachmentResolve(swap chain image).

		// This corresponds to the multi-sampled color buffer
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = msaaSamples;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkAttachmentDescription depthAttachment = {};
		depthAttachment.format = findDepthFormat(physicalDevice);
		depthAttachment.samples = msaaSamples; // multiple samples
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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

		std::array<VkAttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, swapChainAttachment};
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDesc.size());
		renderPassInfo.pSubpasses = subpassDesc.data();
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass!");
		}
	}

	void createFramebuffers() {
		swapChainFramebuffers.resize(swapChainImageViews.size());

		for (size_t i = 0; i < swapChainImageViews.size(); i++) {
			std::array<VkImageView, 3> attachments = {
				colorImageView, // color image
				depthImageView, // depth image
				swapChainImageViews[i], // swap chain image
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
			imageCreateInfo.usage = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imageCreateInfo.samples = msaaSamples; // number of msaa samples
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &colorImage, &colorImageAllocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create color image!");

			colorImageView = createImageView(device, colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

			transitionImageLayout(device, graphicsQueue, graphicsCommandPool, colorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
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
		imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
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
			model.cmdUpdateTlas(commandBuffers[i]);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtxPass.pipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtxPass.pipelineLayout, 0, 1, &rtxPass.descriptorSet, 0, nullptr);

			// Calculate shader binding offsets, which is pretty straight forward in our example
			VkDeviceSize rayGenOffset = rtxPass.sbtGen.getRayGenOffset();
			VkDeviceSize missOffset = rtxPass.sbtGen.getMissOffset();
			VkDeviceSize missStride = rtxPass.sbtGen.getMissEntrySize();
			VkDeviceSize hitGroupOffset = rtxPass.sbtGen.getHitGroupOffset();
			VkDeviceSize hitGroupStride = rtxPass.sbtGen.getHitGroupEntrySize();

			vkCmdTraceRaysNV(commandBuffers[i], rtxPass.sbtBuffer, rayGenOffset,
				rtxPass.sbtBuffer, missOffset, missStride,
				rtxPass.sbtBuffer, hitGroupOffset, hitGroupStride,
				VK_NULL_HANDLE, 0, 0, swapChainExtent.width,
				swapChainExtent.height, 1);

			cmdTransitionImageLayout(commandBuffers[i], swapChainImages[i], swapChainImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);
			cmdTransitionImageLayout(commandBuffers[i], colorImage, swapChainImageFormat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1);

			VkImageCopy copyRegion{};
			copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copyRegion.srcOffset = { 0, 0, 0 };
			copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copyRegion.dstOffset = { 0, 0, 0 };
			copyRegion.extent = { swapChainExtent.width, swapChainExtent.height, 1 };
			vkCmdCopyImage(commandBuffers[i], colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			cmdTransitionImageLayout(commandBuffers[i], swapChainImages[i], swapChainImageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, 1);
			cmdTransitionImageLayout(commandBuffers[i], colorImage, swapChainImageFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			/*VkRenderPassBeginInfo renderPassInfo = {};
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
			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);
			*/
			if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to record command buffer!");
			}
		}
	}

	void drawFrame() {
		uint32_t imageIndex = frameBegin();
		if (imageIndex == 0xffffffff)
			return;

		model.updateMeshData();
		model.updateTlasData();
		cam.updateProjViewMat(io, swapChainExtent.width, swapChainExtent.height);

		submitRenderCmd(commandBuffers[imageIndex]);

		frameEnd(imageIndex);
	}
};

/*
int main() {
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
*/