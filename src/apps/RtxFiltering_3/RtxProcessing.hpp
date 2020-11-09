#include "../../generator.h"
#include "../../helper.h"
#include "../../model.hpp"
#include "../../lightSources.h"
#include "../../camera.hpp"
#include "../../random.h"
#include "../../../shaders/RtxFiltering_3/hostDeviceShared.h"
#include "cnpy.h"

namespace RtxFiltering_3
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
			const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& stencil,
			const VkDescriptorBufferInfo& collectSamples)
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

#if COLLECT_RT_SAMPLES
			descGen.bindBuffer({ 19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV }, collectSamples);
#endif

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			uint32_t rayGenId = rtxPipeGen.addRayGenShaderStage(device, ROOT + "/shaders/RtxFiltering_3/raygen.spv");
			uint32_t missShaderId0 = rtxPipeGen.addMissShaderStage(device, ROOT + "/shaders/RtxFiltering_3/0_miss.spv");
			uint32_t hitGroupId0 = rtxPipeGen.startHitGroup();
			rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxFiltering_3/0_close.spv"); // Partial raytrace
			rtxPipeGen.endHitGroup();
			uint32_t hitGroupId1 = rtxPipeGen.startHitGroup();
			rtxPipeGen.addCloseHitShaderStage(device, ROOT + "/shaders/RtxFiltering_3/1_close.spv"); // // Full raytrace
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

		void cmdDispatch(const VkCommandBuffer& cmdBuf, const uint32_t numSamples, const uint32_t random, const glm::uvec2& pixelQuery)
		{
			pcb.numSamples = numSamples;
			pcb.random = random;

#if COLLECT_RT_SAMPLES
			pcb.pixelQueryX = pixelQuery.x;
			pcb.pixelQueryY = pixelQuery.y;
#endif
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
			uint32_t random;
#if COLLECT_RT_SAMPLES
			uint32_t pixelQueryX;
			uint32_t pixelQueryY;
#endif
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

			mptrCollectRtSampleBuffer = createBuffer(allocator, collectRtSampleBuffer, collectRtSampleBufferAllocation, MAX_RT_SAMPLES * sizeof(float) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
			ptrCollectRtSampleBuffer = new float[MAX_RT_SAMPLES * 4];

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

			pass1.createPipeline(device, raytracingProperties, allocator, model, cam, areaSource, randGen, sampleStatView, getGhDescriptorBufferInfo(), inNormal1, inOther1, inStencil1, getCollectSamplesDescriptorBufferInfo());
			pass2.createPipeline(device, raytracingProperties, allocator, model, cam, areaSource, randGen, sampleStatView, getGhDescriptorBufferInfo(), inNormal2, inOther2, inStencil2, getCollectSamplesDescriptorBufferInfo());
			pass3.createPipeline(device, raytracingProperties, allocator, model, cam, areaSource, randGen, sampleStatView, getGhDescriptorBufferInfo(), inNormal3, inOther3, inStencil3, getCollectSamplesDescriptorBufferInfo());
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{	
			uint32_t sCount = static_cast<uint32_t>(sampleCount);
			uint32_t random = static_cast<uint32_t>(isRandom);

			pass1.cmdDispatch(cmdBuf, sCount, isRandom, pixelQuery);
			pass2.cmdDispatch(cmdBuf, sCount, isRandom, pixelQuery / glm::uvec2(2, 2));
			pass3.cmdDispatch(cmdBuf, sCount, isRandom, pixelQuery / glm::uvec2(4, 4));
		}

		void widget(const VkExtent2D& swapChainExtent)
		{
			if (ImGui::CollapsingHeader("RtxGenPass")) {
				ImGui::Text("Sample:");
				ImGui::RadioButton("McMc##RtxGenCombinedPass", &isRandom, 0); ImGui::SameLine();
				ImGui::RadioButton("Random##RtxGenCombinedPass", &isRandom, 1);

				ImGui::SliderInt("Mc Samples##RtxGenCombinedPass", &sampleCount, 1, 256);
#if COLLECT_RT_SAMPLES
				int xQuery = static_cast<int>(pixelQuery.x);
				int yQuery = static_cast<int>(pixelQuery.y);
				ImGui::SliderInt("X##Combined_RT_MC_Pass", &xQuery, 0, swapChainExtent.width - 1);
				ImGui::SliderInt("Y##Combined_RT_MC_Pass", &yQuery, 0, swapChainExtent.height - 1);
				pixelQuery.x = static_cast<uint32_t>(xQuery);
				pixelQuery.y = static_cast<uint32_t>(yQuery);


				meanVar[0] = ImVec2(ptrCollectRtSampleBuffer[2], ptrCollectRtSampleBuffer[3]);
				meanVar[1] = ImVec2(meanVar[0]); meanVar[1].x -= ptrCollectRtSampleBuffer[4];
				meanVar[2] = ImVec2(meanVar[0]); meanVar[2].x += ptrCollectRtSampleBuffer[4];
				meanVar[3] = ImVec2(meanVar[0]); meanVar[3].y -= ptrCollectRtSampleBuffer[5];
				meanVar[4] = ImVec2(meanVar[0]); meanVar[4].y += ptrCollectRtSampleBuffer[5];

				ImPlot::SetNextPlotLimits(0, 1.0f, 0, 1.0f, ImGuiCond_Always);
				if (ImPlot::BeginPlot("Gauss-hermit sample plot##RtxGenCombinedPass", "u", "v")) {
					ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 0);
					ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 4);

					ImPlot::PlotScatter("Mean##RtxGenCombinedPass", &meanVar[0].x, &meanVar[0].y, 1, 0, sizeof(ImVec2));
					ImPlot::PlotScatter("Std-X##RtxGenCombinedPass", &meanVar[1].x, &meanVar[1].y, 2, 0, sizeof(ImVec2));
					ImPlot::PlotScatter("Std-Y##RtxGenCombinedPass", &meanVar[3].x, &meanVar[3].y, 2, 0, sizeof(ImVec2));

					auto getter = [](void* data, int idx) {
						glm::vec4 d = static_cast<const glm::vec4*>(data)[idx + RT_SAMPLE_HEADER_SIZE];
						//std::this_thread::sleep_for(std::chrono::milliseconds(100));
						return ImPlotPoint(d.x, d.y);
					};
					ImPlot::PlotScatterG("Samples##RtxGenCombinedPass", static_cast<ImPlotPoint(*)(void*, int)>(getter), static_cast<void*>(ptrCollectRtSampleBuffer), static_cast<int>(ptrCollectRtSampleBuffer[1]), 0);

					ImPlot::PopStyleVar(2);
					ImPlot::EndPlot();
				}
#endif
			}

		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			pass3.cleanUp(device, allocator);
			pass2.cleanUp(device, allocator);
			pass1.cleanUp(device, allocator);

			vmaDestroyBuffer(allocator, collectRtSampleBuffer, collectRtSampleBufferAllocation);
			delete[]ptrCollectRtSampleBuffer;

			vmaDestroyBuffer(allocator, ghBuffer, ghBufferAllocation);

			buffersUpdated = false;
		}

		void updateDataPost()
		{
#if COLLECT_RT_SAMPLES
			memcpy(ptrCollectRtSampleBuffer, mptrCollectRtSampleBuffer, MAX_RT_SAMPLES * sizeof(float) * 4);
			/*const glm::vec4* ptr = static_cast<const glm::vec4*>((void*)ptrCollectRtSampleBuffer);
			for (uint32_t i = 0; i < (uint32_t)ptrCollectRtSampleBuffer[1]; i++) {
				glm::vec4 d = ptr[i + RT_SAMPLE_HEADER_SIZE];
				std::cout << d.x << " " << d.y << std::endl;
			}*/
#endif
		}
	private:
		bool buffersUpdated = false;

		int sampleCount = 4;
		int isRandom = 0;

		RtxGenPass pass1;
		RtxGenPass pass2;
		RtxGenPass pass3;

		std::vector<glm::vec4> gaussHermitWeights;
		VkBuffer ghBuffer;
		VmaAllocation ghBufferAllocation;

		glm::uvec2 pixelQuery;

		VkBuffer collectRtSampleBuffer;
		VmaAllocation collectRtSampleBufferAllocation;
		void* mptrCollectRtSampleBuffer;
		float* ptrCollectRtSampleBuffer;
		ImVec2 meanVar[5]; //0- mean, 1/2 - var x, 3/4 - var y 

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

			float specialWeights[] = { 0.25f, 0.5f, -0.25f, 0.5f, 0.75, 0.5f, -0.75, 0.5f, 1.5f, 1.0f, -1.5f, 1.0f };

			for (size_t i = 0; i < gaussHermitWeights.size(); i++) {
				float x = static_cast<float>(data[2 * i]);
				float w = static_cast<float>(data[2 * i + 1]);
				std::cout << x << " " << w << std::endl;
				gaussHermitWeights[i] = glm::vec4(x, w, 0, 0);
			}

			for (uint32_t i = 0; i < 6; i++) {
				gaussHermitWeights[15 + i] = glm::vec4(specialWeights[2 * i], specialWeights[2 * i + 1], 0, 0);
			}
		}

		VkDescriptorBufferInfo getCollectSamplesDescriptorBufferInfo() const
		{
			VkDescriptorBufferInfo descriptorBufferInfo = {};
			descriptorBufferInfo.buffer = collectRtSampleBuffer;
			descriptorBufferInfo.offset = 0;
			descriptorBufferInfo.range = VK_WHOLE_SIZE;

			return descriptorBufferInfo;
		}
	};

	class RtxCompositionPass
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation, uint32_t layers)
			{
				createImageP(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format, VK_SAMPLE_COUNT_1_BIT, 0, layers);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, layers);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, layers);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, accumImage, accumImageView, accumImageAllocation, 2);
			createTexSampler(device);
			
			pcb.choice = 1;
			pcb.brightness = 1.0f;
			pcb.oWeight = 0.1f;
			globalWorkDim = extent;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& diffTex, const VkImageView& specTex, const VkImageView& rtxView, const VkImageView& rtxView2, const VkImageView& rtxView3, const VkImageView& inMcState)
		{
			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , diffTex,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , specTex,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , rtxView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { texSampler , rtxView2,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { texSampler , rtxView3,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inMcState,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , accumImageView,  VK_IMAGE_LAYOUT_GENERAL });
			
			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_3/rtxCompositionPass.spv");
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
			vkDestroySampler(device, texSampler, nullptr);

			vkDestroyImageView(device, accumImageView, nullptr);
			vmaDestroyImage(allocator, accumImage, accumImageAllocation);
			
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
				ImGui::SliderFloat("Brightness##RtxCompositionPass", &pcb.brightness, 1.0f, 200.0f);
				ImGui::SliderFloat("BlendeWeight##RtxCompositionPass", &pcb.oWeight, 0.0f, 1.0f);
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

		VkExtent2D globalWorkDim;
		VkSampler texSampler;

		VkImage accumImage;
		VkImageView accumImageView;
		VmaAllocation accumImageAllocation;


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

		struct PushConstantBlock
		{
			int choice;
			float brightness;
			float oWeight;
		} pcb;
	};
}