#pragma once

#include "generator.h"

class CrossBilateralFilter
{
public:
	void createPipeline(const VkDevice& device, const VkImageView &inDiffuseColor, const VkImageView& inSpecularColor, 
		const VkImageView &inNormal, const VkImageView &inNoisyImage, const VkImageView &outDenoisedImage)
	{	
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inDiffuseColor,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inSpecularColor,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNoisyImage,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDenoisedImage,  VK_IMAGE_LAYOUT_GENERAL });

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(uint32_t) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/Filters/crossBilateralFilter.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D &screenExtent, const uint32_t filterSize = 9)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &filterSize);
		vkCmdDispatch(cmdBuf, screenExtent.width / 16, screenExtent.height / 16, 1);
	}

	void cleanUp(const VkDevice &device)
	{	
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}
	
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("CrossBilateralFilter");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("CrossBilateralFilter");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
};