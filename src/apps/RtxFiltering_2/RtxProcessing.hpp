#include "../../generator.h"
#include "../../helper.h"
#include "../../model.hpp"
#include "../../lightSources.h"
#include "../../camera.hpp"
#include "../../random.h"
#include "../../../shaders/RtxFiltering_2/hostDeviceShared.h"
#include "cnpy.h"

namespace RtxFiltering_2
{	
	class RtxGenPass 
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const uint32_t level, const VkExtent2D& extent, VkImageView& rtxOutView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation, uint32_t layers)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format, VK_SAMPLE_COUNT_1_BIT, layers);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, layers);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, layers);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, rtxOutImage, rtxOutImageView, rtxOutImageAllocation, 2);
			
			rtxOutView = rtxOutImageView;
			globalWorkDim = extent;

			vkCmdTraceRaysNV = reinterpret_cast<PFN_vkCmdTraceRaysNV>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysNV"));
			pcb.level = level;
		}

		void createPipeline(const VkDevice& device, const VkPhysicalDeviceRayTracingPropertiesNV& raytracingProperties, const VmaAllocator& allocator,
			const Model& model, const Camera& cam, const AreaLightSources& areaSource, const RandomGenerator& randGen, const VkImageView& sampleStatView, const VkDescriptorBufferInfo& ghWeights,
			const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& stencil)
		{
			descGen.bindTLAS({ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, model.getDescriptorTlas());
			descGen.bindTLAS({ 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, areaSource.getDescriptorTlas());
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, inNormal, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, inOther, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, stencil, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, sampleStatView, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, ghWeights);
			descGen.bindImage({ 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, { VK_NULL_HANDLE, rtxOutImageView, VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, cam.getDescriptorBufferInfo());
			descGen.bindBuffer({ 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, areaSource.getVerticesDescriptorBufferInfo());
			descGen.bindBuffer({ 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, areaSource.dPdf.getCdfNormDescriptorBufferInfo());
			descGen.bindBuffer({ 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_RAYGEN_BIT_NV }, areaSource.dPdf.getEmitterIndexMapDescriptorBufferInfo());
			descGen.bindBuffer({ 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_RAYGEN_BIT_NV }, randGen.getDescriptorBufferInfo());
			descGen.bindBuffer({ 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getStaticInstanceDescriptorBufferInfo());
			descGen.bindBuffer({ 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getMaterialDescriptorBufferInfo());
			descGen.bindBuffer({ 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getVertexDescriptorBufferInfo());
			descGen.bindBuffer({ 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, model.getIndexDescriptorBufferInfo());
			descGen.bindImage({ 17, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, { model.ldrTextureSampler,  model.ldrTextureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			descGen.bindBuffer({ 18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV }, areaSource.getLightInstanceDescriptorBufferInfo());

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

			pcb.discretePdfSize = areaSource.dPdf.size().y;
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, rtxOutImageView, nullptr);
			vmaDestroyImage(allocator, rtxOutImage, rtxOutImageAllocation);
			
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vmaDestroyBuffer(allocator, sbtBuffer, sbtBufferAllocation);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf, const uint32_t numSamples)
		{
			pcb.numSamples = numSamples;
		
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
			uint32_t discretePdfSize;
			uint32_t numSamples;
			uint32_t level;
		} pcb;

		VkImage rtxOutImage;
		VkImageView rtxOutImageView;
		VmaAllocation rtxOutImageAllocation;

		VkExtent2D globalWorkDim;

		PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV = nullptr;
	};

	class RtxGenCombinedPass
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& rtxView1, VkImageView& rtxView2, VkImageView& rtxView3)
		{	
			loadGhWeights((1 << GH_ORDER_BITS));
			createBuffer(device, allocator, queue, commandPool, ghBuffer, ghBufferAllocation, gaussHermitWeights.size() * sizeof(gaussHermitWeights[0]), gaussHermitWeights.data(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

			pass1.createBuffers(device, allocator, queue, commandPool, 0, extent, rtxView1);
			pass2.createBuffers(device, allocator, queue, commandPool, 1, { extent.width / 2, extent.height / 2 }, rtxView2);
			pass3.createBuffers(device, allocator, queue, commandPool, 2, { extent.width / 4, extent.height / 4 }, rtxView3);

			buffersUpdated = true;
		}

		void createPipelines(const VkDevice& device, const VkPhysicalDeviceRayTracingPropertiesNV& raytracingProperties, const VmaAllocator& allocator,
			const Model& model, const Camera& cam, const AreaLightSources& areaSource, const RandomGenerator& randGen, const VkImageView& sampleStatView,
			const VkImageView& inNormal1, const VkImageView& inOther1, const VkImageView& inStencil1,
			const VkImageView& inNormal2, const VkImageView& inOther2, const VkImageView& inStencil2,
			const VkImageView& inNormal3, const VkImageView& inOther3, const VkImageView& inStencil3)
		{	
			CHECK_DBG_ONLY(buffersUpdated, "RtxGenCombinedPass : call createBuffers first.");

			pass1.createPipeline(device, raytracingProperties, allocator, model, cam, areaSource, randGen, sampleStatView, getGhDescriptorBufferInfo(), inNormal1, inOther1, inStencil1);
			pass2.createPipeline(device, raytracingProperties, allocator, model, cam, areaSource, randGen, sampleStatView, getGhDescriptorBufferInfo(), inNormal2, inOther2, inStencil2);
			pass3.createPipeline(device, raytracingProperties, allocator, model, cam, areaSource, randGen, sampleStatView, getGhDescriptorBufferInfo(), inNormal3, inOther3, inStencil3);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{	
			uint32_t sCount = static_cast<uint32_t>(sampleCount);
			pass1.cmdDispatch(cmdBuf, sCount);
			pass2.cmdDispatch(cmdBuf, sCount);
			pass3.cmdDispatch(cmdBuf, sCount);
		}

		void widget()
		{
			if (ImGui::CollapsingHeader("RtxGenPass")) {
				ImGui::SliderInt("Mc Samples##RtxGenCombinedPass", &sampleCount, 1, 256);
			}

		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			pass3.cleanUp(device, allocator);
			pass2.cleanUp(device, allocator);
			pass1.cleanUp(device, allocator);

			vmaDestroyBuffer(allocator, ghBuffer, ghBufferAllocation);

			buffersUpdated = false;
		}
	private:
		bool buffersUpdated = false;

		int sampleCount = 4;

		RtxGenPass pass1;
		RtxGenPass pass2;
		RtxGenPass pass3;

		std::vector<glm::vec2> gaussHermitWeights;
		VkBuffer ghBuffer;
		VmaAllocation ghBufferAllocation;

		VkDescriptorBufferInfo getGhDescriptorBufferInfo() const
		{
			CHECK(buffersUpdated, "Uniform buffer for Gauss-Hermit weights un-initialized");

			VkDescriptorBufferInfo info = { ghBuffer, 0, VK_WHOLE_SIZE };
			return info;
		}

		void loadGhWeights(uint32_t maxOrder)
		{	
			CHECK(maxOrder <= 100, "GH Order cannot be greater than 100.");
			cnpy::NpyArray ghWeights = cnpy::npy_load(ROOT + "/models/ghWeights100.npy");
			CHECK(ghWeights.word_size == 8, "Save the numpy array as double.");
			CHECK(ghWeights.shape[1] == 2, "GH Weights are invalid.");
			CHECK(ghWeights.shape[0] == 5050, "GH Weights are invalid.");

			double* data = ghWeights.data<double>();
			gaussHermitWeights.resize(maxOrder * (maxOrder + 1) / 2);

			for (size_t i = 0; i < gaussHermitWeights.size(); i++) {
				float x = static_cast<float>(data[2 * i]);
				float w = static_cast<float>(data[2 * i + 1]);
				std::cout << x << " " << w << std::endl;
				gaussHermitWeights[i] = glm::vec2(x, w);
			}
		}
	};

	class RtxCompositionPass
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& rtxComposedView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, rtxComposedImage, rtxComposedImageView, rtxComposedImageAllocation);
			createTexSampler(device);
			rtxComposedView = rtxComposedImageView;
			
			pcb.choice = 1;
			pcb.brightness = 1.0f;
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
			
			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/rtxCompositionPass.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
			vkCmdDispatch(cmdBuf, 1 + (globalWorkDim.width - 1) / 16, 1 + (globalWorkDim.height - 1) / 16, 1);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, rtxComposedImageView, nullptr);
			vmaDestroyImage(allocator, rtxComposedImage, rtxComposedImageAllocation);
			vkDestroySampler(device, texSampler, nullptr);
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}

		void widget()
		{	
			if (ImGui::CollapsingHeader("RtxCompositionPass")) {
				ImGui::Text("Output:");
				ImGui::RadioButton("Rtx##RtxCompositionPass", &pcb.choice, 0); ImGui::SameLine();
				ImGui::RadioButton("Mcmc##RtxCompositionPass", &pcb.choice, 1);
				ImGui::SliderFloat("Brightness##MarkovChainPass", &pcb.brightness, 1.0f, 200.0f);
			}

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

		struct PushConstantBlock
		{
			int choice;
			float brightness;
		} pcb;
	};
}