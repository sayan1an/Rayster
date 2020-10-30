#include "../../generator.h"
#include "../../helper.h"
#include "../../lightSources.h"
#include "../../../shaders/RtxFiltering_2/hostDeviceShared.h"
#include <thread>
#include <chrono>

#if COLLECT_MARKOV_CHAIN_SAMPLES
#if SAVE_SAMPLES_TO_DISK
#include "cnpy.h"
#endif
#endif

namespace RtxFiltering_2
{
	class MarkovChainNoVisibility
	{
	public:
		// Assume extent is twice the size of stencil
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const uint32_t level, const VkExtent2D& extent)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			globalWorkDim = extent;
			pcb.level = level;
			pcb.gamma = 0.0f;
			pcb.sigmaProposal = 0.01f;
			buffersUpdated = true;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const Camera& cam, const AreaLightSources& areaSource, const RandomGenerator& randGen, 
			const VkImageView& outMcState, const VkImageView& outSampleStat, const VkDescriptorBufferInfo& mcSampleInfo,
			const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& inStencil, const VkDescriptorBufferInfo& collectSamples)
		{
			CHECK_DBG_ONLY(buffersUpdated, "MarkovChainNoVisibilityPass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inOther,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inStencil,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, cam.getDescriptorBufferInfo());
			descGen.bindBuffer({ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, randGen.getDescriptorBufferInfo());
			descGen.bindBuffer({ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.getVerticesDescriptorBufferInfo());
			descGen.bindBuffer({ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getCdfDescriptorBufferInfo());
			descGen.bindBuffer({ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getCdfNormDescriptorBufferInfo());
			descGen.bindBuffer({ 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getEmitterIndexMapDescriptorBufferInfo());
			descGen.bindImage({ 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outMcState,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outSampleStat,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, mcSampleInfo);
#if COLLECT_MARKOV_CHAIN_SAMPLES
			descGen.bindBuffer({ 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, collectSamples);
#endif

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/mcNoVis.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

			pcb.cumulativeSum = areaSource.dPdf.cumulativeSum();
			pcb.uniformToEmitterIndexMapSize = areaSource.dPdf.size().y;
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);

			buffersUpdated = false;
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf, const float sigma, const float gamma, const glm::uvec2& pixelQuery, const int resetWeight)
		{	
			pcb.sigmaProposal = sigma;
			pcb.gamma = gamma;
#if COLLECT_MARKOV_CHAIN_SAMPLES
			pcb.pixelQueryX = pixelQuery.x;
			pcb.pixelQueryY = pixelQuery.y;
			pcb.resetWeight = resetWeight;
#endif
			
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
			vkCmdDispatch(cmdBuf, 1 + (globalWorkDim.width - 1) / 2, 1 + (globalWorkDim.height - 1) / 2, 1);
		}
	private:
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;

		ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("MarkovChainNoVisibilityPass");

		DescriptorSetGenerator descGen = DescriptorSetGenerator("MarkovChainNoVisibilityPass");
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		bool buffersUpdated = false;

		VkExtent2D globalWorkDim;
		
		struct PushConstantBlock {
			uint32_t level;
			float cumulativeSum;
			uint32_t uniformToEmitterIndexMapSize;
			float sigmaProposal;
			float gamma;
	
#if COLLECT_MARKOV_CHAIN_SAMPLES
			uint32_t pixelQueryX;
			uint32_t pixelQueryY;
			int resetWeight;
#endif
		} pcb;
	};

	class MarkovChainNoVisibilityCombined
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& _mcStateView, VkImageView& _sampleStatView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImageP(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage(extent, VK_FORMAT_R32G32_SFLOAT, mcState, mcStateView, mcStateAlloc);
			makeImage(extent, VK_FORMAT_R32G32_UINT, sampleStat, sampleStatView, sampleStatAlloc);
			
			_mcStateView = mcStateView;
			_sampleStatView = sampleStatView;

			McSampleInfo* initData = new McSampleInfo[extent.width * extent.height];
			initData[0].mean = glm::vec2(DEFAULT_MC_SAMPLE_MEAN);
			initData[0].var = glm::vec2(DEFAULT_MC_SAMPLE_VAR);
			initData[0].covWeight = glm::vec2(0.0f);
			initData[0].maxVal = glm::vec2(0.0f);
			for (uint32_t i = 1; i < extent.width * extent.height; i++)
				initData[i] = initData[0];
		
			createBuffer(device, allocator, queue, commandPool, mcSampleInfo, mcSampleInfoAlloc, extent.width * extent.height * sizeof(McSampleInfo), initData, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

			mptrCollectMcSampleBuffer = createBuffer(allocator, collectMcSampleBuffer, collectMcSampleBufferAllocation, MAX_MARKOV_CHAIN_SAMPLES * sizeof(float) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
			ptrCollectMcSampleBuffer = new float[MAX_MARKOV_CHAIN_SAMPLES * 4];

			mcPass1.createBuffers(device, allocator, queue, commandPool, 0, extent);
			mcPass2.createBuffers(device, allocator, queue, commandPool, 1, { extent.width / 2, extent.height / 2 });
			mcPass3.createBuffers(device, allocator, queue, commandPool, 2, { extent.width / 4, extent.height / 4 });
			
			buffersUpdated = true;

			delete[]initData;
		}

		void createPipelines(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const Camera& cam, const AreaLightSources &areaSource, const RandomGenerator& randGen,
			const VkImageView& inNormal1, const VkImageView& inOther1, const VkImageView& inStencil1,
			const VkImageView& inNormal2, const VkImageView& inOther2, const VkImageView& inStencil2, 
			const VkImageView& inNormal3, const VkImageView& inOther3, const VkImageView& inStencil3)
		{
			CHECK_DBG_ONLY(buffersUpdated, "MarkovChainNoVisibilityCombined : call createBuffers first.");

			mcPass1.createPipeline(physicalDevice, device, cam, areaSource, randGen, mcStateView, sampleStatView, getSampleInfoDescriptorBufferInfo(), inNormal1, inOther1, inStencil1, getCollectSamplesDescriptorBufferInfo());
			mcPass2.createPipeline(physicalDevice, device, cam, areaSource, randGen, mcStateView, sampleStatView, getSampleInfoDescriptorBufferInfo(), inNormal2, inOther2, inStencil2, getCollectSamplesDescriptorBufferInfo());
			mcPass3.createPipeline(physicalDevice, device, cam, areaSource, randGen, mcStateView, sampleStatView, getSampleInfoDescriptorBufferInfo(), inNormal3, inOther3, inStencil3, getCollectSamplesDescriptorBufferInfo());
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{	
			mcPass1.cmdDispatch(cmdBuf, sigmaProposal, gammaDifferentialEvolution, pixelQuery, resetWeight);
			mcPass2.cmdDispatch(cmdBuf, sigmaProposal, gammaDifferentialEvolution, pixelQuery / glm::uvec2(2,2), resetWeight);
			mcPass3.cmdDispatch(cmdBuf, sigmaProposal, gammaDifferentialEvolution, pixelQuery / glm::uvec2(4,4), resetWeight);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, mcStateView, nullptr);
			vmaDestroyImage(allocator, mcState, mcStateAlloc);
			vkDestroyImageView(device, sampleStatView, nullptr);
			vmaDestroyImage(allocator, sampleStat, sampleStatAlloc);
			vmaDestroyBuffer(allocator, mcSampleInfo, mcSampleInfoAlloc);

			vmaDestroyBuffer(allocator, collectMcSampleBuffer, collectMcSampleBufferAllocation);
			delete[]ptrCollectMcSampleBuffer;

			mcPass3.cleanUp(device, allocator);
			mcPass2.cleanUp(device, allocator);
			mcPass1.cleanUp(device, allocator);

			buffersUpdated = false;
		}

		void widget(const VkExtent2D& swapChainExtent)
		{
			if (ImGui::CollapsingHeader("MarkovChainPass")) {

				ImGui::SliderFloat("Sigma MC proposal##MarkovChainPass", &sigmaProposal, 0.0f, 0.25f);
				ImGui::SliderFloat("Gamma DE##MarkovChainPass", &gammaDifferentialEvolution, 0.0f, 1.0f);

#if COLLECT_MARKOV_CHAIN_SAMPLES
				int xQuery = static_cast<int>(pixelQuery.x);
				int yQuery = static_cast<int>(pixelQuery.y);
				ImGui::SliderInt("X##Combined_RT_MC_Pass", &xQuery, 0, swapChainExtent.width - 1);
				ImGui::SliderInt("Y##Combined_RT_MC_Pass", &yQuery, 0, swapChainExtent.height - 1);
				pixelQuery.x = static_cast<uint32_t>(xQuery);
				pixelQuery.y = static_cast<uint32_t>(yQuery);

				ImGui::Text("Reset weight:");
				ImGui::RadioButton("No##MarkovChainPass_1", &resetWeight, 0); ImGui::SameLine();
				ImGui::RadioButton("Yes##MarkovChainPass_1", &resetWeight, 1);
				
				meanVar[0] = ImVec2(ptrCollectMcSampleBuffer[4 * 1], ptrCollectMcSampleBuffer[4 * 1 + 1]);
				meanVar[1] = ImVec2(meanVar[0]); meanVar[1].x -= std::sqrt(ptrCollectMcSampleBuffer[4 * 2]);
				meanVar[2] = ImVec2(meanVar[0]); meanVar[2].x += std::sqrt(ptrCollectMcSampleBuffer[4 * 2]);
				meanVar[3] = ImVec2(meanVar[0]); meanVar[3].y -= std::sqrt(ptrCollectMcSampleBuffer[4 * 2 + 1]);
				meanVar[4] = ImVec2(meanVar[0]); meanVar[4].y += std::sqrt(ptrCollectMcSampleBuffer[4 * 2 + 1]);

				ImPlot::SetNextPlotLimits(0, 1.0f, 0, 1.0f, ImGuiCond_Always);
				if (ImPlot::BeginPlot("Gauss Approx Plot##MarkovChainPass", "u", "v")) {
					ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 0);
					ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 4);

					ImPlot::PlotScatter("Mean##MarkovChainPass", &meanVar[0].x, &meanVar[0].y, 1, 0, sizeof(ImVec2));
					ImPlot::PlotScatter("Std-X##MarkovChainPass", &meanVar[1].x, &meanVar[1].y, 2, 0, sizeof(ImVec2));
					ImPlot::PlotScatter("Std-Y##MarkovChainPass", &meanVar[3].x, &meanVar[3].y, 2, 0, sizeof(ImVec2));

					ImPlot::PopStyleVar(2);
					ImPlot::EndPlot();
				}

				ImPlot::SetNextPlotLimits(0, 1.0f, 0, 1.0f, ImGuiCond_Always);
				if (ImPlot::BeginPlot("Scatter Plot##MarkovChainPass", "u", "v")) {
					ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 0);
					ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 4);
					auto getter = [](void* data, int idx) {
						glm::vec4 d = static_cast<const glm::vec4*>(data)[idx + MC_SAMPLE_HEADER_SIZE];
						//std::this_thread::sleep_for(std::chrono::milliseconds(100));
						return ImPlotPoint(d.x, d.y);
					};
					ImPlot::PlotScatterG("Samples##MarkovChainPass_2", static_cast<ImPlotPoint(*)(void*, int)>(getter), static_cast<void*>(ptrCollectMcSampleBuffer), static_cast<int>(ptrCollectMcSampleBuffer[1]), 0);
					
					ImPlot::PopStyleVar(2);
					ImPlot::EndPlot();
				}
				
#if SAVE_SAMPLES_TO_DISK
				ImGui::Text("Save pixel data");
				int stateOld = savePixelData;
				ImGui::RadioButton("No##MarkovChainPass_2", &savePixelData, 0); ImGui::SameLine();
				ImGui::RadioButton("Yes##MarkovChainPass_2", &savePixelData, 1);

				if (stateOld == 0 && savePixelData == 1) {
					fileIdx++;
					resetWeight = 0;
				}

				if (savePixelData) {
					// let's not worry about performance here :P
					std::vector<size_t> shape;
					size_t headerSize = MC_SAMPLE_HEADER_SIZE;
					size_t nSamples = static_cast<size_t>(ptrCollectMcSampleBuffer[1]);
					shape.push_back(nSamples + headerSize);
					shape.push_back(size_t(4));
					
					cnpy::npy_save("mcSamples_" + std::to_string(fileIdx) + ".npy", ptrCollectMcSampleBuffer, shape, "a");
				}
#endif
#endif
			}

		}

		VkDescriptorBufferInfo getSampleInfoDescriptorBufferInfo() const
		{	
			CHECK_DBG_ONLY(buffersUpdated, "MarkovChainNoVisibilityCombined : call createBuffers first.");

			VkDescriptorBufferInfo descriptorBufferInfo = {};
			descriptorBufferInfo.buffer = mcSampleInfo;
			descriptorBufferInfo.offset = 0;
			descriptorBufferInfo.range = VK_WHOLE_SIZE;

			return descriptorBufferInfo;
		}

		void updateDataPost()
		{
#if COLLECT_MARKOV_CHAIN_SAMPLES
			//static float lastX = 0;
			//static float lastY = 0;
			memcpy(ptrCollectMcSampleBuffer, mptrCollectMcSampleBuffer, MAX_MARKOV_CHAIN_SAMPLES * sizeof(float) * 4);
			//uint32_t numSamples = (uint32_t)ptrCollectMcSampleBuffer[1];

			//std::cout << numSamples << std::endl;
			//std::cout << (std::abs(ptrCollectMcSampleBuffer[2] - lastX) > 0.25f) << " " << (std::abs(ptrCollectMcSampleBuffer[3] - lastY) > 0.25f) << std::endl;

			//lastX = ptrCollectMcSampleBuffer[4 * (numSamples + SAMPLE_HEADER_SIZE - 1)];
			//lastY = ptrCollectMcSampleBuffer[4 * (numSamples + SAMPLE_HEADER_SIZE - 1) + 1];
			//std::cout << ptrCollectMcSampleBuffer[2] << " " << ptrCollectMcSampleBuffer[3] << " " << ptrCollectMcSampleBuffer[2*numSamples] << " " << ptrCollectMcSampleBuffer[2*numSamples + 1] << std::endl;
#endif
		}

	private:
		bool buffersUpdated = false;

		VkImage mcState;
		VkImageView mcStateView;
		VmaAllocation mcStateAlloc;

		VkImage sampleStat;
		VkImageView sampleStatView;
		VmaAllocation sampleStatAlloc;

		VkBuffer mcSampleInfo;
		VmaAllocation mcSampleInfoAlloc;
		int resetWeight = 1;

		VkBuffer collectMcSampleBuffer;
		VmaAllocation collectMcSampleBufferAllocation;
		void* mptrCollectMcSampleBuffer;
		float* ptrCollectMcSampleBuffer;
		ImVec2 meanVar[5]; //0- mean, 1/2 - var x, 3/4 - var y 

		glm::uvec2 pixelQuery;
		
		int savePixelData = 0, fileIdx = 0;

		float gammaDifferentialEvolution = 0.4f;
		float sigmaProposal = 0.15f;

		MarkovChainNoVisibility mcPass1;
		MarkovChainNoVisibility mcPass2;
		MarkovChainNoVisibility mcPass3;

		VkDescriptorBufferInfo getCollectSamplesDescriptorBufferInfo() const
		{
			VkDescriptorBufferInfo descriptorBufferInfo = {};
			descriptorBufferInfo.buffer = collectMcSampleBuffer;
			descriptorBufferInfo.offset = 0;
			descriptorBufferInfo.range = VK_WHOLE_SIZE;

			return descriptorBufferInfo;
		}
	};

}