#pragma once

#include "generator.h"
#include "helper.h"
#include "stb_image_write.h"
#include "tinyexr.h"
#include "../shaders/Filters/filterParams.h"

class DummyFilter
{
public:
	void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inNoisyImage, const VkImageView& outDenoisedImage)
	{
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/dummyFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D& screenExtent)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		vkCmdDispatch(cmdBuf, 1 + (screenExtent.width - 1) / 16, 1 + (screenExtent.height - 1) / 16, 1);
	}

	void cleanUp(const VkDevice& device)
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}

	void widget()
	{
		if (ImGui::CollapsingHeader("DummyFilter")) {

		}
	}

	DummyFilter()
	{
		
	}
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("DummyFilter");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("DummyFilter");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
};

class CrossBilateralFilter
{
public:
	void createPipeline(const VkPhysicalDevice &physicalDevice, const VkDevice& device, const VkImageView &inDiffuseColor, const VkImageView& inSpecularColor, 
		const VkImageView &inNormal, const VkImageView &inNoisyImage, const VkImageView &outDenoisedImage)
	{	
		CHECK(queryComputeSharedMemSize(physicalDevice) > sharedMemRequired(), "CrossBilateralFilter: Insufficent local shared (on-device) memory.");

		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inDiffuseColor,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inSpecularColor,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/crossBilateralFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D &screenExtent, const uint32_t filterSize = 9)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		pcb.extent = screenExtent;
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
		vkCmdDispatch(cmdBuf, 1 + (screenExtent.width - 1) / CBF_O_TILE_WIDTH, 1 + (screenExtent.height - 1) / CBF_O_TILE_WIDTH, 1);
	}

	void cleanUp(const VkDevice &device)
	{	
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}
	
	void widget()
	{	
		if (ImGui::CollapsingHeader("CrossBilateralFilter")) {
			ImGui::Text("Mode:"); ImGui::SameLine();
			ImGui::RadioButton("Cross##UID_CBFilter", &pcb.mode, FILTER_MODE_CROSS); ImGui::SameLine();
			ImGui::RadioButton("Bilateral##UID_CBFilter", &pcb.mode, FILTER_MODE_BILATERAL); ImGui::SameLine();
			ImGui::RadioButton("Unilateral##UID_CBFilter", &pcb.mode, FILTER_MODE_UNILATERAL);
			ImGui::SliderFloat("Filter size", &pcb.filterSize, 0.02f, 5.0f);
		}
	}

	CrossBilateralFilter()
	{	
		pcb.filterSize = 1.0f;
		pcb.mode = FILTER_MODE_CROSS;
	}
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("CrossBilateralFilter");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("CrossBilateralFilter");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	struct PushConstantBlock
	{
		VkExtent2D extent;
		float filterSize;
		int mode;
	} pcb;

	uint32_t sharedMemRequired() 
	{
		CHECK(IS_POWER_2(CBF_I_TILE_WIDTH), "CrossBilateralFilter:Input tile size must be a power of 2");
		return static_cast<uint32_t>(sizeof(glm::vec3) * CBF_I_TILE_WIDTH * CBF_I_TILE_WIDTH * 3);
	}
};

