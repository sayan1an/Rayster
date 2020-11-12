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
#include <cstdlib>

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
#include "../filter.h"

#include "TemporalFiltering.hpp"
#include "GenerateStencil.hpp"
#include "RtxProcessing.hpp"
#include "SubSample.hpp"
#include "MarkovChain.hpp"

namespace RtxFiltering_3
{
	class RtxFiltering_3 : public WindowApplication
	{
	private:
		class Subpass1 {
		public:
			VkSubpassDescription subpassDescription;

			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorPool descriptorPool;
			VkDescriptorSet descriptorSet;

			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;

			void createSubpassDescription(const VkDevice& device, FboManager& fboMgr) {
				colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("diffuseColor", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
				colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("specularColor", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
				colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("normal", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
				colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("other", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
				colorAttachmentRefs.push_back(fboMgr.getAttachmentReference("motionVector", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
				depthAttachmentRef = fboMgr.getAttachmentReference("depth", VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

				subpassDescription = {};
				subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
				subpassDescription.pColorAttachments = colorAttachmentRefs.data();
				subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;
			}

			void createSubpass(const VkDevice& device, const VkExtent2D& fboExtent, const VkRenderPass& renderPass, const Camera& cam, const Model& model)
			{
				descGen.bindBuffer({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }, cam.getDescriptorBufferInfo());
				descGen.bindBuffer({ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT }, model.getMaterialDescriptorBufferInfo());
				descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { model.hdrTextureSampler,  model.hdrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

				descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

				auto bindingDescription = Model::getBindingDescription();
				auto attributeDescription = Model::getAttributeDescriptions();

				gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/GBuffer/gBufVert.spv");
				gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/RtxFiltering_3/gBufFrag.spv");
				gfxPipeGen.addVertexInputState(bindingDescription, attributeDescription);
				gfxPipeGen.addViewportState(fboExtent);
				gfxPipeGen.addColorBlendAttachmentState(5);
				gfxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkExtent2D) });

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
			struct PushConstantBlock
			{
				VkExtent2D viewport;
				glm::ivec2 choice;
			} pcb;
			VkSubpassDescription subpassDescription = {};

			VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
			VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

			VkSampler texSampler;

			void createSubpassDescription(const VkDevice& device, FboManager& fboMgr)
			{
				colorAttachmentRef = fboMgr.getAttachmentReference("swapchain", VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				subpassDescription = {};
				subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpassDescription.colorAttachmentCount = 1;
				subpassDescription.pColorAttachments = &colorAttachmentRef;
			}

			void createTexSampler(const VkDevice& device)
			{
				VkSamplerCreateInfo samplerInfo = {};
				samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				samplerInfo.magFilter = VK_FILTER_LINEAR;
				samplerInfo.minFilter = VK_FILTER_LINEAR;
				samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				samplerInfo.anisotropyEnable = VK_TRUE;
				samplerInfo.maxAnisotropy = 16;
				samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
				samplerInfo.unnormalizedCoordinates = VK_FALSE;
				samplerInfo.compareEnable = VK_FALSE;
				samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.minLod = 0;
				samplerInfo.maxLod = 1;
				samplerInfo.mipLodBias = 0;

				VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &texSampler),
					std::string(" RtxFiltering_3: failed to create texture sampler!"));
			}

			void createSubpass(const VkDevice& device, const VkRenderPass& renderPass, FboManager& fboMgr, const VkImageView& v1, const VkImageView& v2, const VkImageView& v3, const VkImageView& v4, const VkImageView &v5)
			{
				descGen.bindImage({ 0,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { texSampler, v1, VK_IMAGE_LAYOUT_GENERAL });
				descGen.bindImage({ 1,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { texSampler, v2, VK_IMAGE_LAYOUT_GENERAL });
				descGen.bindImage({ 2,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { texSampler, v3, VK_IMAGE_LAYOUT_GENERAL });
				descGen.bindImage({ 3,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { texSampler, v4, VK_IMAGE_LAYOUT_GENERAL });
				descGen.bindImage({ 4,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { texSampler, v5, VK_IMAGE_LAYOUT_GENERAL });
				descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

				gfxPipeGen.addVertexShaderStage(device, ROOT + "/shaders/RtxFiltering_3/gShowVert.spv");
				gfxPipeGen.addFragmentShaderStage(device, ROOT + "/shaders/RtxFiltering_3/gShowFrag.spv");
				gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
				gfxPipeGen.addDepthStencilState(VK_FALSE, VK_FALSE);
				gfxPipeGen.addViewportState(fboMgr.getSize());
				gfxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantBlock) });

				gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 0, &pipeline, &pipelineLayout);
			}

			void widget()
			{	
				if (ImGui::CollapsingHeader("Display pass")) {
					int choice = pcb.choice.x;
					ImGui::Text("Select:");
					ImGui::RadioButton("Stencil:", &choice, 0);
					ImGui::RadioButton("RTX pass:", &choice, 1);
					ImGui::RadioButton("RTX pass (no-texure):", &choice, 2);
					ImGui::RadioButton("Mc State:", &choice, 3);
					pcb.choice.x = choice;
				}
			}

			void cleanUp(const VkDevice& device)
			{
				vkDestroySampler(device, texSampler, nullptr);
			}

		private:
			VkAttachmentReference colorAttachmentRef;
			std::vector<VkAttachmentReference> inputAttachmentRefs;
			DescriptorSetGenerator descGen;
			GraphicsPipelineGenerator gfxPipeGen;
		};

		class NewGui : public Gui
		{
		public:
			const IO* io;
			Camera* cam;
			//SquarePattern* pSqPat;
			
			//TemporalFilter* tempFilt;
			StencilCompositionPass* sCmpPass;
			MarkovChainNoVisibilityCombined* mcPass;
			RtxGenCombinedPass* rGen;
			RtxCompositionPass* rtxCompPass;
			Subpass2* displayPass;
			uint32_t numSamples;
			int animate = 0;
			VkExtent2D* swapChainExtent;
		private:
			float power = 10;

			void guiSetup()
			{
				io->frameRateWidget();
				cam->cameraWidget();
				ImGui::Text("Animate:"); ImGui::SameLine();
				ImGui::RadioButton("Yes:", &animate, 1); ImGui::SameLine();
				ImGui::RadioButton("No:", &animate, 0);
				//uint32_t collectData, pixelInfo;
				//pSqPat->widget(/*collectData, pixelInfo,*/ numSamples);
				sCmpPass->widget();
				mcPass->widget(*swapChainExtent);
				rGen->widget(*swapChainExtent);
				rtxCompPass->widget();
				//tempFilt->widget();
				displayPass->widget();

			}
		};

	public:
		RtxFiltering_3(const std::vector<const char*>& _instanceExtensions, const std::vector<const char*>& _deviceExtensions, const std::vector<const char*>& _deviceFeatures) :
			WindowApplication(std::vector<const char*>(), _instanceExtensions, _deviceExtensions, _deviceFeatures) {}
	private:
		std::vector<VkFramebuffer> swapChainFramebuffers;
		VkFramebuffer renderPass1Fbo;

		VkRenderPass renderPass1;
		VkRenderPass renderPass2;

		Subpass1 subpass1;
		Subpass2 subpass2;

		VkImage diffuseColorImage;
		VmaAllocation diffuseColorImageAllocation;
		VkImageView diffuseColorImageView;

		VkImage specularColorImage;
		VmaAllocation specularColorImageAllocation;
		VkImageView specularColorImageView;

		// conatins normal + depth
		VkImage normalImage, normal16Image;
		VmaAllocation normalImageAllocation, normal16ImageAllocation;
		VkImageView normalImageView, normal16ImageView;

		// specular alpha, Int ior, Ext ior and material bsdf type
		VkImage otherInfoImage;
		VmaAllocation otherInfoImageAllocation;
		VkImageView otherInfoImageView;

		VkImage depthImage;
		VmaAllocation depthImageAllocation;
		VkImageView depthImageView;

		VkImage motionVectorImage;
		VmaAllocation motionVectorImageAllocation;
		VkImageView motionVectorImageView;

		// Output buffer for rtx pass
		VkImageView rtxPassView, rtxPassHalfView, rtxPassQuatView;
		//VkImageView filterOutImageView;

		//SquarePattern rPatSq;

		Model model;
		AreaLightSources areaSources;
		//TemporalFilter temporalFilter;
		GenerateStencilPass stencilPass;
		StencilCompositionPass stencilCompPass;
		RtxCompositionPass rtxCompPass;
		SubSamplePass subSamplePass;
		MarkovChainNoVisibilityCombined mcPass;
		RtxGenCombinedPass rtxGenPass;
		
		VkImageView stencilView, stencilView2, stencilView3;
		VkImageView normalHalf, otherHalf, normalQuat, otherQuat;
		VkImageView mcStateView, mcSampleStatView;

		std::vector<VkCommandBuffer> commandBuffers;

		FboManager fboManager1; // For subpass 1
		FboManager fboManager2; // For subpass 2

		NewGui gui;
		RandomGenerator randGen;

		void init()
		{
			findDepthFormat(physicalDevice);
			fboManager1.addDepthAttachment("depth", VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &depthImageView);
			fboManager1.addColorAttachment("diffuseColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &diffuseColorImageView);
			fboManager1.addColorAttachment("specularColor", VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, &specularColorImageView);
			fboManager1.addColorAttachment("normal", VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &normalImageView, 1, { 0.0f, 0.0f, 0.0f, -1.0f });
			fboManager1.addColorAttachment("other", VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &otherInfoImageView, 1, { 0.0f, 0.0f, 0.0f, -1.0f });
			fboManager1.addColorAttachment("motionVector", VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, &motionVectorImageView, 1, { 0.0f, 0.0f, 0.0f, -1.0f });

			fboManager2.addColorAttachment("swapchain", swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, swapChainImageViews.data(), static_cast<uint32_t>(swapChainImageViews.size()));

			loadScene(model, cam, "spaceship");
			areaSources.init(device, allocator, graphicsQueue, graphicsCommandPool, &model);
			subpass1.createSubpassDescription(device, fboManager1);
			subpass2.createSubpassDescription(device, fboManager2);
			subpass2.createTexSampler(device);
			createRenderPass();

			createColorResources();
			createFramebuffers();

			gui.io = &io;
			gui.cam = &cam;
			//gui.pSqPat = &rPatSq;
			gui.sCmpPass = &stencilCompPass;
			gui.swapChainExtent = &swapChainExtent;
			gui.mcPass = &mcPass;
			gui.rGen = &rtxGenPass;
			gui.rtxCompPass = &rtxCompPass;
			//gui.tempFilt = &temporalFilter;
			gui.displayPass = &subpass2;
			gui.setStyle();
			
			gui.createResources(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool, renderPass2, 0);
			//rPatSq.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool);
			randGen.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize());
			model.createBuffers(physicalDevice, device, allocator, graphicsQueue, graphicsCommandPool);
			model.createRtxBuffers(device, allocator, graphicsQueue, graphicsCommandPool);
			//temporalFilter.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), filterOutImageView);
			stencilPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), stencilView, stencilView2, stencilView3);
			subSamplePass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), fboManager1.getFormat("normal"), fboManager1.getFormat("other"), normalHalf, otherHalf, normalQuat, otherQuat);
			mcPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), mcStateView, mcSampleStatView);
			rtxGenPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), rtxPassView, rtxPassHalfView, rtxPassQuatView);
			rtxCompPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize());
			
			subpass1.createSubpass(device, fboManager1.getSize(), renderPass1, cam, model);
			rtxGenPass.createPipelines(device, raytracingProperties, allocator, model, cam, areaSources, randGen, mcSampleStatView, 
				fboManager1.getImageView("normal"), fboManager1.getImageView("other"), stencilView,
				normalHalf, otherHalf, stencilView2,
				normalQuat, otherQuat, stencilView3);
			//temporalFilter.createPipeline(physicalDevice, device, rtxPassView);
			stencilPass.createPipeline(physicalDevice, device, fboManager1.getImageView("normal"), rtxPassView);
			stencilCompPass.createPipeline(physicalDevice, device, stencilView, stencilView2, stencilView3);
			subSamplePass.createPipeline(physicalDevice, device, fboManager1.getImageView("normal"), fboManager1.getImageView("other"));
			mcPass.createPipelines(physicalDevice, device, cam, areaSources, randGen,
				fboManager1.getImageView("normal"), fboManager1.getImageView("other"), stencilView,
				normalHalf, otherHalf, stencilView2,
				normalQuat, otherQuat, stencilView3);
			subpass2.createSubpass(device, renderPass2, fboManager2, rtxPassView, stencilView, stencilView2, stencilView3, mcStateView);
			rtxCompPass.createPipeline(physicalDevice, device, fboManager1.getImageView("diffuseColor"), fboManager1.getImageView("specularColor"), rtxPassView, rtxPassHalfView, rtxPassQuatView, mcStateView);

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

			vkDestroyImageView(device, normal16ImageView, nullptr);
			vmaDestroyImage(allocator, normal16Image, normal16ImageAllocation);

			vkDestroyImageView(device, otherInfoImageView, nullptr);
			vmaDestroyImage(allocator, otherInfoImage, otherInfoImageAllocation);

			vkDestroyImageView(device, motionVectorImageView, nullptr);
			vmaDestroyImage(allocator, motionVectorImage, motionVectorImageAllocation);

			vkDestroyFramebuffer(device, renderPass1Fbo, nullptr);
			for (auto framebuffer : swapChainFramebuffers) {
				vkDestroyFramebuffer(device, framebuffer, nullptr);
			}

			randGen.cleanUp(allocator);
			//temporalFilter.cleanUp(device, allocator);
			stencilPass.cleanUp(device, allocator);
			stencilCompPass.cleanUp(device, allocator);
			subSamplePass.cleanUp(device, allocator);
			mcPass.cleanUp(device, allocator);
			
			vkFreeCommandBuffers(device, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

			vkDestroyPipeline(device, subpass1.pipeline, nullptr);
			vkDestroyPipelineLayout(device, subpass1.pipelineLayout, nullptr);

			// rtx pass cleanup
			rtxGenPass.cleanUp(device, allocator);
			
			rtxCompPass.cleanUp(device, allocator);

			vkDestroyPipeline(device, subpass2.pipeline, nullptr);
			vkDestroyPipelineLayout(device, subpass2.pipelineLayout, nullptr);

			vkDestroyRenderPass(device, renderPass1, nullptr);
			vkDestroyRenderPass(device, renderPass2, nullptr);

			vkDestroyDescriptorSetLayout(device, subpass1.descriptorSetLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, subpass2.descriptorSetLayout, nullptr);

			vkDestroyDescriptorPool(device, subpass1.descriptorPool, nullptr);
			vkDestroyDescriptorPool(device, subpass2.descriptorPool, nullptr);
		}

		void recreateAfterSwapChainResize()
		{
			createRenderPass();

			createColorResources();
			randGen.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, swapChainExtent);
			createFramebuffers();
			//temporalFilter.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), filterOutImageView);
			stencilPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), stencilView, stencilView2, stencilView3);
			subSamplePass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), fboManager1.getFormat("normal"), fboManager1.getFormat("other"), normalHalf, otherHalf, normalQuat, otherQuat);
			mcPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), mcStateView, mcSampleStatView);
			rtxGenPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize(), rtxPassView, rtxPassHalfView, rtxPassQuatView);
			rtxCompPass.createBuffers(device, allocator, graphicsQueue, graphicsCommandPool, fboManager1.getSize());
		
			subpass1.createSubpass(device, fboManager1.getSize(), renderPass1, cam, model);
			rtxGenPass.createPipelines(device, raytracingProperties, allocator, model, cam, areaSources, randGen, mcSampleStatView,
				fboManager1.getImageView("normal"), fboManager1.getImageView("other"), stencilView,
				normalHalf, otherHalf, stencilView2,
				normalQuat, otherQuat, stencilView3);
			//temporalFilter.createPipeline(physicalDevice, device, rtxPassView);
			stencilPass.createPipeline(physicalDevice, device, fboManager1.getImageView("normal"), rtxPassView);
			stencilCompPass.createPipeline(physicalDevice, device, stencilView, stencilView2, stencilView3);
			subSamplePass.createPipeline(physicalDevice, device, fboManager1.getImageView("normal"), fboManager1.getImageView("other"));
			mcPass.createPipelines(physicalDevice, device, cam, areaSources, randGen,
				fboManager1.getImageView("normal"), fboManager1.getImageView("other"), stencilView,
				normalHalf, otherHalf, stencilView2,
				normalQuat, otherQuat, stencilView3);
			subpass2.createSubpass(device, renderPass2, fboManager2, rtxPassView, stencilView, stencilView2, stencilView3, mcStateView);
			rtxCompPass.createPipeline(physicalDevice, device, fboManager1.getImageView("diffuseColor"), fboManager1.getImageView("specularColor"), rtxPassView, rtxPassHalfView, rtxPassQuatView, mcStateView);

			createCommandBuffers();
		}

		void cleanupFinal()
		{
			gui.cleanUp(device, allocator);
			model.cleanUpRtx(device, allocator);
			model.cleanUp(device, allocator);
			areaSources.cleanUp(device, allocator);
			//rPatSq.cleanUp(allocator);
			subpass2.cleanUp(device);
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
				fboManager1.updateAttachmentDescription("motionVector", attachment);

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
				//fboManager2.updateAttachmentDescription("rtxOut", attachment);
				//fboManager2.updateAttachmentDescription("filterOut", attachment);

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
				framebufferInfo.width = fboManager1.getSize().width;
				framebufferInfo.height = fboManager1.getSize().height;
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
				framebufferInfo.width = fboManager2.getSize().width;
				framebufferInfo.height = fboManager2.getSize().height;
				framebufferInfo.layers = 1;

				VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]),
					"failed to create renderpass2 framebuffer!");
			}
		}

		void createColorResources()
		{
			auto makeColorImage = [&device = device, &graphicsQueue = graphicsQueue,
				&graphicsCommandPool = graphicsCommandPool, &allocator = allocator](VkExtent2D extent, VkFormat colorFormat, VkSampleCountFlagBits samples, VkImageUsageFlags usageFlags, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, graphicsQueue, graphicsCommandPool, image, allocation, extent, usageFlags, colorFormat, samples);
				VkImageAspectFlagBits aspectFlagBit = (usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
					? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
				imageView = createImageView(device, image, colorFormat, aspectFlagBit, 1, 1);
			};

			fboManager1.setSize({ 1280 , 720 });
			makeColorImage(fboManager1.getSize(), fboManager1.getFormat("diffuseColor"), fboManager1.getSampleCount("diffuseColor"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, diffuseColorImage, diffuseColorImageView, diffuseColorImageAllocation);
			makeColorImage(fboManager1.getSize(), fboManager1.getFormat("specularColor"), fboManager1.getSampleCount("specularColor"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, specularColorImage, specularColorImageView, specularColorImageAllocation);
			makeColorImage(fboManager1.getSize(), fboManager1.getFormat("normal"), fboManager1.getSampleCount("normal"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, normalImage, normalImageView, normalImageAllocation);
			makeColorImage(fboManager1.getSize(), fboManager1.getFormat("other"), fboManager1.getSampleCount("other"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, otherInfoImage, otherInfoImageView, otherInfoImageAllocation);
			makeColorImage(fboManager1.getSize(), fboManager1.getFormat("depth"), fboManager1.getSampleCount("depth"), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthImageView, depthImageAllocation);
			makeColorImage(fboManager1.getSize(), fboManager1.getFormat("motionVector"), fboManager1.getSampleCount("motionVector"), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, motionVectorImage, motionVectorImageView, motionVectorImageAllocation);

			fboManager2.setSize(swapChainExtent);
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
			renderPassInfo.renderArea.extent = fboManager1.getSize();

			std::vector<VkClearValue> clearValues = fboManager1.getClearValues();
			renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(commandBuffers[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipeline);
			vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass1.pipelineLayout, 0, 1, &subpass1.descriptorSet, 0, nullptr);
			vkCmdPushConstants(commandBuffers[index], subpass1.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkExtent2D), &renderPassInfo.renderArea.extent);
			// put model draw
			model.cmdDraw(commandBuffers[index]);
			vkCmdEndRenderPass(commandBuffers[index]);

			stencilPass.cmdDispatch(commandBuffers[index]);
			stencilCompPass.cmdDispatch(commandBuffers[index], fboManager1.getSize());
			subSamplePass.cmdDispatch(commandBuffers[index]);

			mcPass.cmdDispatch(commandBuffers[index]);

			rtxGenPass.cmdDispatch(commandBuffers[index]);
			vkCmdPipelineBarrier(commandBuffers[index], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT,
				0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
			rtxCompPass.cmdDispatch(commandBuffers[index]);
			
			//temporalFilter.cmdDispatch(commandBuffers[index]);
			
			// begin second render-pass
			renderPassInfo.renderPass = renderPass2;
			renderPassInfo.framebuffer = swapChainFramebuffers[index];
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = fboManager2.getSize();
			renderPassInfo.clearValueCount = 0;
			renderPassInfo.pClearValues = VK_NULL_HANDLE;

			vkCmdBeginRenderPass(commandBuffers[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipeline);
			vkCmdBindDescriptorSets(commandBuffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, subpass2.pipelineLayout, 0, 1, &subpass2.descriptorSet, 0, nullptr);
			subpass2.pcb.viewport = swapChainExtent;
			vkCmdPushConstants(commandBuffers[index], subpass2.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(subpass2.pcb), &subpass2.pcb);
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
			model.updateMeshData(gui.animate);
			model.updateTlasData();
			areaSources.updateData();
			cam.updateProjViewMat(io, fboManager1.getSize().width, fboManager1.getSize().height);
			//rPatSq.updateDataPre(swapChainExtent);

			buildCommandBuffer(imageIndex);
			submitRenderCmd(commandBuffers[imageIndex]);
			frameEnd(imageIndex);

			mcPass.updateDataPost();
			rtxGenPass.updateDataPost();
			//rPatSq.updateDataPost();
			//temporalFilter.saveFramePass.toDisk("D:/results/");
		}
	};
}