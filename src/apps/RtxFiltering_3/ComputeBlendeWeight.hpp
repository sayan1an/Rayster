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

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, blendeWeightImage, blendeWeightImageView, blendeWeightImageAllocation);

			_blendeWeightView = blendeWeightImageView;

			globalWorkDim = extent;
			buffersUpdated = true;
			pcb.maxN = 50.0f;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const Camera& cam, const AreaLightSources& areaSource, const VkImageView &inMcStateView,
			const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& inMotionVector)
		{
			CHECK_DBG_ONLY(buffersUpdated, "ComputeBlendeWeightPass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inOther,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inMotionVector,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, cam.getDescriptorBufferInfo());
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inMcStateView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getCdfNormDescriptorBufferInfo());
			descGen.bindBuffer({ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getEmitterIndexMapDescriptorBufferInfo());
			descGen.bindBuffer({ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.getVerticesDescriptorBufferInfo());
			descGen.bindImage({ 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , blendeWeightImageView,  VK_IMAGE_LAYOUT_GENERAL });


			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			blendPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			blendPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_3/computeBlendeWeight.spv");
			blendPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

			pcb.uniformToEmitterIndexMapSize = areaSource.dPdf.size().y;
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
				ImGui::SliderFloat("Max blende frames##BlendeWeightPass", &pcb.maxN, 0.0f, 50.0f);
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
			float maxN;
			uint32_t uniformToEmitterIndexMapSize;
		} pcb;

	};
}