class TemporalFilter
{
public:
	void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inNoisyImage, const VkImageView& outDenoisedImage)
	{	
		CHECK_DBG_ONLY(buffersUpdated, "TemporalFilter : call createBuffers first.");

		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , accumImageView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/temporalFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& screenExtent)
	{
		VkFormat imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		createImageP(device, allocator, queue, commandPool, accumImage, accumImageAllocation, screenExtent, VK_IMAGE_USAGE_STORAGE_BIT, imageFormat);
		accumImageView = createImageView(device, accumImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		transitionImageLayout(device, queue, commandPool, accumImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
		buffersUpdated = true;
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D& screenExtent)
	{	
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		// update push constant block
		pcb.frameIndex++;
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
		vkCmdDispatch(cmdBuf, 1 + (screenExtent.width - 1) / 16, 1 + (screenExtent.height - 1) / 16, 1);

		if (reset == 1)
			pcb.frameIndex = 0;
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
	{	
		vkDestroyImageView(device, accumImageView, nullptr);
		vmaDestroyImage(allocator, accumImage, accumImageAllocation);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		buffersUpdated = false;
	}

	void widget()
	{
		if (ImGui::CollapsingHeader("TemporalFilter")) {
			ImGui::Text("Filter type"); ImGui::SameLine();
			int isExp = pcb.isExponential & 1;
			ImGui::RadioButton("Uniform##UID_TemporalFilter", &isExp, 0); ImGui::SameLine();
			ImGui::RadioButton("Exponential##UID_TemporalFilter", &isExp, 1);
			if (isExp & 1) {
				ImGui::SliderInt("Coarse##UID_TemporalFilter", &coarseExp, 1, 99);
				ImGui::SliderInt("Fine##UID_TemporalFilter", &fineExp, 1, 99);

				pcb.isExponential = (coarseExp * 100 + fineExp) << 1;
			}
			else {
				ImGui::Text("Reset"); ImGui::SameLine();
				ImGui::RadioButton("No##UID_TemporalFilter", &reset, 0); ImGui::SameLine();
				ImGui::RadioButton("Yes##UID_TemporalFilter", &reset, 1);
			}

			if (isExp > 0)
				pcb.frameIndex = 0;

			pcb.isExponential = (pcb.isExponential & 0xfffffffe) | (isExp & 1);
		}
	}

	TemporalFilter()
	{
		pcb.frameIndex = 0;
		pcb.isExponential = 0;

		buffersUpdated = false;
		accumImage = VK_NULL_HANDLE;
		accumImageAllocation = VK_NULL_HANDLE;
		accumImageView = VK_NULL_HANDLE;

		reset = 1;
		coarseExp = 95;
		fineExp = 99;
	}
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("TemporalFilter");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("TemporalFilter");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	struct PushConstantBlock
	{
		uint32_t frameIndex;
		uint32_t isExponential;
	} pcb;

	VkImage accumImage;
	VmaAllocation accumImageAllocation;
	VkImageView accumImageView;

	bool buffersUpdated;
	int reset;
	int coarseExp;
	int fineExp;
};

class TemporalWindowFilter
{
public:
	void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const Camera &cam, const VkImageView& inNoisyImage, const VkImageView& inNormalImage, const VkImageView& inDiffuseCol,
		const VkImageView& inCameraDepth, const VkImageView& outDenoisedImage)
	{
		CHECK_DBG_ONLY(buffersUpdated, "TemporalWindowFilter : call createBuffers first.");

		descGen.bindBuffer({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, cam.getDescriptorBufferInfo());
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormalImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inDiffuseCol,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inCameraDepth,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , accumImageView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , depthAccumImageView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindBuffer({ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { viewProjBuffer, 0, sizeof(glm::mat4) * 2 * MAX_TEMPORAL_WIND_FILT_SAMPLES });
		descGen.bindImage({ 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/temporalWindowFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& screenExtent)
	{
		VkFormat imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		createImageP(device, allocator, queue, commandPool, accumImage, accumImageAllocation, screenExtent, VK_IMAGE_USAGE_STORAGE_BIT, imageFormat, VK_SAMPLE_COUNT_1_BIT, 0, MAX_TEMPORAL_WIND_FILT_SAMPLES);
		accumImageView = createImageView(device, accumImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, MAX_TEMPORAL_WIND_FILT_SAMPLES);
		transitionImageLayout(device, queue, commandPool, accumImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, MAX_TEMPORAL_WIND_FILT_SAMPLES);

		createImageP(device, allocator, queue, commandPool, depthAccumImage, depthAccumImageAllocation, screenExtent, VK_IMAGE_USAGE_STORAGE_BIT, VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, 0, MAX_TEMPORAL_WIND_FILT_SAMPLES);
		depthAccumImageView = createImageView(device, depthAccumImage, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, MAX_TEMPORAL_WIND_FILT_SAMPLES);
		transitionImageLayout(device, queue, commandPool, depthAccumImage, VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, MAX_TEMPORAL_WIND_FILT_SAMPLES);

		createBuffer(device, allocator, queue, commandPool, viewProjBuffer, viewProjBufferAllocation, sizeof(glm::mat4) * 2 * MAX_TEMPORAL_WIND_FILT_SAMPLES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
				
		buffersUpdated = true;
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D& screenExtent)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		// update push constant block
		pcb.frameIndex++;
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
		vkCmdDispatch(cmdBuf, 1 + (screenExtent.width - 1) / 16, 1 + (screenExtent.height - 1) / 16, 1);
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
	{
		vkDestroyImageView(device, accumImageView, nullptr);
		vmaDestroyImage(allocator, accumImage, accumImageAllocation);
		vkDestroyImageView(device, depthAccumImageView, nullptr);
		vmaDestroyImage(allocator, depthAccumImage, depthAccumImageAllocation);
		vmaDestroyBuffer(allocator, viewProjBuffer, viewProjBufferAllocation);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		buffersUpdated = false;
	}

	void widget()
	{
		if (ImGui::CollapsingHeader("TemporalWindowFilter")) {
			int wS = static_cast<int>(pcb.windowSize);
			ImGui::SliderInt("WindowSize##UID_TemporalWindowFilter", &wS, 1, MAX_TEMPORAL_WIND_FILT_SAMPLES);
			pcb.windowSize = static_cast<uint32_t>(wS);

			int toggle = (pcb.reset >> 8) & 0xff;
			ImGui::RadioButton("Simple##UID_TemporalWindowFilter", &toggle, 0); ImGui::SameLine();
			ImGui::RadioButton("Complex##UID_TemporalWindowFilter", &toggle, 1);
			
			pcb.reset = ((wS != pcb.windowSize)  || (toggle != ((pcb.reset >> 8) & 0xff))) | (toggle << 8);
		}
	}

	TemporalWindowFilter()
	{
		pcb.frameIndex = 0;
		pcb.windowSize = 21;
		pcb.reset = 0;
	
		buffersUpdated = false;
		
		accumImage = VK_NULL_HANDLE;
		accumImageAllocation = VK_NULL_HANDLE;
		accumImageView = VK_NULL_HANDLE;

		depthAccumImage = VK_NULL_HANDLE;
		depthAccumImageAllocation = VK_NULL_HANDLE;
		depthAccumImageView = VK_NULL_HANDLE;

		viewProjBuffer = VK_NULL_HANDLE;
		viewProjBufferAllocation = VK_NULL_HANDLE;
	}
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("TemporalFilter");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("TemporalFilter");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	struct PushConstantBlock
	{
		uint32_t frameIndex;
		uint32_t windowSize;
		uint32_t reset;
	} pcb;

	VkImage accumImage;
	VmaAllocation accumImageAllocation;
	VkImageView accumImageView;

	VkImage depthAccumImage;
	VmaAllocation depthAccumImageAllocation;
	VkImageView depthAccumImageView;

	VkBuffer viewProjBuffer;
	VmaAllocation viewProjBufferAllocation;

	bool buffersUpdated;
};


class TemporalFrequencyFilter
{
public:
	void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inNoisyImage, const VkImageView& outDenoisedImage)
	{	
		CHECK_DBG_ONLY(buffersCreated, "TemporalFrequencyFilter : call createBuffers first.");

		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , accumImageView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindBuffer({2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, getDftDescriptorBufferInfo());
		descGen.bindBuffer({3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, getPixelMagSpectDescriptorBufferInfo());
		descGen.bindImage({4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/temporalFrequencyFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D& screenExtent)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
		vkCmdDispatch(cmdBuf, 1 + (screenExtent.width - 1) / 16, 1 + (screenExtent.height - 1) / 16, 1);

		// update push constant block
		pcb.frameIndex++;
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& screenExtent)
	{	
		VkFormat imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		createImageP(device, allocator, queue, commandPool, accumImage, accumImageAllocation, screenExtent, VK_IMAGE_USAGE_STORAGE_BIT, imageFormat, VK_SAMPLE_COUNT_1_BIT, 0, MAX_TEMPORAL_FREQ_FILT_SAMPLES);
		accumImageView = createImageView(device, accumImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, MAX_TEMPORAL_FREQ_FILT_SAMPLES);
		transitionImageLayout(device, queue, commandPool, accumImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, MAX_TEMPORAL_FREQ_FILT_SAMPLES);

		uint64_t dftComponents = (MAX_TEMPORAL_FREQ_FILT_SAMPLES >> 1) + 1;
		VkDeviceSize dftBufferSize = 6 * sizeof(float) * dftComponents * screenExtent.width * screenExtent.height;
		createBuffer(device, allocator, queue, commandPool, dftBuffer, dftBufferAllocation, dftBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		
		pcb.imageInfo = (screenExtent.width & 0xffff) | (screenExtent.height << 16);

		ptrPixelMagnitudeSpectrumMapped = createBuffer(allocator, pixelMagnitudeSpectrumBuffer, pixelMagnitudeSpectrumAllocation, dftComponents * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
		ptrPixelMagnitudeSpectrum = new float[dftComponents];

		buffersCreated = true;
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
	{	
		vmaDestroyBuffer(allocator, pixelMagnitudeSpectrumBuffer, pixelMagnitudeSpectrumAllocation);
		vmaDestroyBuffer(allocator, dftBuffer, dftBufferAllocation);
		vkDestroyImageView(device, accumImageView, nullptr);
		vmaDestroyImage(allocator, accumImage, accumImageAllocation);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		buffersCreated = false;
	}

	void updateData()
	{	
		if ((pcb.dftInfo >> 24) == TEMPORAL_FREQ_FILT_MODE_5)
			memcpy(ptrPixelMagnitudeSpectrum, ptrPixelMagnitudeSpectrumMapped, ((MAX_TEMPORAL_FREQ_FILT_SAMPLES >> 1) + 1) * sizeof(float));
	}

	void widget(const IO &io)
	{
		if (ImGui::CollapsingHeader("TemporalFrequencyFilter")) {
			int tSamples = pcb.dftInfo & 0xff;
			int dftComponent = (pcb.dftInfo >> 8) & 0xff;
			int sampleComponent = (pcb.dftInfo >> 16) & 0xff;
			int mode = (pcb.dftInfo >> 24) & 0xff;
						
			ImGui::SliderInt("Temporal samples##UID_TemporalFreqFilter", &tSamples, 1, MAX_TEMPORAL_FREQ_FILT_SAMPLES);
			
			if (tSamples != (pcb.dftInfo & 0xff))
				mode = TEMPORAL_FREQ_FILT_MODE_4;

			ImGui::Text("Select output modes:");
			ImGui::RadioButton("Filtered output##UID_TemporalFreqFilter", &mode, TEMPORAL_FREQ_FILT_MODE_0);
			ImGui::RadioButton("Test filtered output##UID_TemporalFreqFilter", &mode, TEMPORAL_FREQ_FILT_MODE_1);
			ImGui::RadioButton("Display DFT components##UID_TemporalFreqFilter", &mode, TEMPORAL_FREQ_FILT_MODE_2);
			ImGui::RadioButton("Test DFT output##UID_TemporalFreqFilter", &mode, TEMPORAL_FREQ_FILT_MODE_3);
			ImGui::RadioButton("Show pixel DFT##UID_TemporalFreqFilter", &mode, TEMPORAL_FREQ_FILT_MODE_5);
			ImGui::RadioButton("Reset DFT buffer##UID_TemporalFreqFilter", &mode, TEMPORAL_FREQ_FILT_MODE_4);
			
			if (mode == TEMPORAL_FREQ_FILT_MODE_0 || mode == TEMPORAL_FREQ_FILT_MODE_1)
				ImGui::SliderInt("Sample component##UID_TemporalFreqFilter", &sampleComponent, 0, tSamples - 1);
			else if (mode == TEMPORAL_FREQ_FILT_MODE_2)
				ImGui::SliderInt("DFT component##UID_TemporalFreqFilter", &dftComponent, 0, (tSamples >> 1));
			else if (mode == TEMPORAL_FREQ_FILT_MODE_5) {
				int px = pcb.pixelInfo & 0xffff;
				int py = pcb.pixelInfo >> 16;
				ImGui::SliderInt("Pixel X coord##UID_TemporalFreqFilter", &px, 0, (pcb.imageInfo & 0xffff) - 1);
				ImGui::SliderInt("Pixel Y coord##UID_TemporalFreqFilter", &py, 0, (pcb.imageInfo >> 16) - 1);
				pcb.pixelInfo = (px & 0xffff) | (py << 16);

				uint32_t maxFrequency = static_cast<uint32_t>(std::round(500.0f / io.getAvgFrameTime()));
				ImGui::PlotHistogram(std::to_string(static_cast<uint32_t>(ptrPixelMagnitudeSpectrum[0])).c_str(), ptrPixelMagnitudeSpectrum, (tSamples >> 1) + 1, 0, "Magnitude spectrum", 0, ptrPixelMagnitudeSpectrum[0], ImVec2(0, 50));
				ImGui::Text("0 Hz"); ImGui::SameLine(); ImGui::Dummy(ImVec2(220.0f, 0.0f));  ImGui::SameLine(); ImGui::Text((std::to_string(maxFrequency) + " Hz").c_str());
			}

			pcb.dftInfo = ((mode & 0xff) << 24) | ((sampleComponent & 0xff) << 16) | ((dftComponent  & 0xff) << 8) | (tSamples & 0xff);
		}
	}

	TemporalFrequencyFilter()
	{	
		accumImage = VK_NULL_HANDLE;
		accumImageAllocation = VK_NULL_HANDLE;
		accumImageView = VK_NULL_HANDLE;

		dftBuffer = VK_NULL_HANDLE;
		dftBufferAllocation = VK_NULL_HANDLE;

		pixelMagnitudeSpectrumBuffer = VK_NULL_HANDLE;
		pixelMagnitudeSpectrumAllocation = VK_NULL_HANDLE;
		ptrPixelMagnitudeSpectrumMapped = nullptr;

		buffersCreated = false;

		pcb.frameIndex = 0;
		pcb.dftInfo= MAX_TEMPORAL_FREQ_FILT_SAMPLES;
		pcb.pixelInfo = 25 | (25 << 16);
	}
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("TemporalFrequencyFilterFilter");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("TemporalFrequencyFilterFilter");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkImage accumImage;
	VmaAllocation accumImageAllocation;
	VkImageView accumImageView;

	VkBuffer dftBuffer;
	VmaAllocation dftBufferAllocation;

	VkBuffer pixelMagnitudeSpectrumBuffer;
	VmaAllocation  pixelMagnitudeSpectrumAllocation;
	void* ptrPixelMagnitudeSpectrumMapped;
	float* ptrPixelMagnitudeSpectrum;

	bool buffersCreated;

	struct PushConstantBlock
	{
		uint32_t frameIndex;
		uint32_t dftInfo; // Starting LSB  8 bit - number of samples, 8 bit - dftComponent to display, 8 bit - index of sample to recover after filtering (0 is oldest while max is latest), 8 bit mode 
		uint32_t imageInfo; // Staring LSB 16 bit - image width, 16 bit - image height
		uint32_t pixelInfo; // Starting LSB 16 - pixel x coord, 16 bit - pixel y coord
	} pcb;

	VkDescriptorBufferInfo getDftDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = dftBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkDescriptorBufferInfo getPixelMagSpectDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = pixelMagnitudeSpectrumBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}
};

class SaveFramePass
{	
public:
	void createBuffer(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkImageLayout layout, const VkExtent2D& extent, const VkFormat format, const uint32_t mipLevels, const uint32_t layers)
	{	
		VkDeviceSize bufferSizeBytes = layers * extent.height * (extent.width * imageFormatToBytes(format));
		mptrStagingBuffer = createStagingBuffer(device, allocator, queue, commandPool, stagingBuffer, stagingBufferAllocation, bufferSizeBytes);

		uint8Pixels = new uint8_t[extent.width * extent.height * numChannelsToSave];

		imgExtent = extent;
		imgFormat = format;
		imgLayout = layout;
		imgMips = mipLevels;
		imgLayers = layers;

		exrBlob.createBlob(imgExtent, numChannelsToSave);
		buffersUpdated = true;
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, VkImage &image)
	{	
		if (saveFrame > 0) {
			CHECK_DBG_ONLY(buffersUpdated, "SaveFramePass : call createBuffers first.");
			vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT,
				0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
			cmdTransitionImageLayout(cmdBuf, image, imgFormat, imgLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imgMips, imgLayers);
			cmdCopyImageToBuffer(cmdBuf, image, stagingBuffer, imgExtent, imgFormat, imgLayers);
			cmdTransitionImageLayout(cmdBuf, image, imgFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imgLayout, imgMips, imgLayers);
		}
	}

	void cleanUp(const VmaAllocator& allocator)
	{	
		vmaUnmapMemory(allocator, stagingBufferAllocation);
		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
		delete[] uint8Pixels;
		exrBlob.cleanUp();
		buffersUpdated = false;
	}

	void toDisk(std::string path)
	{	
		if (saveFrame > 0) {

			if (type == 0)
				exrBlob.toExr(imgExtent, mptrStagingBuffer, path + std::to_string(frameIndex) + ".exr");
			else if (type == 1) {
				floatToUcharImage();
				stbi_write_jpg((path + std::to_string(frameIndex) + ".jpg").c_str(), imgExtent.width, imgExtent.height, numChannelsToSave, uint8Pixels, 100);
			}
			frameIndex++;
		}
	}

	void widget()
	{
		if (ImGui::CollapsingHeader("SaveFramePass")) {
			ImGui::Text("Save Frame:"); ImGui::SameLine();
			ImGui::RadioButton(("Yes" + randomUID).c_str(), &saveFrame, 1); ImGui::SameLine();
			ImGui::RadioButton(("No" + randomUID).c_str(), &saveFrame, 0);

			ImGui::Text("Type:"); ImGui::SameLine();
			ImGui::RadioButton(("Exr" + randomUID).c_str(), &type, 0); ImGui::SameLine();
			ImGui::RadioButton(("Jpg" + randomUID).c_str(), &type, 1);
		}
	}

	SaveFramePass()
	{
		buffersUpdated = false;
		saveFrame = 0;
		frameIndex = 0;
		type = 0;
		randomUID = "##UID_SaveFramePass" + std::to_string(rGen.getNextUint32_t());
	}
private:
	VkBuffer stagingBuffer;
	VmaAllocation stagingBufferAllocation;
	void* mptrStagingBuffer;
	uint8_t* uint8Pixels;
	
	VkExtent2D imgExtent;
	VkFormat imgFormat;
	VkImageLayout imgLayout;
	uint32_t imgMips;
	uint32_t imgLayers;
	
	RandomGenerator rGen;
	std::string randomUID;
	bool buffersUpdated;
	int saveFrame;
	uint64_t frameIndex;
	uint32_t numChannelsToSave = 3;
	int type; // 0 -exr, 1-jpg
	
	void floatToUcharImage()
	{	
		uint32_t indexDst = 0;
		uint32_t indexSrc = 0;
		float* pBuffer = static_cast<float*>(mptrStagingBuffer);

		for (uint32_t j = imgExtent.height - 1; (int)j >= 0; --j)
		{
			for (uint32_t i = 0; i < imgExtent.width; ++i)
			{	
				for (uint32_t c = 0; c < numChannelsToSave; c++)
				{
					int d = static_cast<int>(pBuffer[indexSrc + c] * 255.9f);
					uint8Pixels[indexDst + c] = std::clamp(d, 0, 255);
				}

				indexDst += numChannelsToSave;
				indexSrc += 4;
			}
		}
	}

	struct ExrBlob {
		std::vector<float> images[4];
		EXRHeader header;
		EXRImage image;

		void createBlob(VkExtent2D imgExtent, uint32_t numChannels) 
		{
			CHECK(numChannels == 3 || numChannels == 4, "Could not save to EXR, number of channels must be 3 or 4.");
			InitEXRHeader(&header);
			InitEXRImage(&image);

			image.num_channels = numChannels;

			for (uint32_t c = 0; c < numChannels; c++)
				images[c].resize(imgExtent.width * imgExtent.height);
			
						
			image_ptr[0] = &(images[2].at(0)); // B
			image_ptr[1] = &(images[1].at(0)); // G
			image_ptr[2] = &(images[0].at(0)); // R
			if (numChannels == 4)
				image_ptr[3] = &(images[3].at(0)); // A

			image.images = (unsigned char**)image_ptr;
			image.width = static_cast<int>(imgExtent.width);
			image.height = static_cast<int>(imgExtent.height);

			header.num_channels = numChannels;
			header.channels = new EXRChannelInfo[header.num_channels];
			
			// Must be BGR(A) order, since most of EXR viewers expect this channel order.
			header.channels[0].name[0] = 'B'; header.channels[0].name[1] = '\0';
			header.channels[1].name[0] = 'G'; header.channels[1].name[1] = '\0';
			header.channels[2].name[0] = 'R'; header.channels[2].name[1] = '\0';
			if (numChannels == 4)
				header.channels[3].name[0] = 'A'; header.channels[3].name[1] = '\0';
			
			header.pixel_types = new int[header.num_channels];
			header.requested_pixel_types = new int[header.num_channels];
			for (int i = 0; i < header.num_channels; i++) {
				header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of input image
				header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of output image to be stored in .EXR
			}
		}

		void toExr(VkExtent2D imgExtent, void* mptrStagingBuffer, std::string filename)
		{	
			float* pBuffer = static_cast<float*>(mptrStagingBuffer);

			for (uint32_t i = 0; i < imgExtent.width * imgExtent.height; i++)
				for (int c = 0; c < header.num_channels; c++)
					images[c][i] = pBuffer[4 * i + c];
			
			const char* err;
			CHECK(SaveEXRImageToFile(&image, &header, filename.c_str(), &err) == TINYEXR_SUCCESS, "Filed to save as .exr" + std::string(err));
		}

		void cleanUp()
		{
			delete []header.channels;
			delete []header.pixel_types;
			delete []header.requested_pixel_types;
		}
	private:
		float* image_ptr[4];
	} exrBlob;
};