#include "../../generator.h"
#include "../../helper.h"

namespace RtxFiltering_2
{
	class SubSamplePass
	{
	public:
		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inNormal, const VkImageView& inOther)
		{
			CHECK_DBG_ONLY(buffersUpdated, "SubSamplePass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inOther,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outNormalHalfView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outOtherHalfView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outNormalQuatView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outOtherQuatView,  VK_IMAGE_LAYOUT_GENERAL });


			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			//filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/subSample.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
		}

		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, const VkFormat normalFmt, const VkFormat otherFmt,
			VkImageView& _outNormalHalf, VkImageView& _outOtherHalf, VkImageView& _outNormalQuat, VkImageView& _outOtherQuat)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](const VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage({ extent.width / 2, extent.height / 2 }, normalFmt, outNormalHalfImg, outNormalHalfView, outNormalHalfAlloc);
			makeImage({ extent.width / 2, extent.height / 2 }, otherFmt, outOtherHalfImg, outOtherHalfView, outOtherHalfAlloc);
			makeImage({ extent.width / 4, extent.height / 4 }, normalFmt, outNormalQuatImg, outNormalQuatView, outNormalQuatAlloc);
			makeImage({ extent.width / 4, extent.height / 4 }, otherFmt, outOtherQuatImg, outOtherQuatView, outOtherQuatAlloc);

			_outNormalHalf = outNormalHalfView;
			_outOtherHalf = outOtherHalfView;
			_outNormalQuat = outNormalQuatView;
			_outOtherQuat = outOtherQuatView;

			globalWorkDim = extent;

			buffersUpdated = true;
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdDispatch(cmdBuf, 1 + (globalWorkDim.width - 1) / 8, 1 + (globalWorkDim.height - 1) / 8, 1);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			auto destroyImage = [&device = device, &allocator = allocator](VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				vkDestroyImageView(device, imageView, nullptr);
				vmaDestroyImage(allocator, image, allocation);
			};

			destroyImage(outNormalHalfImg, outNormalHalfView, outNormalHalfAlloc);
			destroyImage(outOtherHalfImg, outOtherHalfView, outOtherHalfAlloc);
			destroyImage(outNormalQuatImg, outNormalQuatView, outNormalQuatAlloc);
			destroyImage(outOtherQuatImg, outOtherQuatView, outOtherQuatAlloc);

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

		VkImage outNormalHalfImg;
		VkImageView outNormalHalfView;
		VmaAllocation outNormalHalfAlloc;

		VkImage outOtherHalfImg;
		VkImageView outOtherHalfView;
		VmaAllocation outOtherHalfAlloc;

		VkImage outNormalQuatImg;
		VkImageView outNormalQuatView;
		VmaAllocation outNormalQuatAlloc;

		VkImage outOtherQuatImg;
		VkImageView outOtherQuatView;
		VmaAllocation outOtherQuatAlloc;

		VkExtent2D globalWorkDim;

		bool buffersUpdated;
	};
}