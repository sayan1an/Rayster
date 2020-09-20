#include "../../generator.h"
#include "../../helper.h"
namespace RtxFiltering_2
{
	class GenerateStencilPass
	{
	public:
		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& inNormal, const VkImageView &inShadowFull, const VkImageView &inShadowBlur)
		{
			CHECK_DBG_ONLY(buffersUpdated, "StencilPass : call createBuffers first.");

			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inNormal,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , inShadowFull,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { texSampler , inShadowBlur,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outStencilView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outStencilView2,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , outStencilView3,  VK_IMAGE_LAYOUT_GENERAL });

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			//filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/stencilPass.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
		}

		void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, const VkExtent2D& extent, VkImageView& stencilView, VkImageView& stencilView2, VkImageView& stencilView3)
		{
			auto makeImage = [&device = device, &queue = queue, &commandPool = commandPool,
				&allocator = allocator](VkExtent2D extent, VkFormat format, VkImage& image, VkImageView& imageView, VmaAllocation& allocation)
			{
				createImage(device, allocator, queue, commandPool, image, allocation, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, format);
				imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
				transitionImageLayout(device, queue, commandPool, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 1);
			};

			makeImage({ extent.width / 2, extent.height / 2 }, VK_FORMAT_R16G16B16A16_SFLOAT, outStencil, outStencilView, outStencilAlloc);
			makeImage({ extent.width / 4, extent.height / 4 }, VK_FORMAT_R16G16B16A16_SFLOAT, outStencil2, outStencilView2, outStencilAlloc2);
			makeImage({ extent.width / 8, extent.height / 8 }, VK_FORMAT_R16G16B16A16_SFLOAT, outStencil3, outStencilView3, outStencilAlloc3);
			createTexSampler(device);

			stencilView = outStencilView;
			stencilView2 = outStencilView2;
			stencilView3 = outStencilView3;

			globalWorkDim = { extent.width / 2, extent.height / 2 };

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
			
			destroyImage(outStencil, outStencilView, outStencilAlloc);
			destroyImage(outStencil2, outStencilView2, outStencilAlloc2);
			destroyImage(outStencil3, outStencilView3, outStencilAlloc3);
			vkDestroySampler(device, texSampler, nullptr);

			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);

			buffersUpdated = false;
		}

		void widget()
		{

		}

		GenerateStencilPass()
		{
			buffersUpdated = false;
		}
	private:
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;

		ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("GenerateStencilPass");

		DescriptorSetGenerator descGen = DescriptorSetGenerator("GenerateStencilPass");
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		VkImage outStencil;
		VkImageView outStencilView;
		VmaAllocation outStencilAlloc;

		VkImage outStencil2;
		VkImageView outStencilView2;
		VmaAllocation outStencilAlloc2;

		VkImage outStencil3;
		VkImageView outStencilView3;
		VmaAllocation outStencilAlloc3;

		VkExtent2D globalWorkDim;

		bool buffersUpdated;

		VkSampler texSampler;
		void createTexSampler(const VkDevice& device)
		{
			VkSamplerCreateInfo samplerInfo = {};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = 16;
			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.minLod = 0;
			samplerInfo.maxLod = 1;
			samplerInfo.mipLodBias = 0;

			VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &texSampler),
				std::string(" RtxFiltering_2: failed to create texture sampler!"));
		}
	};

	class StencilCompositionPass
	{
	public:
		void createPipeline(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkImageView& stencilView, const VkImageView& stencilView2, const VkImageView& stencilView3)
		{
			descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , stencilView,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , stencilView2,  VK_IMAGE_LAYOUT_GENERAL });
			descGen.bindImage({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { VK_NULL_HANDLE , stencilView3,  VK_IMAGE_LAYOUT_GENERAL });

			descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

			filterPipeGen.addPushConstantRange({ VK_SHADER_STAGE_COMPUTE_BIT , 0, sizeof(PushConstantBlock) });
			filterPipeGen.addComputeShaderStage(device, ROOT + "/shaders/RtxFiltering_2/stencilCompositionPass.spv");
			filterPipeGen.createPipeline(device, descriptorSetLayout, &pipeline, &pipelineLayout);
		}

		void cmdDispatch(const VkCommandBuffer& cmdBuf, const VkExtent2D& extent)
		{
			VkExtent2D globalWorkDim = { extent.width / 2, extent.height / 2 };
			vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantBlock), &pcb);
			vkCmdDispatch(cmdBuf, 1 + (globalWorkDim.width - 1) / 4, 1 + (globalWorkDim.height - 1) / 4, 1);
		}

		void cleanUp(const VkDevice& device, const VmaAllocator& allocator)
		{
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}

		void widget()
		{
			if (ImGui::CollapsingHeader("StencilCompositionPass")) {
				float normalVar = pcb.normalVarianceLimit * 1000.0f;
				float depthVar = pcb.depthVarianceLimit * 1000.0f;
				ImGui::SliderFloat("Normal Variance Limit##StencilCompositionPass", &normalVar, 0, 10);
				ImGui::SliderFloat("Depth Variance Limit##StencilCompositionPass", &depthVar, 0, 20);
				ImGui::SliderFloat("Shadow Variance Limit##StencilCompositionPass", &pcb.shadowVarianceLimit, 0.0f, 1.0f);

				pcb.normalVarianceLimit = normalVar / 1000.f;
				pcb.depthVarianceLimit = depthVar / 1000.f;
			}
		}

		StencilCompositionPass()
		{
			pcb.normalVarianceLimit = 0.005f;
			pcb.depthVarianceLimit = 0.01f;
			pcb.shadowVarianceLimit = 0.5;
			
		}

	private:
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;

		ComputePipelineGenerator filterPipeGen = ComputePipelineGenerator("StencilCompositionPass");

		DescriptorSetGenerator descGen = DescriptorSetGenerator("StencilCompositionPass");
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet descriptorSet;

		struct PushConstantBlock {
			float normalVarianceLimit;
			float depthVarianceLimit;
			float shadowVarianceLimit;
		} pcb;
	};
}