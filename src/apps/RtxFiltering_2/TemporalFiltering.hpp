#include "../../generator.h"
#include "../../helper.h"

namespace RtxFiltering_2
{	
	const uint32_t windowSize = 11;
	
	class SquarePattern
	{
	public:
		SquarePattern()
		{
			maxSamples = 1024;
			minSamples = 4;
			sampleSquareBuffer = VK_NULL_HANDLE;
			sampleSquareBufferAllocation = VK_NULL_HANDLE;
			mptrSampleSquareBuffer = nullptr;

			feedbackBuffer = VK_NULL_HANDLE;
			feedbackBufferAllocation = VK_NULL_HANDLE;
			mptrFeedbackBuffer = nullptr;
			ptrFeedbackBuffer = nullptr;

			seed = 5;
			nSamples = 4;

			dataUpdated = false;
			moveSampleInTime = true;
			choosePattern = 0;
			nLines = 1;

			//xPixelQuery = 1;
			//yPixelQuery = 1;
			//extent = { 10,10 };

			randomSamplesSquare.reserve(maxSamples);

			nonRaytracedSamples.reserve(maxSamples);
			//raytracedSamples.reserve(maxSamples);
			//intersectedSamples.reserve(maxSamples);
		}

		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
		{
			mptrSampleSquareBuffer = static_cast<glm::vec2*>(createBuffer(allocator, sampleSquareBuffer, sampleSquareBufferAllocation, maxSamples * sizeof(glm::vec2), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
			mptrFeedbackBuffer = createBuffer(allocator, feedbackBuffer, feedbackBufferAllocation, maxSamples * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
			ptrFeedbackBuffer = new uint32_t[maxSamples];
		}

		void updateDataPre(const VkExtent2D& extent)
		{
			bool writeToBuffer = false;

			if (dataUpdated == false) {
				randomSamplesSquare.clear();

				if (choosePattern) {
					RandomGenerator rGen(seed);

					for (uint32_t i = 0; i < nSamples; i++) {
						float u = rGen.getNextUint32_t() / 4294967295.0f;
						float v = rGen.getNextUint32_t() / 4294967295.0f;

						randomSamplesSquare.push_back(glm::vec2(u, v));
					}
				}
				else {
					float samplesPerLine = std::ceil((float)nSamples / nLines);
					for (uint32_t j = 0; j < static_cast<uint32_t>(nLines); j++) {
						for (uint32_t i = 0; i < static_cast<uint32_t>(samplesPerLine); i++) {
							float u = static_cast<float>(i) / samplesPerLine;
							float v = static_cast<float>(j) / nLines;
							randomSamplesSquare.push_back(glm::vec2(u, v));
						}
					}
				}

				//std::sort(randomSamplesSquare.begin(), randomSamplesSquare.end(), [](const glm::vec2& lhs, const glm::vec2& rhs)
				//{
				//	if (lhs.x == rhs.x)
				//		return lhs.y < rhs.y;
				//
				//	return lhs.x < rhs.x;
				//});

				writeToBuffer = true;
				dataUpdated = true;
			}

			if (moveSampleInTime) {
				float stepSize = 1.0f / (windowSize - 1) ;
				for (uint32_t i = 0; i < nSamples; i++) {
					randomSamplesSquare[i].y += stepSize * (1 + (rGen.getNextUint32_t() / 4294967295.0f - 0.5f) * 0.f);
					randomSamplesSquare[i].y = randomSamplesSquare[i].y > 1.0 ? randomSamplesSquare[i].y - 1 : randomSamplesSquare[i].y;
				}
				writeToBuffer = true;
			}

			if (writeToBuffer)
				memcpy(mptrSampleSquareBuffer, randomSamplesSquare.data(), randomSamplesSquare.size() * sizeof(glm::vec2));

			//this->extent = extent;
		}

		void updateDataPost()
		{
			memcpy(ptrFeedbackBuffer, mptrFeedbackBuffer, nSamples * sizeof(uint32_t));

			nonRaytracedSamples.clear();
			//raytracedSamples.clear();
			//intersectedSamples.clear();
			for (uint32_t i = 0; i < nSamples; i++) {
				if (ptrFeedbackBuffer[i] == 0)
					nonRaytracedSamples.push_back(randomSamplesSquare[i]);
				//else if (ptrFeedbackBuffer[i] == 1)
					//raytracedSamples.push_back(randomSamplesSquare[i]);
				//else
					//intersectedSamples.push_back(randomSamplesSquare[i]);
			}
		}

		void cleanUp(const VmaAllocator& allocator)
		{
			vmaDestroyBuffer(allocator, sampleSquareBuffer, sampleSquareBufferAllocation);
			vmaDestroyBuffer(allocator, feedbackBuffer, feedbackBufferAllocation);
			delete[] ptrFeedbackBuffer;
		}

		void widget(/*uint32_t& collectData, uint32_t& pixelInfo,*/ uint32_t& _nSamples)
		{
			if (ImGui::CollapsingHeader("RandomPattern")) {
				ImGui::RadioButton("Dynamic samples##UID_SqPattern", &moveSampleInTime, 1); ImGui::SameLine();
				ImGui::RadioButton("Static samples##UID_SqPattern", &moveSampleInTime, 0);

				int nS = static_cast<int>(nSamples);
				ImGui::SliderInt("Sample size##UID_SqPattern", &nS, static_cast<int>(minSamples), static_cast<int>(maxSamples));
				dataUpdated = (nS == nSamples);
				nSamples = nS;


				int cP = choosePattern;
				ImGui::RadioButton("Random pattern##UID_SqPattern", &cP, 1); ImGui::SameLine();
				ImGui::RadioButton("Regular pattern##UID_SqPattern", &cP, 0);
				dataUpdated = dataUpdated && (cP == choosePattern);
				choosePattern = cP;

				if (choosePattern) {
					int sd = static_cast<int>(seed);
					ImGui::SliderInt("Sample seed##UID_SqPattern", &sd, 1, static_cast<int>(maxSamples));
					dataUpdated = dataUpdated && (sd == seed);
					seed = sd;
				}
				else {
					int nL = nLines;
					ImGui::SliderInt("Num Lines##UID_SqPattern", &nL, 1, 50);
					dataUpdated = dataUpdated && (nL == nLines);
					nLines = nL;
				}


				if (ImGui::CollapsingHeader("Sample Visualization##UID_SqPattern")) {

					//ImGui::SliderInt("Pixel X##UID_SqPattern", &xPixelQuery, 0, static_cast<int>(extent.width));
					//ImGui::SliderInt("Pixel Y##UID_SqPattern", &yPixelQuery, 0, static_cast<int>(extent.height));

					//collectData = 1;
					//pixelInfo = (xPixelQuery & 0xffff) | (yPixelQuery & 0xffff) << 16;

					ImGui::SetNextPlotRange(0, 1.0f, 0, 1.0f, ImGuiCond_Always);
					if (ImGui::BeginPlot("Scatter Plot##UID_SqPattern", "U", "V")) {
						//ImGui::Text(("Intersected Samples:" + std::to_string(intersectedSamples.size())).c_str());
						ImGui::PushPlotStyleVar(ImPlotStyleVar_LineWeight, 0);
						ImGui::PushPlotStyleVar(ImPlotStyleVar_Marker, ImMarker_Circle);
						ImGui::PushPlotStyleVar(ImPlotStyleVar_MarkerSize, 4);
						auto getter = [](const void* data, int idx) {
							glm::vec2 d = static_cast<const glm::vec2*>(data)[idx];
							return ImVec2(d.y, d.x);
						};
						ImGui::Plot("Not Raytraced##UID_SqPattern", static_cast<ImVec2(*)(const void*, int)>(getter), static_cast<const void*>(nonRaytracedSamples.data()), static_cast<int>(nonRaytracedSamples.size()), 0);
						//ImGui::Plot("Raytraced##UID_SqPattern", static_cast<ImVec2(*)(const void*, int)>(getter), static_cast<const void*>(raytracedSamples.data()), static_cast<int>(raytracedSamples.size()), 0);
						//ImGui::Plot("Intersected##UID_SqPattern", static_cast<ImVec2(*)(const void*, int)>(getter), static_cast<const void*>(intersectedSamples.data()), static_cast<int>(intersectedSamples.size()), 0);
						ImGui::PopPlotStyleVar(2);
						ImGui::EndPlot();
					}
				}
				//else
					//collectData = 0;
			}

			_nSamples = static_cast<uint32_t>(nSamples);
		}

		VkDescriptorBufferInfo getSquareSamplesDescriptorBufferInfo() const
		{
			VkDescriptorBufferInfo descriptorBufferInfo = {};
			descriptorBufferInfo.buffer = sampleSquareBuffer;
			descriptorBufferInfo.offset = 0;
			descriptorBufferInfo.range = VK_WHOLE_SIZE;

			return descriptorBufferInfo;
		}

		VkDescriptorBufferInfo getFeedbackDescriptorBufferInfo() const
		{
			VkDescriptorBufferInfo descriptorBufferInfo = {};
			descriptorBufferInfo.buffer = feedbackBuffer;
			descriptorBufferInfo.offset = 0;
			descriptorBufferInfo.range = VK_WHOLE_SIZE;

			return descriptorBufferInfo;
		}

	private:
		VkDeviceSize maxSamples;
		VkDeviceSize minSamples;

		VkDeviceSize nSamples;
		uint32_t seed;

		bool dataUpdated;
		int moveSampleInTime;
		int choosePattern; // Psuedo random or structured
		int nLines;

		RandomGenerator rGen;

		std::vector<glm::vec2> randomSamplesSquare;
		VkBuffer sampleSquareBuffer;
		VmaAllocation sampleSquareBufferAllocation;
		glm::vec2* mptrSampleSquareBuffer;

		VkBuffer feedbackBuffer;
		VmaAllocation feedbackBufferAllocation;
		void* mptrFeedbackBuffer;
		uint32_t* ptrFeedbackBuffer;

		//int xPixelQuery;
		//int yPixelQuery;
		//VkExtent2D extent;

		std::vector<glm::vec2> nonRaytracedSamples;
		//std::vector<glm::vec2> raytracedSamples;
		//std::vector<glm::vec2> intersectedSamples;
	};
	
	class TemporalFilter
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& filteredImgView) 
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, uint32_t layers, VkImage& image, VkImageView& imageView, VmaAllocation& allocation, bool makeTexture = false)
			{	
				VkImageUsageFlags flag = makeTexture ? (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT) : VK_IMAGE_USAGE_STORAGE_BIT;
				createImageP(device, allocator, queue, commandPool, image, allocation, extent, flag, format, VK_SAMPLE_COUNT_1_BIT, 0, layers);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, layers);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, layers);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, windowSize, windowImg, windowView, windowAllocation);
			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, windowSize, windowOldImg, windowOldView, windowOldAllocation);
			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, 1, filteredImg, filteredView, filteredAllocation, true);

			filteredImgView = filteredView;
			pcb.windowSize = windowSize;
			buffersUpdated = true;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inRtxPass)
		{
			CHECK_DBG_ONLY(buffersUpdated, "SubSamplePass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inRtxPass,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , windowView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , windowOldView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , filteredView,  VK_IMAGE_LAYOUT_GENERAL });
			
			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/temporalFilter.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			auto destroyImage = [&device = device, &allocator = allocator](VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				vkDestroyImageView(device, imageView, nullptr);
				vmaDestroyImage(allocator, image, allocation);
			};

			destroyImage(windowImg, windowView, windowAllocation);
			destroyImage(windowOldImg, windowOldView, windowOldAllocation);
			destroyImage(filteredImg, filteredView, filteredAllocation);
			
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}
	private:
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;

		ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("TemporalFilteringPass");

		DescriptorSetGenerator descGen = DescriptorSetGenerator("TemporalFilteringPass");
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		VkImage windowImg;
		VkImageView windowView;
		VmaAllocation windowAllocation;

		VkImage windowOldImg;
		VkImageView windowOldView;
		VmaAllocation windowOldAllocation;

		VkImage filteredImg;
		VkImageView filteredView;
		VmaAllocation filteredAllocation;

		bool buffersUpdated = false;

		struct PushConstantBlock {
			uint32_t windowSize;
		} pcb;
	};

}