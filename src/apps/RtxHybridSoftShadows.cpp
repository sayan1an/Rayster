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
#include "../lightSources.h"
#include "../filter.h"
#include <cstdlib>

struct PushConstantBlock
{
	glm::vec3 lightPosition;
	float power;
	uint32_t discretePdfSize;
	uint32_t numSamples;
	uint32_t seed;
};

class NewGui : public Gui
{
public:
	const IO* io;
	Camera* cam;
	CrossBilateralFilter* cFilter;
	TemporalFilter* tFilter;
	TemporalFrequencyFilter* tfFilter;
	PushConstantBlock pcb;
	int denoise = 0;
private:

	float lightX = 1;
	float lightY = -1;
	float lightZ = 1;
	float power = 10;
	float distance = 10;
	int numSamples = 4;

	void guiSetup()
	{
		io->frameRateWidget();
		cam->cameraWidget();
		ImGui::SliderFloat("Emitter direction - x", &lightX, -1.0f, 1.0f);
		ImGui::SliderFloat("Emitter direction - y", &lightY, -1.0f, 1.0f);
		ImGui::SliderFloat("Emitter direction - z", &lightZ, -1.0f, 1.0f);
		ImGui::SliderFloat("Emitter power", &power, 1.0f, 100.0f);
		ImGui::SliderFloat("Emitter distance", &distance, 1.0f, 25.0f);
		ImGui::SliderInt("MC Samples", &numSamples, 1, 64);
		ImGui::Text("Denoise"); ImGui::SameLine();
		ImGui::RadioButton("No", &denoise, 0); ImGui::SameLine();
		ImGui::RadioButton("Yes", &denoise, 1);
		//cFilter->widget();
		tFilter->widget();
		tfFilter->widget();
		pcb.lightPosition = glm::normalize(glm::vec3(lightX, lightY, lightZ)) * distance;
		pcb.power = power;
		pcb.numSamples = static_cast<uint32_t>(numSamples);
		pcb.seed = static_cast<uint32_t>(rand());
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

	void createPipeline(const VkDevice& device, const VkPhysicalDeviceRayTracingPropertiesNV& raytracingProperties, const VmaAllocator& allocator, 
		const Model& model, FboManager &fboMgr, const Camera& cam, const AreaLightSources &areaSource,
		const RandomGenerator &randGen)
	{
		descGen.bindTLAS({ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, model.getDescriptorTlas());
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, fboMgr.getImageView("diffuseColor"), VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, fboMgr.getImageView("specularColor"), VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, fboMgr.getImageView("normal"), VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, fboMgr.getImageView("other"), VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, fboMgr.getImageView("rtxOut"), VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindBuffer({ 6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, cam.getDescriptorBufferInfo());
		descGen.bindBuffer({ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, areaSource.getDescriptorBufferInfo());
		descGen.bindBuffer({ 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, areaSource.dPdf.getCdfNormDescriptorBufferInfo());
		descGen.bindBuffer({ 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getStaticInstanceDescriptorBufferInfo());
		descGen.bindBuffer({ 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getMaterialDescriptorBufferInfo());
		descGen.bindBuffer({ 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getVertexDescriptorBufferInfo());
		descGen.bindBuffer({ 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getIndexDescriptorBufferInfo());
		descGen.bindImage({ 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindBuffer({ 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_RAYGEN_BIT_NV }, randGen.getDescriptorBufferInfo());

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		uint32_t rayGenId = rtxPipeGen.addRayGenShaderStage(device, ROOT + "/shaders/RtxHybridSoftShadows/01_raygen.spv");
		uint32_t missShaderId0 = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxHybridSoftShadows/01_miss.spv");
		uint32_t missShaderId1 = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxHybridSoftShadows/02_miss.spv");
		uint32_t hitGroupId0 = rtxPipeGen.startHitGroup();
		rtxPipeGen.endHitGroup();
		uint32_t hitGroupId1 = rtxPipeGen.startHitGroup();
		rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxHybridSoftShadows/02_close.spv");
		rtxPipeGen.endHitGroup();
		rtxPipeGen.setMaxRecursionDepth(1);
		rtxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(PushConstantBlock) });
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
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("rtxOut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		inputAttachmentRefs.push_back(fboMgr.getAttachmentReference("filterOut", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		
		subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorAttachmentRef;
		subpassDescription.inputAttachmentCount = static_cast<uint32_t>(inputAttachmentRefs.size());
		subpassDescription.pInputAttachments = inputAttachmentRefs.data();
	}

	void createSubpass(const VkDevice& device, const VkExtent2D& swapChainExtent, const VkRenderPass& renderPass, FboManager &fboMgr) 
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("rtxOut"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { VK_NULL_HANDLE, fboMgr.getImageView("filterOut"),  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);
			
		gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/RtxHybridSoftShadows/gShowVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/RtxHybridSoftShadows/gShowFrag.spv");
		gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
		gfxPipeGen.addDepthStencilState(VK_FALSE, VK_FALSE);
		gfxPipeGen.addViewportState(swapChainExtent);
		gfxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int) });

		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
	}

private:
	VkAttachmentReference colorAttachmentRef;
	std::vector<VkAttachmentReference> inputAttachmentRefs;
	DescriptorSetGenerator descGen;
	GraphicsPipelineGenerator gfxPipeGen;
};


class RtxHybridSoftShadows : public WindowApplication {
public:
	RtxHybridSoftShadows(const std::vector<const char*>& _instanceExtensions, const std::vector<const char*>& _deviceExtensions) :
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

	// Output buffer for rtx pass and input for subpass 2 and filters
	VkImage rtxOutImage;
	VmaAllocation rtxOutImageAllocation;
	VkImageView rtxOutImageView;

	// Output buffer for all filters and input for subpass 2
	VkImage filterOutImage; 
	VmaAllocation filterOutImageAllocation;
	VkImageView filterOutImageView;

	Model model;
	AreaLightSources areaSources;
	CrossBilateralFilter crossBilateralFilter;
	TemporalFilter temporalFilter;
	TemporalFrequencyFilter temporalFrequencyFilter;

	std::vector<VkCommandBuffer> commandBuffers;

	FboManager fboManager1; // For subpass 1
	FboManager fboManager2; // For subpass 2

	NewGui gui;
	RandomGenerator randGen;

	PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV = nullptr;

	void init() 
	{	
		vkCmdTraceRaysNV = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysNV"));
		findDepthFormat(physicalDevice);
		fboManager1.addDepthAttachment("depth", VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &depthImageView);
		fboManager1.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColorImageView);
		fboManager1.addColorAttachment("specularColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &specularColorImageView);
		fboManager1.addColorAttachment("normal", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &normalImageView, 1, {0.0f, 0.0f, 0.0f, 0.0f});
		fboManager1.addColorAttachment("other", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &otherInfoImageView, 1, {-1.0f, 0.0f, 0.0f, -1.0f});
		fboManager1.addColorAttachment("rtxOut", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &rtxOutImageView);
		fboManager1.addColorAttachment("filterOut", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &filterOutImageView);
		
		fboManager2.addColorAttachment("filterOut", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &filterOutImageView);
		fboManager2.addColorAttachment("rtxOut", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &rtxOutImageView);
		fboManager2.addColorAttachment("swapchain", swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, swapChainImageViews.data(), static_cast<uint32_t>(swapChainImageViews.size()));

		loadScene(model, cam, "spaceship");
		areaSources.init(device, allocator, graphicsQueue, graphicsCommandPool, &model);
		subpass1.createSubpassDescription(device, fboManager1);
		subpass2.createSubpassDescription(device, fboManager2);
		createRenderPass();

		createColorResources();
		createFramebuffers();

		gui.io = &io;
		gui.cam = &cam;
		gui.cFilter = &crossBilateralFilter;
		gui.tFilter = &temporalFilter;
		gui.tfFilter = &temporalFrequencyFilter;
		gui.setStyle();
		gui.pcb.discretePdfSize = areaSources.dPdf.size();
		gui.createResources(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool, renderPass2, 0);
		randGen.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);
		model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
		model.createRtxBuffers(device, allocator, graphicsQueue, graphicsCommandPool);
		temporalFilter.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);
		temporalFrequencyFilter.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);
		
		subpass1.createSubpass(device, swapChainExtent, renderPass1, cam, model);
		rtxPass.createPipeline(device, raytracingProperties, allocator, model, fboManager1, cam, areaSources, randGen);
		crossBilateralFilter.createPipeline(physicalDevice, device, fboManager1.getImageView("diffuseColor"), fboManager1.getImageView("specularColor"),
			fboManager1.getImageView("normal"), fboManager1.getImageView("rtxOut"), fboManager1.getImageView("filterOut"));
		temporalFilter.createPipeline(physicalDevice, device, fboManager1.getImageView("rtxOut"), fboManager1.getImageView("filterOut"));
		temporalFrequencyFilter.createPipeline(physicalDevice, device, fboManager1.getImageView("rtxOut"), fboManager1.getImageView("filterOut"));
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

		vkDestroyImageView(device, rtxOutImageView, nullptr);
		vmaDestroyImage(allocator, rtxOutImage, rtxOutImageAllocation);

		vkDestroyImageView(device, filterOutImageView, nullptr);
		vmaDestroyImage(allocator, filterOutImage, filterOutImageAllocation);

		vkDestroyFramebuffer(device, renderPass1Fbo, nullptr);
		for (auto framebuffer : swapChainFramebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}

		randGen.cleanUp(allocator);
		crossBilateralFilter.cleanUp(device);
		temporalFilter.cleanUp(device, allocator);
		temporalFrequencyFilter.cleanUp(device, allocator);
		
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
		randGen.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);
		createFramebuffers();
		temporalFilter.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);
		temporalFrequencyFilter.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);

		subpass1.createSubpass(device, swapChainExtent, renderPass1, cam, model);
		rtxPass.createPipeline(device, raytracingProperties, allocator, model, fboManager1, cam, areaSources, randGen);
		crossBilateralFilter.createPipeline(physicalDevice, device, fboManager1.getImageView("diffuseColor"), fboManager1.getImageView("specularColor"),
			fboManager1.getImageView("normal"), fboManager1.getImageView("rtxOut"), fboManager1.getImageView("filterOut"));
		temporalFilter.createPipeline(physicalDevice, device, fboManager1.getImageView("rtxOut"), fboManager1.getImageView("filterOut"));
		temporalFrequencyFilter.createPipeline(physicalDevice, device, fboManager1.getImageView("rtxOut"), fboManager1.getImageView("filterOut"));
		subpass2.createSubpass(device, swapChainExtent, renderPass2, fboManager2);
		createCommandBuffers();
	}

	void cleanupFinal() 
	{	
		gui.cleanUp(device, allocator);
		model.cleanUpRtx(device, allocator);
		model.cleanUp(device, allocator);
		areaSources.cleanUp(allocator);
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
			attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			fboManager1.updateAttachmentDescription("diffuseColor", attachment);
			fboManager1.updateAttachmentDescription("specularColor", attachment);
			fboManager1.updateAttachmentDescription("normal", attachment);
			fboManager1.updateAttachmentDescription("other", attachment);

			attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			fboManager1.updateAttachmentDescription("depth", attachment);

			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			fboManager1.updateAttachmentDescription("rtxOut", attachment);
			fboManager1.updateAttachmentDescription("filterOut", attachment);

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
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
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
			attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
			attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			fboManager2.updateAttachmentDescription("rtxOut", attachment);
			fboManager2.updateAttachmentDescription("filterOut", attachment);
			
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			fboManager2.updateAttachmentDescription("swapchain", attachment);

			std::array<VkSubpassDescription, 1> subpassDesc = { subpass2.subpassDescription };

			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
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
				&swapChainExtent = swapChainExtent](VkFormat colorFormat, VkSampleCountFlagBits samples, VkImageUsageFlags usageFlags, VkImage &image, VkImageView &imageView, VmaAllocation &allocation)
		{
			createImage(device, allocator, graphicsQueue, graphicsCommandPool, image, allocation, swapChainExtent, usageFlags, colorFormat, samples);
			VkImageAspectFlagBits aspectFlagBit = (usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
				? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			imageView = createImageView(device, image, colorFormat, aspectFlagBit, 1, 1);
		};

		makeColorImage(fboManager1.getFormat("diffuseColor") , fboManager1.getSampleCount("diffuseColor"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, diffuseColorImage, diffuseColorImageView, diffuseColorImageAllocation);
		makeColorImage(fboManager1.getFormat("specularColor"), fboManager1.getSampleCount("specularColor"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, specularColorImage, specularColorImageView, specularColorImageAllocation);
		makeColorImage(fboManager1.getFormat("normal"), fboManager1.getSampleCount("normal"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, normalImage, normalImageView, normalImageAllocation);
		makeColorImage(fboManager1.getFormat("other"), fboManager1.getSampleCount("other"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, otherInfoImage, otherInfoImageView, otherInfoImageAllocation);
		makeColorImage(fboManager1.getFormat("depth"), fboManager1.getSampleCount("depth"), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthImageView, depthImageAllocation);

		makeColorImage(fboManager2.getFormat("rtxOut"), fboManager2.getSampleCount("rtxOut"), VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, rtxOutImage, rtxOutImageView, rtxOutImageAllocation);
		makeColorImage(fboManager2.getFormat("filterOut"), fboManager2.getSampleCount("filterOut"), VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, filterOutImage, filterOutImageView, filterOutImageAllocation);
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
		areaSources.cmdTransferData(commandBuffers[index]);

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
		
		//crossBilateralFilter.cmdDispatch(commandBuffers[index], swapChainExtent);
		//temporalFilter.cmdDispatch(commandBuffers[index], swapChainExtent);
		temporalFrequencyFilter.cmdDispatch(commandBuffers[index], swapChainExtent);
		
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
		vkCmdPushConstants(commandBuffers[index], subpass2.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &gui.denoise);
		vkCmdDraw(commandBuffers[index], 3, 1, 0, 0);
			
		gui.cmdDraw(commandBuffers[index]);

		vkCmdEndRenderPass(commandBuffers[index]);

		VK_CHECK_DBG_ONLY(vkEndCommandBuffer(commandBuffers[index]),
			"failed to record command buffer!");
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
		areaSources.updateData();
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
		RtxHybridSoftShadows app(instanceExtensions, deviceExtensions);

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