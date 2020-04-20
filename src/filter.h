#pragma once

#include "generator.h"
#include "helper.h"
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

class TemporalFrequencyFilter
{
public:
	void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inNoisyImage, const VkImageView& outDenoisedImage)
	{	
		CHECK_DBG_ONLY(buffersUpdated, "TemporalFrequencyFilter : call createBuffers first.");

		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , accumImageView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , dftImageView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

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
		createImageP(device, allocator, queue, commandPool, accumImage, accumImageAllocation, screenExtent, VK_IMAGE_USAGE_STORAGE_BIT, imageFormat, VK_SAMPLE_COUNT_1_BIT, 0, MAX_TEMPORAL_FREQ_FILT_LAYERS);
		accumImageView = createImageView(device, accumImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, MAX_TEMPORAL_FREQ_FILT_LAYERS);
		transitionImageLayout(device, queue, commandPool, accumImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, MAX_TEMPORAL_FREQ_FILT_LAYERS);

		imageFormat = VK_FORMAT_R32G32_SFLOAT;
		uint32_t dftBufferSize = (MAX_TEMPORAL_FREQ_FILT_LAYERS >> 1) + 1;
		createImageP(device, allocator, queue, commandPool, dftImage, dftImageAllocation, screenExtent, VK_IMAGE_USAGE_STORAGE_BIT, imageFormat, VK_SAMPLE_COUNT_1_BIT, 0, dftBufferSize);
		dftImageView = createImageView(device, dftImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, dftBufferSize);
		transitionImageLayout(device, queue, commandPool, dftImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 1, dftBufferSize);

		buffersUpdated = true;
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
	{	
		vkDestroyImageView(device, accumImageView, nullptr);
		vmaDestroyImage(allocator, accumImage, accumImageAllocation);
		vkDestroyImageView(device, dftImageView, nullptr);
		vmaDestroyImage(allocator, dftImage, dftImageAllocation);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		buffersUpdated = false;
	}

	void widget()
	{
		if (ImGui::CollapsingHeader("TemporalFrequencyFilter")) {
			int tSamples = pcb.temporalSamples;
			ImGui::SliderInt("Temporal samples##UID_TemporalFreqFilter", &tSamples, 1, MAX_TEMPORAL_FREQ_FILT_LAYERS);
			pcb.temporalSamples = tSamples;

			int dftComponent = pcb.nDftComponent;
			ImGui::SliderInt("DFT component##UID_TemporalFreqFilter", &dftComponent, 0, (pcb.temporalSamples >> 1));
			pcb.nDftComponent = dftComponent;
		}
	}

	TemporalFrequencyFilter()
	{
		accumImage = VK_NULL_HANDLE;
		accumImageAllocation = VK_NULL_HANDLE;
		accumImageView = VK_NULL_HANDLE;

		dftImage = VK_NULL_HANDLE;
		dftImageAllocation = VK_NULL_HANDLE;
		dftImageView = VK_NULL_HANDLE;

		buffersUpdated = false;

		pcb.frameIndex = 0;
		pcb.temporalSamples = MAX_TEMPORAL_FREQ_FILT_LAYERS;
		pcb.nDftComponent = 0;
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

	VkImage dftImage;
	VmaAllocation dftImageAllocation;
	VkImageView dftImageView;

	bool buffersUpdated;

	struct PushConstantBlock
	{
		uint32_t frameIndex;
		uint32_t temporalSamples;
		uint32_t nDftComponent;
	} pcb;

};