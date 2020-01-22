#pragma once

#include "generator.h"
#include "helper.h"
#include "../shaders/Filters/crossBilateralFilterParam.h"

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
	
	void cbfWidget()
	{	
		if (ImGui::CollapsingHeader("CrossBilateralFilter")) {
			ImGui::Text("Mode:"); ImGui::SameLine();
			ImGui::RadioButton("Cross", &pcb.mode, FILTER_MODE_CROSS); ImGui::SameLine();
			ImGui::RadioButton("Bilateral", &pcb.mode, FILTER_MODE_BILATERAL); ImGui::SameLine();
			ImGui::RadioButton("Unilateral", &pcb.mode, FILTER_MODE_UNILATERAL);
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
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/temporalFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D& screenExtent, const uint32_t filterSize = 9)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		// update push constant block
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
		vkCmdDispatch(cmdBuf, 1 + (screenExtent.width - 1) / 16, 1 + (screenExtent.height - 1) / 16, 1);
	}

	void cleanUp(const VkDevice& device)
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}

	void temporalFilterWidget()
	{
		if (ImGui::CollapsingHeader("TemporalFilter")) {
			
		}
	}

	TemporalFilter()
	{
		pcb.frameIndex = 0;
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
	} pcb;
};