#include "../../generator.h"
#include "../../helper.h"
#include "../../model.hpp"
#include "../../lightSources.h"
#include "../../camera.hpp"
#include "../../random.h"
#include "../../../shaders/RtxFiltering_2/hostDeviceShared.h"

#include "TemporalFiltering.hpp"

namespace RtxFiltering_2
{	
	class SquarePattern;

	class RtxGenPass {
	public:
		void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, const uint32_t level, VkImageView& rtxOutView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation, uint32_t layers)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format, VK_SAMPLE_COUNT_1_BIT, layers);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, layers);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, layers);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, rtxOutImage, rtxOutImageView, rtxOutImageAllocation, 2);
			createTexSampler(device);

			rtxOutView = rtxOutImageView;
			globalWorkDim = extent;

			vkCmdTraceRaysNV = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysNV"));
			pcb.level = level;
			pcb.shadowRayCutoff = 0.45f;
			pcb.shadowRayCutoffProb = 0.05f;
		}

		void createPipeline(const VkDevice& device, const VkPhysicalDeviceRayTracingPropertiesNV& raytracingProperties, const VmaAllocator& allocator,
			const Model& model, const Camera& cam, const AreaLightSources& areaSource, const SquarePattern& coherentSamples, const RandomGenerator& randGen, const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& stencil, const VkImageView& shadowMap)
		{
			descGen.bindTLAS({ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, model.getDescriptorTlas());
			descGen.bindTLAS({ 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, areaSource.getDescriptorTlas());
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, inNormal, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, inOther, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, stencil, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { texSampler, shadowMap, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, rtxOutImageView, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, cam.getDescriptorBufferInfo());
			descGen.bindBuffer({ 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, areaSource.getVerticesDescriptorBufferInfo());
			descGen.bindBuffer({ 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, areaSource.dPdf.getCdfNormDescriptorBufferInfo());
			descGen.bindBuffer({ 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_RAYGEN_BIT_NV }, coherentSamples.getSquareSamplesDescriptorBufferInfo());
			descGen.bindBuffer({ 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_RAYGEN_BIT_NV }, randGen.getDescriptorBufferInfo());
			descGen.bindBuffer({ 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getStaticInstanceDescriptorBufferInfo());
			descGen.bindBuffer({ 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getMaterialDescriptorBufferInfo());
			descGen.bindBuffer({ 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getVertexDescriptorBufferInfo());
			descGen.bindBuffer({ 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getIndexDescriptorBufferInfo());
			descGen.bindImage({ 16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			descGen.bindBuffer({ 17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, areaSource.getLightInstanceDescriptorBufferInfo());

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			uint32_t rayGenId = rtxPipeGen.addRayGenShaderStage(device, ROOT + "/shaders/RtxFiltering_2/raygen.spv");
			uint32_t missShaderId0 = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxFiltering_2/0_miss.spv");
			uint32_t hitGroupId0 = rtxPipeGen.startHitGroup();
			rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxFiltering_2/0_close.spv"); // Partial raytrace
			rtxPipeGen.endHitGroup();
			uint32_t hitGroupId1 = rtxPipeGen.startHitGroup();
			rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxFiltering_2/1_close.spv"); // // Full raytrace
			rtxPipeGen.endHitGroup();
			rtxPipeGen.setMaxRecursionDepth(1);
			rtxPipeGen.addPushConstantRange({ VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(PushConstantBlock) });
			rtxPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

			sbtGen.addRayGenerationProgram(rayGenId, {});
			sbtGen.addMissProgram(missShaderId0, {});
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

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, rtxOutImageView, nullptr);
			vmaDestroyImage(allocator, rtxOutImage, rtxOutImageAllocation);
			vkDestroySampler(device, texSampler, nullptr);

			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vmaDestroyBuffer(allocator, sbtBuffer, sbtBufferAllocation);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf, const uint32_t dPdfSize, const uint32_t numSamples, float shadowRayCutoff, float shadowRayCutoffProb)
		{
			pcb.discretePdfSize = dPdfSize;
			pcb.numSamples = numSamples;
			pcb.shadowRayCutoff = shadowRayCutoff;
			pcb.shadowRayCutoffProb = shadowRayCutoffProb;
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(PushConstantBlock), &pcb);

			VkDeviceSize rayGenOffset = sbtGen.getRayGenOffset();
			VkDeviceSize missOffset = sbtGen.getMissOffset();
			VkDeviceSize missStride = sbtGen.getMissEntrySize();
			VkDeviceSize hitGroupOffset = sbtGen.getHitGroupOffset();
			VkDeviceSize hitGroupStride = sbtGen.getHitGroupEntrySize();

			vkCmdTraceRaysNV(cmdBuf, sbtBuffer, rayGenOffset, sbtBuffer, missOffset, missStride, sbtBuffer, hitGroupOffset, hitGroupStride,
				VK_NULL_HANDLE, 0, 0, globalWorkDim.width, globalWorkDim.height, 1);
		}
	private:
		DescriptorSetGenerator descGen;
		RayTracingPipelineGenerator rtxPipeGen;

		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;
		VkBuffer sbtBuffer;
		VmaAllocation sbtBufferAllocation;
		ShaderBindingTableGenerator sbtGen;

		struct PushConstantBlock
		{
			float power = 100.0;
			uint32_t discretePdfSize;
			uint32_t numSamples;
			uint32_t level;
			float shadowRayCutoff;
			float shadowRayCutoffProb;
		} pcb;

		VkImage rtxOutImage;
		VkImageView rtxOutImageView;
		VmaAllocation rtxOutImageAllocation;

		VkExtent2D globalWorkDim;

		PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV = nullptr;

		VkSampler texSampler;

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
				std::string(" RtxFiltering_2: failed to create texture sampler!"));
		}
	};

	class RtxCompositionPass
	{
	public:
		void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& rtxComposedView, VkImageView& smView, VkImageView& smBlurView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, rtxComposedImage, rtxComposedImageView, rtxComposedImageAllocation);
			makeImage(extent, VK_FORMAT_R32_SFLOAT, shadowMap, shadowMapView, shadowMapAllocation);
			makeImage({ extent.width / SHADOW_MAP_SUBSAMPLE, extent.height / SHADOW_MAP_SUBSAMPLE }, VK_FORMAT_R32_SFLOAT, shadowMapBlur, shadowMapBlurView, shadowMapBlurAllocation);

			createTexSampler(device);
			rtxComposedView = rtxComposedImageView;
			smView = shadowMapView;
			smBlurView = shadowMapBlurView;
			globalWorkDim = extent;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& diffTex, const VkImageView& specTex, const VkImageView& rtxView, const VkImageView& rtxView2, const VkImageView& rtxView3, const VkDescriptorBufferInfo& mcSampleInfo)
		{
			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , diffTex,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , specTex,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , rtxView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { texSampler , rtxView2,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { texSampler , rtxView3,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, mcSampleInfo);
			descGen.bindImage({ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , rtxComposedImageView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , shadowMapView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , shadowMapBlurView,  VK_IMAGE_LAYOUT_GENERAL });

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			//filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/rtxCompositionPass.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdDispatch(cmdBuf, 1 + (globalWorkDim.width - 1) / SHADOW_MAP_SUBSAMPLE, 1 + (globalWorkDim.height - 1) / SHADOW_MAP_SUBSAMPLE, 1);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, rtxComposedImageView, nullptr);
			vmaDestroyImage(allocator, rtxComposedImage, rtxComposedImageAllocation);
			vkDestroyImageView(device, shadowMapView, nullptr);
			vmaDestroyImage(allocator, shadowMap, shadowMapAllocation);
			vkDestroyImageView(device, shadowMapBlurView, nullptr);
			vmaDestroyImage(allocator, shadowMapBlur, shadowMapBlurAllocation);
			vkDestroySampler(device, texSampler, nullptr);
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}

		void widget()
		{	


		}

	private:
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;

		ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("RtxCompositionPass");

		DescriptorSetGenerator descGen = DescriptorSetGenerator("RtxCompositionPass");
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		VkImage rtxComposedImage;
		VkImageView rtxComposedImageView;
		VmaAllocation rtxComposedImageAllocation;

		VkImage shadowMap;
		VkImageView shadowMapView;
		VmaAllocation shadowMapAllocation;

		VkImage shadowMapBlur;
		VkImageView shadowMapBlurView;
		VmaAllocation shadowMapBlurAllocation;

		VkExtent2D globalWorkDim;
		VkSampler texSampler;

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
				std::string(" RtxFiltering_2: failed to create texture sampler!"));
		}
	};
}