#include "../../generator.h"
#include "../../helper.h"

class SubSamplePass
{
public:
	void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inDiffuseCol, const VkImageView& inSpecularCol, const VkImageView& inNormal, const VkImageView& inOtherParam)
	{
		CHECK_DBG_ONLY(buffersUpdated, "TemporalWindowFilter : call createBuffers first.");

		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inDiffuseCol,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inSpecularCol,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inOtherParam,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outDiffuseColView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outSpecularColView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outNormalView,  VK_IMAGE_LAYOUT_GENERAL });
		descGen.bindImage({ 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outOtherParamView,  VK_IMAGE_LAYOUT_GENERAL });
		

		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		//filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
		filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/subSample.spv");
		filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, 
		VkImageView& _outDiffuseCol, VkImageView& _outSpecularCol, VkImageView& _outNormal, VkImageView& _outOtherParam)
	{
		auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool, 
			&allocator = allocator, &extent = extent](VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
		{
			createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT, format);
			imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
			transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
		};
		
		makeImage(VK_FORMAT_R8G8B8A8_UNORM, outDiffuseCol, outDiffuseColView, outDiffuseColAlloc);
		makeImage(VK_FORMAT_R8G8B8A8_UNORM, outSpecularCol, outSpecularColView, outSpecularColAlloc);
		makeImage(VK_FORMAT_R32G32B32A32_SFLOAT, outNormal, outNormalView, outNormalAlloc);
		makeImage(VK_FORMAT_R32G32B32A32_SFLOAT, outOtherParam, outOtherParamView, outOtherParamAlloc);

		_outDiffuseCol = outDiffuseColView;
		_outSpecularCol = outSpecularColView;
		_outNormal = outNormalView;
		_outOtherParam = outOtherParamView;

		
		outImageSize = extent;

		buffersUpdated = true;
	}

	void cmdDispatch(const VkCommandBuffer& cmdBuf)
	{
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
		vkCmdDispatch(cmdBuf, 1 + (outImageSize.width - 1) / 4, 1 + (outImageSize.height - 1) / 4, 1);
	}

	void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
	{	
		auto destroyImage = [&device = device, &allocator = allocator](VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
		{
			vkDestroyImageView(device, imageView, nullptr);
			vmaDestroyImage(allocator, image, allocation);
		};
		
		destroyImage(outDiffuseCol, outDiffuseColView, outDiffuseColAlloc);
		destroyImage(outSpecularCol, outSpecularColView, outSpecularColAlloc);
		destroyImage(outNormal, outNormalView, outNormalAlloc);
		destroyImage(outOtherParam, outOtherParamView, outOtherParamAlloc);

		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);

		buffersUpdated = false;
	}

	void widget()
	{	
		
	}

	SubSamplePass()
	{
		buffersUpdated = false;
	}
private:
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("SubSamplePass");

	DescriptorSetGenerator descGen = DescriptorSetGenerator("SubSamplePass");
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	VkImage outDiffuseCol;
	VkImageView outDiffuseColView;
	VmaAllocation outDiffuseColAlloc;
	
	VkImage outSpecularCol;
	VkImageView outSpecularColView;
	VmaAllocation outSpecularColAlloc;

	VkImage outNormal;
	VkImageView outNormalView;
	VmaAllocation outNormalAlloc;

	VkImage outOtherParam;
	VkImageView outOtherParamView;
	VmaAllocation outOtherParamAlloc;

	VkExtent2D outImageSize;

	bool buffersUpdated;
};
