#include "../../generator.h"
#include "../../helper.h"
#include "../../lightSources.h"
#include "../../../shaders/RtxFiltering_3/hostDeviceShared.h"

namespace RtxFiltering_3
{
	class ComputeBlendeWeightPass
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& _blendeWeightView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage(extent, VK_FORMAT_R16G16_SFLOAT, blendeWeightImage, blendeWeightImageView, blendeWeightImageAllocation);

			_blendeWeightView = blendeWeightImageView;

			globalWorkDim = extent;
			buffersUpdated = true;
			pcb.weight = 0.1f;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const AreaLightSources& areaSource, 
			const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& inMotionVector)
		{
			CHECK_DBG_ONLY(buffersUpdated, "ComputeBlendeWeightPass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inOther,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inMotionVector,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.getVerticesDescriptorBufferInfo());
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , blendeWeightImageView,  VK_IMAGE_LAYOUT_GENERAL });


			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			blendPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			blendPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_3/computeBlendeWeight.spv");
			blendPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{	
			vkDestroyImageView(device, blendeWeightImageView, nullptr);
			vmaDestroyImage(allocator, blendeWeightImage, blendeWeightImageAllocation);

			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);

			buffersUpdated = false;
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
			vkCmdDispatch(cmdBuf, 1 + (globalWorkDim.width - 1) / COMPUTE_BLENDE_WEIGHT_WORKGROUP_SIZE, 1 + (globalWorkDim.height - 1) / COMPUTE_BLENDE_WEIGHT_WORKGROUP_SIZE, 1);
		}

		void widget()
		{
			if (ImGui::CollapsingHeader("BlendeWeightPass")) {
				ImGui::SliderFloat("BlendeWeight##BlendeWeightPass", &pcb.weight, 0.0f, 1.0f);
			}
		}
	private:
		VkExtent2D globalWorkDim;
		bool buffersUpdated = false;
		
		DescriptorSetGenerator descGen = DescriptorSetGenerator("ComputeBlendeWeightPass");
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		ComputePipelineGenerator blendPipeGen = ComputePipelineGenerator("ComputeBlendeWeightPass");
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;

		VkImage blendeWeightImage;
		VkImageView blendeWeightImageView;
		VmaAllocation blendeWeightImageAllocation;

		struct PushConstantBlock {
			float weight;
		} pcb;

	};
}