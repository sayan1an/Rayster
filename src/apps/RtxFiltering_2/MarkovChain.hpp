#include "../../generator.h"
#include "../../helper.h"
#include "../../lightSources.h"

namespace RtxFiltering_2
{
	class MarkovChainNoVisibility
	{
	public:
		// Assume extent is twice the size of stencil
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const uint32_t level, const VkExtent2D& extent, VkImageView& _mcmcView)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, mcmc, mcmcView, mcmcAlloc);
			_mcmcView = mcmcView;

			globalWorkDim = extent;
			pcb.level = level;
			buffersUpdated = true;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const AreaLightSources& areaSource, const RandomGenerator& randGen, const VkImageView& outMcState, const VkImageView& inNormal, const VkImageView& inOther, const VkImageView& inStencil)
		{
			CHECK_DBG_ONLY(buffersUpdated, "MarkovChainNoVisibilityPass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inOther,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inStencil,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindBuffer({ 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, randGen.getDescriptorBufferInfo());
			descGen.bindBuffer({ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.getVerticesDescriptorBufferInfo());
			descGen.bindBuffer({ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getCdfDescriptorBufferInfo());
			descGen.bindBuffer({ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getCdfNormDescriptorBufferInfo());
			descGen.bindBuffer({ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,  VK_SHADER_STAGE_COMPUTE_BIT }, areaSource.dPdf.getEmitterIndexMapDescriptorBufferInfo());
			descGen.bindImage({ 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , mcmcView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outMcState,  VK_IMAGE_LAYOUT_GENERAL });

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/mcNoVis.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);

			pcb.discretePdfSize = areaSource.dPdf.size().x;
			pcb.uniformToEmitterIndexMapSize = areaSource.dPdf.size().y;
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, mcmcView, nullptr);
			vmaDestroyImage(allocator, mcmc, mcmcAlloc);

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

		VkImage mcmc;
		VkImageView mcmcView;
		VmaAllocation mcmcAlloc;

		struct PushConstantBlock {
			uint32_t level;
			uint32_t discretePdfSize;
			uint32_t uniformToEmitterIndexMapSize;
		} pcb;
	};

	class MarkovChainNoVisibilityCombined
	{
	public:
		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& _mcStateView, VkImageView& _mcmcView1, VkImageView& _mcmcView2, VkImageView& _mcmcView3)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImageP(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage(extent, VK_FORMAT_R32G32_SFLOAT, mcState, mcStateView, mcStateAlloc);
			_mcStateView = mcStateView;

			mcPass1.createBuffers(device, allocator, queue, commandPool, 0, extent, _mcmcView1);
			mcPass2.createBuffers(device, allocator, queue, commandPool, 1, { extent.width / 2, extent.height / 2 }, _mcmcView2);
			mcPass3.createBuffers(device, allocator, queue, commandPool, 2, { extent.width / 4, extent.height / 4 }, _mcmcView3);
			
			buffersUpdated = true;
		}

		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const AreaLightSources &areaSource, const RandomGenerator& randGen,
			const VkImageView& inNormal1, const VkImageView& inOther1, const VkImageView& inStencil1,
			const VkImageView& inNormal2, const VkImageView& inOther2, const VkImageView& inStencil2, 
			const VkImageView& inNormal3, const VkImageView& inOther3, const VkImageView& inStencil3)
		{
			CHECK_DBG_ONLY(buffersUpdated, "MarkovChainNoVisibilityCombined : call createBuffers first.");

			mcPass1.createPipeline(physicalDevice, device, areaSource, randGen, mcStateView, inNormal1, inOther1, inStencil1);
			mcPass2.createPipeline(physicalDevice, device, areaSource, randGen, mcStateView, inNormal2, inOther2, inStencil2);
			mcPass3.createPipeline(physicalDevice, device, areaSource, randGen, mcStateView, inNormal3, inOther3, inStencil3);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf)
		{	
			mcPass1.cmdDispatch(cmdBuf);
			mcPass2.cmdDispatch(cmdBuf);
			mcPass3.cmdDispatch(cmdBuf);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyImageView(device, mcStateView, nullptr);
			vmaDestroyImage(allocator, mcState, mcStateAlloc);

			mcPass3.cleanUp(device, allocator);
			mcPass2.cleanUp(device, allocator);
			mcPass1.cleanUp(device, allocator);

			buffersUpdated = false;
		}
	private:
		bool buffersUpdated = false;

		VkImage mcState;
		VkImageView mcStateView;
		VmaAllocation mcStateAlloc;

		MarkovChainNoVisibility mcPass1;
		MarkovChainNoVisibility mcPass2;
		MarkovChainNoVisibility mcPass3;
	};

}