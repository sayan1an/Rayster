/*-----------------------------------------------------------------------
Copyright (c) 2014-2018, NVIDIA. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Neither the name of its contributors may be used to endorse
or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

// Copyright(c) 2019, Sayantan Datta @ sayantan.d.one@gmail.com.

#pragma once
#include "vulkan/vulkan.h"
#include "helper.h"
#include <string>
#include <vector>
#include <map>

class FboManager
{
public:
	void addDepthAttachment(std::string name, VkFormat format, VkSampleCountFlagBits sample,
		const VkImageView* view, uint32_t count = 1, VkClearDepthStencilValue clearDepth = { 1.0f, 0 })
	{
		VkClearValue v = {};
		v.depthStencil = clearDepth;

		addAttachment(name, format, sample, view, count, v);
	}

	void addColorAttachment(std::string name, VkFormat format, VkSampleCountFlagBits sample,
		const VkImageView* view, uint32_t count = 1, VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f })
	{
		VkClearValue v = {};
		v.color = clearColor;

		addAttachment(name, format, sample, view, count, v);
	}

	VkAttachmentReference getAttachmentReference(std::string name, VkImageLayout layout)
	{	
		if (attachments.find(name) == attachments.end())
			throw std::runtime_error("Attachment name - " + name + " not found");

		VkAttachmentReference ref = {};
		const FboData data = attachments[name];
		
		ref.attachment = data.index;
		ref.layout = layout;

		return ref;
	}

	VkFormat getFormat(std::string name)
	{
		if (attachments.find(name) == attachments.end())
			throw std::runtime_error("Attachment name - " + name + " not found");
		
		return attachments[name].format;
	}

	VkSampleCountFlagBits getSampleCount(std::string name)
	{
		if (attachments.find(name) == attachments.end())
			throw std::runtime_error("Attachment name - " + name + " not found");

		return attachments[name].samples;
	}

	void updateAttachmentDescription(std::string name, VkAttachmentDescription description)
	{	
		if (attachments.find(name) == attachments.end())
			throw std::runtime_error("Attachment name - " + name + " not found");
		
		FboData data = attachments[name];
		description.format = data.format;
		description.samples = data.samples;
		
		auto iter = attachmentDescriptions.insert(std::make_pair(name, description));

		if (iter.second == false)
			attachmentDescriptions[name] = description;
	}

	void getAttachmentDescriptions(std::vector<VkAttachmentDescription> &_attachmentDescriptions) 
	{
		if (attachmentDescriptions.size() != attachments.size())
			throw std::runtime_error("One or more of the attachment descriptions has not been updated");

		_attachmentDescriptions.resize(attachments.size());
		for (const auto& attachment : attachments) {
			if (attachmentDescriptions.find(attachment.first) == attachmentDescriptions.end())
				throw std::runtime_error("This should not be happening, something wrong with the logic; attchemnt name not found");

			_attachmentDescriptions[attachment.second.index] = attachmentDescriptions[attachment.first];
		}
	}

	void getAttachments(std::vector<VkImageView>& _attachmentImageViews, uint32_t index)
	{
		_attachmentImageViews.resize(attachments.size());

		for (const auto& attachment : attachments) {
			uint32_t count = attachment.second.count;
			
			_attachmentImageViews[attachment.second.index] = attachment.second.view[index < count ? index : count - 1];
		}
	}

	VkImageView getImageView(std::string name, uint32_t index = 0)
	{
		if (attachments.find(name) == attachments.end())
			throw std::runtime_error("Attachment name - " + name + " not found");
		
		uint32_t count = attachments[name].count;

		return attachments[name].view[index < count ? index : count - 1];
	}

	std::vector<VkClearValue>& getClearValues() 
	{
		return clearValues;
	}

private:
	struct FboData
	{	
		uint32_t index;
		VkFormat format;
		VkSampleCountFlagBits samples;
		const VkImageView *view;
		uint32_t count;
	};

	std::vector<VkClearValue> clearValues;
	std::map<std::string, VkAttachmentDescription> attachmentDescriptions;
	std::map<std::string, FboData> attachments;

	void addAttachment(std::string name, VkFormat format, VkSampleCountFlagBits sample, 
		const VkImageView* view, uint32_t count, VkClearValue clear)
	{
		uint32_t index = static_cast<uint32_t>(attachments.size());

		FboData data = { index, format, sample, view, count };
		auto iter = attachments.insert(std::make_pair(name, data));

		if (iter.second == false)
			throw std::runtime_error("Attachment already exsists");

		clearValues.push_back(clear);
	}
};

class TextureGenerator 
{
public:
	size_t addTexture(Image2d textureImage)
	{	
		textureCache.push_back(textureImage);
		return textureCache.size();
	}

	size_t size() const
	{
		return textureCache.size();
	}

	void createTexture(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage& textureImage, VkImageView &textureImageView, VkSampler &sampler, VmaAllocation& textureImageAllocation)
	{	
		fixTextureCache();
		createTextureImage(physicalDevice, device, allocator, queue, commandPool, textureImage, textureImageAllocation, textureCache[0].mipLevels());
		textureImageView = createImageView(device, textureImage, textureCache[0].format, VK_IMAGE_ASPECT_COLOR_BIT, textureCache[0].mipLevels(), static_cast<uint32_t>(textureCache.size()));
		createTextureSampler(device, sampler, textureCache[0].mipLevels());
	}
private:
	std::vector<Image2d> textureCache;
	
	void fixTextureCache()
	{
		if (textureCache.empty())
			throw std::runtime_error("TextureGenerator : Provided texture cache is empty");
		else {
			// All images must have same image format and size. We can relax this restriction to having same aspect ratio and rescaling the
			// images to the largest one.

			// throw an error when format, aspectRatio and mip levels are not same.
			// Check if the images have same aspect ratio and format. Upscale all images to the size of the largest one.
			VkFormat desiredFormat = textureCache[0].format;
			float desiredAspectRatio = (float)textureCache[0].width / textureCache[0].height;
			
			uint32_t maxWidth = 0;
			uint32_t maxHeight = 0;

			for (const auto& image : textureCache) {
				float aspectRatio = (float)image.width / image.height;

				if (image.format != desiredFormat)
					throw std::runtime_error("TextureGenerator : Format for all texture images must be same in the texture cache.");

				else if  (std::abs(aspectRatio - desiredAspectRatio) / desiredAspectRatio > 0.01)
					throw std::runtime_error("TextureGenerator : Aspect ratio for all texture images must be same in the texture cache.");

				if (image.width > maxWidth)
					maxWidth = image.width;

				if (image.height > maxHeight)
					maxHeight = image.height;
			}

			for (auto& image : textureCache)
				if (image.width != maxWidth || image.height != maxHeight)
					image.resize(maxWidth, maxHeight);

			// check size and mipLevels of all images are same
			uint32_t mipLevels = textureCache[0].mipLevels();
			for (const auto& image : textureCache) {
				if (image.width != maxWidth || image.height != maxHeight)
					throw std::runtime_error("TextureGenerator: Fix me - some problem with image resize.");
				else if (mipLevels != image.mipLevels())
					throw std::runtime_error("TextureGenerator : Mip levels for all texture images must be same in the texture cache.");
			}

		}
	}

	void createTextureImage(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkImage &textureImage, VmaAllocation &textureImageAllocation, uint32_t mipLevels)
	{
		uint32_t bufferSize = 0;
		for (const auto& image : textureCache)
			bufferSize += image.size();

		VkBuffer stagingBuffer;
		VmaAllocation stagingBufferAllocation;

		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create staging buffer for index buffer!");

		void* data;
		vmaMapMemory(allocator, stagingBufferAllocation, &data);
		uint32_t bufferOffset = 0;
		
		for (auto& image : textureCache) {
			byte* start = static_cast<byte*>(data);
			memcpy(&start[bufferOffset], image.pixels, static_cast<size_t>(image.size()));
			bufferOffset += static_cast<size_t>(image.size());
			image.cleanUp();
		}
		vmaUnmapMemory(allocator, stagingBufferAllocation);

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		bufferOffset = 0;
		for (uint32_t layer = 0; layer < textureCache.size(); layer++) {
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = textureCache[0].width;
			bufferCopyRegion.imageExtent.height = textureCache[0].height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = bufferOffset;

			bufferCopyRegions.push_back(bufferCopyRegion);

			// Increase offset into staging buffer for next level / face
			bufferOffset += textureCache[layer].size();
		}

		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = textureCache[0].width;
		imageCreateInfo.extent.height = textureCache[0].height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = static_cast<uint32_t>(textureCache.size());
		imageCreateInfo.format = textureCache[0].format;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &textureImage, &textureImageAllocation, nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create texture image!");

		transitionImageLayout(device, queue, commandPool, textureImage, textureCache[0].format,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, static_cast<uint32_t>(textureCache.size()));
		VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		endSingleTimeCommands(device, queue, commandPool, commandBuffer);
		//transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);

		generateMipmaps(physicalDevice, device, queue, commandPool, textureImage,
			textureCache[0].format, textureCache[0].width, textureCache[0].height, mipLevels, static_cast<uint32_t>(textureCache.size()));
	}
	
	void generateMipmaps(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkQueue& queue, const VkCommandPool& commandPool,
		VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels, uint32_t layerCount)
	{
		if (mipLevels == 1) {
			transitionImageLayout(device, queue, commandPool, image, imageFormat,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, layerCount);
			return;
		}

		// Check if image format supports linear blitting
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
			throw std::runtime_error("texture image format does not support linear blitting!");
		}

		VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layerCount;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < mipLevels; i++) {
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = layerCount;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = layerCount;

			vkCmdBlitImage(commandBuffer,
				image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		endSingleTimeCommands(device, queue, commandPool, commandBuffer);
	}

	void createTextureSampler(const VkDevice& device, VkSampler& sampler, uint32_t mipLevels)
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
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0;
		samplerInfo.maxLod = static_cast<float>(mipLevels);
		samplerInfo.mipLodBias = 0;
						
		if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
			throw std::runtime_error("failed to create texture sampler!");
		}
	}

};

class DescriptorSetGenerator {
public:
	void bindBuffer(VkDescriptorSetLayoutBinding layout, VkDescriptorBufferInfo bufferInfo) 
	{
		VkDescriptorImageInfo imageInfo = {};
		VkWriteDescriptorSetAccelerationStructureNV tlasInfo = {};
		bindings.push_back(layout);
		descriptorTypeInfo.push_back({ bufferInfo, imageInfo, tlasInfo, TYPE_BUFFER });
	}

	void bindImage(VkDescriptorSetLayoutBinding layout, VkDescriptorImageInfo imageInfo) 
	{
		VkDescriptorBufferInfo bufferInfo = {};
		VkWriteDescriptorSetAccelerationStructureNV tlasInfo = {};
		bindings.push_back(layout);
		descriptorTypeInfo.push_back({ bufferInfo, imageInfo, tlasInfo, TYPE_IMAGE });
	}

	void bindTLAS(VkDescriptorSetLayoutBinding layout, VkWriteDescriptorSetAccelerationStructureNV tlasInfo) {
		VkDescriptorBufferInfo bufferInfo = {};
		VkDescriptorImageInfo imageInfo = {};
		bindings.push_back(layout);
		descriptorTypeInfo.push_back({ bufferInfo, imageInfo, tlasInfo, TYPE_TLAS });
	}

	void generateDescriptorSet(const VkDevice &device, VkDescriptorSetLayout* layout, VkDescriptorPool* descriptorPool, VkDescriptorSet* descriptorSets, uint32_t maxSets = 1) 
	{
		if (descriptorTypeInfo.size() == 0)
			throw std::runtime_error("Descriptor bindings are un-initialized");

		createDescriptorSetLayout(device, layout);
		allocateDescriptorSets(device, *layout, descriptorPool, descriptorSets, maxSets);

		for (uint32_t i = 0; i < maxSets; i++)
			updateDescriptorSet(device, descriptorSets[i]);

		reset();
	}

	void reset() {
		bindings.clear();
		descriptorTypeInfo.clear();
	}

private:
	enum DESCRIPTOR_TYPE {TYPE_BUFFER, TYPE_IMAGE, TYPE_TLAS};

	struct DescriptorTypeInfo {
		VkDescriptorBufferInfo bufferInfo;
		VkDescriptorImageInfo imageInfo;
		VkWriteDescriptorSetAccelerationStructureNV tlasInfo;
		DESCRIPTOR_TYPE type;
	};
	
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<DescriptorTypeInfo> descriptorTypeInfo;

	void createDescriptorSetLayout(const VkDevice& device, VkDescriptorSetLayout* layout) {
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, layout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create descriptor set layout!");
		}
	}

	void allocateDescriptorSets(const VkDevice& device, const VkDescriptorSetLayout &layout, VkDescriptorPool *descriptorPool, VkDescriptorSet *descriptorSets, uint32_t maxSets) {
		std::vector<VkDescriptorPoolSize> poolSizes;

		for (auto& binding : bindings) {
			VkDescriptorPoolSize poolSize;
			poolSize.type = binding.descriptorType;
			poolSize.descriptorCount = maxSets;
			poolSizes.push_back(poolSize);
		}

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = maxSets;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, descriptorPool) != VK_SUCCESS) {
			throw std::runtime_error("failed to create descriptor pool!");
		}

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = *descriptorPool;
		allocInfo.descriptorSetCount = maxSets;
		allocInfo.pSetLayouts = &layout;

		if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate descriptor sets!");
		}
	}

	void updateDescriptorSet(const VkDevice &device, VkDescriptorSet &descriptorSet) {
		std::vector<VkWriteDescriptorSet> descriptorWrites;

		for (size_t i = 0; i < bindings.size(); i++) {
			VkWriteDescriptorSet write = {};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = descriptorSet;
			write.dstBinding = bindings[i].binding;
			write.dstArrayElement = 0;
			write.descriptorType = bindings[i].descriptorType;
			write.descriptorCount = bindings[i].descriptorCount;

			if (descriptorTypeInfo[i].type == TYPE_BUFFER)
				write.pBufferInfo = &descriptorTypeInfo[i].bufferInfo;
			else if (descriptorTypeInfo[i].type == TYPE_IMAGE)
				write.pImageInfo = &descriptorTypeInfo[i].imageInfo;
			else if (descriptorTypeInfo[i].type == TYPE_TLAS)
				write.pNext = &descriptorTypeInfo[i].tlasInfo;
			else
				throw std::runtime_error("Could not find descriptor set type");

			descriptorWrites.push_back(write);
		}

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
	}
};

class PipelineGenerator
{
public:
	void addPushConstantRange(const VkPushConstantRange pushConstant)
	{	
		pushConstantRanges.push_back(pushConstant);
	}
private:
	std::vector<VkPushConstantRange> pushConstantRanges;
protected:
	/// Shader stages contained in the pipeline
	std::vector<VkPipelineShaderStageCreateInfo> shaderStageCIs;
	std::vector<VkShaderModule> shaderModules;

	void createPipelineLayout(const VkDevice& device, const VkDescriptorSetLayout& descriptorSetLayout, VkPipelineLayout *pipelineLayout) 
	{	
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
		pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout!");
		}
	}
	
	void createShaderStage(const VkDevice& device, const std::string& filename, VkShaderStageFlagBits stageFlag)
	{
		auto shaderCode = readFile(filename);
		VkShaderModule shaderModule = createShaderModule(shaderCode, device);

		VkPipelineShaderStageCreateInfo stageCreate;
		stageCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageCreate.pNext = nullptr;
		stageCreate.stage = stageFlag;
		stageCreate.module = shaderModule;
		// This member has to be 'main', regardless of the actual entry point of the shader
		stageCreate.pName = "main";
		stageCreate.flags = 0;
		stageCreate.pSpecializationInfo = nullptr;

		shaderStageCIs.emplace_back(stageCreate);
		shaderModules.push_back(shaderModule);
	}

	static VkShaderModule createShaderModule(const std::vector<char>& code, const VkDevice& device) 
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
			throw std::runtime_error("failed to create shader module!");
		
		return shaderModule;
	}

	void cleanUp(const VkDevice& device) 
	{
		for (auto& sm : shaderModules)
			vkDestroyShaderModule(device, sm, nullptr);

		shaderModules.clear();
		shaderStageCIs.clear();
		pushConstantRanges.clear();
	}
};

class GraphicsPipelineGenerator : public PipelineGenerator
{
public:
	GraphicsPipelineGenerator() {
		reset();
	}
	void addVertexShaderStage(const VkDevice& device, const std::string& filename)
	{
		createShaderStage(device, filename, VK_SHADER_STAGE_VERTEX_BIT);
	}

	void addFragmentShaderStage(const VkDevice& device, const std::string& filename)
	{
		createShaderStage(device, filename, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	void addVertexInputState(const std::vector<VkVertexInputBindingDescription>& bindingDescription, const std::vector<VkVertexInputAttributeDescription>& attributeDescriptions) 
	{
		vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size());
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputStateCI.pVertexBindingDescriptions = bindingDescription.data();
		vertexInputStateCI.pVertexAttributeDescriptions = attributeDescriptions.data();
	}

	void addInputAssemblyState(VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
	{
		inputAssemblyStateCI.topology = topology;
		inputAssemblyStateCI.primitiveRestartEnable = VK_FALSE;
	}
	
	void addViewportState(const VkExtent2D& swapChainExtent, float minDepth = 0.0f, float maxDepth = 1.0f)
	{	
		viewport.width = (float)swapChainExtent.width;
		viewport.height = (float)swapChainExtent.height;
		viewport.minDepth = minDepth;
		viewport.maxDepth = maxDepth;

		scissor.extent = swapChainExtent;

		viewportStateCI.pViewports = &viewport;
		viewportStateCI.pScissors = &scissor;
	}

	void addRasterizationState(VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT)
	{
		rasterizationStateCI.cullMode = cullMode;
	}

	void addMsaaSate(VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT)
	{
		msaaStateCI.rasterizationSamples = msaaSamples;
	}

	void addDepthStencilState(VkBool32 depthTestEnable = VK_TRUE, VkBool32 depthWriteEnable = VK_TRUE)
	{	
		depthStencilStateCI.depthTestEnable = depthTestEnable;
		depthStencilStateCI.depthWriteEnable = depthWriteEnable;
	}

	void addColorBlendAttachmentState(uint32_t attachmentCount = 1, bool blendEnable = false)
	{
		colorBlendAttachmentStateCI.attachmentCount = attachmentCount;
		colorBlendAttachmentStates.clear();

		VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
		colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		
		if (blendEnable) {
			colorBlendAttachmentState.blendEnable = VK_TRUE;
			colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		else
			colorBlendAttachmentState.blendEnable = VK_FALSE;

		for (uint32_t i = 0; i < attachmentCount; i++)
			colorBlendAttachmentStates.push_back(colorBlendAttachmentState);

		colorBlendAttachmentStateCI.pAttachments = colorBlendAttachmentStates.data();
	}

	void addDynamicStates(const std::vector<VkDynamicState>& dynamicStates = std::vector<VkDynamicState>())
	{
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicStateCI.pDynamicStates = dynamicStates.data();
	}

	void createPipeline(const VkDevice& device, const VkDescriptorSetLayout& descriptorSetLayout, const VkRenderPass& renderPass, uint32_t subpassIdx, VkPipeline* pipeline, VkPipelineLayout* pipelineLayout)
	{
		createPipelineLayout(device, descriptorSetLayout, pipelineLayout);

		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStageCIs.size());
		pipelineInfo.pStages = shaderStageCIs.data();
		pipelineInfo.pVertexInputState = &vertexInputStateCI;
		pipelineInfo.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineInfo.pViewportState = &viewportStateCI;
		pipelineInfo.pRasterizationState = &rasterizationStateCI;
		pipelineInfo.pMultisampleState = &msaaStateCI;
		pipelineInfo.pDepthStencilState = &depthStencilStateCI;
		pipelineInfo.pColorBlendState = &colorBlendAttachmentStateCI;
		pipelineInfo.pDynamicState = &dynamicStateCI;
		pipelineInfo.layout = *pipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = subpassIdx;
		
		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline) != VK_SUCCESS)
			throw std::runtime_error("failed to create graphics pipeline!");
		
		cleanUp(device);
		reset();
	}

	void reset() {
		vertexInputStateCI = {};
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		
		inputAssemblyStateCI = {};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		addInputAssemblyState();

		viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		scissor = {};
		scissor.offset = { 0, 0 };
		viewportStateCI = {};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;
		
		rasterizationStateCI = {};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.depthClampEnable = VK_FALSE;
		rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.lineWidth = 1.0f;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.depthBiasEnable = VK_FALSE;
		addRasterizationState();

		msaaStateCI = {};
		msaaStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msaaStateCI.sampleShadingEnable = VK_FALSE;
		addMsaaSate();

		depthStencilStateCI = {};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
		depthStencilStateCI.stencilTestEnable = VK_FALSE;
		addDepthStencilState();
		
		colorBlendAttachmentStateCI = {};
		colorBlendAttachmentStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendAttachmentStateCI.logicOpEnable = VK_FALSE;
		colorBlendAttachmentStateCI.logicOp = VK_LOGIC_OP_COPY;
		colorBlendAttachmentStateCI.blendConstants[0] = 0.0f;
		colorBlendAttachmentStateCI.blendConstants[1] = 0.0f;
		colorBlendAttachmentStateCI.blendConstants[2] = 0.0f;
		colorBlendAttachmentStateCI.blendConstants[3] = 0.0f;
		addColorBlendAttachmentState();

		dynamicStateCI = {};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		addDynamicStates();
	}
private:
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI;
	
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI;

	VkViewport viewport;
	VkRect2D scissor;
	VkPipelineViewportStateCreateInfo viewportStateCI;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCI;

	VkPipelineMultisampleStateCreateInfo msaaStateCI;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI;

	VkPipelineDynamicStateCreateInfo dynamicStateCI;

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates;
	VkPipelineColorBlendStateCreateInfo colorBlendAttachmentStateCI;
};

class ComputePipelineGenerator : public PipelineGenerator
{
public:
	void addComputeShaderStage(const VkDevice& device, const std::string& filename)
	{
		createShaderStage(device, filename, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	void createPipeline(const VkDevice& device, const VkDescriptorSetLayout& descriptorSetLayout, VkPipeline* pipeline, VkPipelineLayout* pipelineLayout)
	{
		createPipelineLayout(device, descriptorSetLayout, pipelineLayout);

		if (shaderStageCIs.size() != 1)
			throw std::runtime_error("no compute shader stage found.");

		VkComputePipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = shaderStageCIs[0];
		pipelineInfo.layout = *pipelineLayout;

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline) != VK_SUCCESS)
			throw std::runtime_error("failed to create compute pipeline!");
		
		cleanUp(device);
	}
};

/// Helper class to create raytracing pipelines
class RayTracingPipelineGenerator : public PipelineGenerator
{
public:
  /// Start the description of a hit group, that contains at least a closest hit shader, but may
  /// also contain an intesection shader and a any-hit shader. The method outputs the index of the
  /// created hit group
  uint32_t startHitGroup();

  /// Add a hit shader stage in the current hit group, where the stage can be
  /// VK_SHADER_STAGE_ANY_HIT_BIT_NV, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, or
  /// VK_SHADER_STAGE_INTERSECTION_BIT_NV
  uint32_t addAnyHitShaderStage(const VkDevice& device, const std::string& filename);
  uint32_t addCloseHitShaderStage(const VkDevice& device, const std::string& filename);
  uint32_t addIntersectionShaderStage(const VkDevice& device, const std::string& filename);

  /// End the description of the hit group
  void endHitGroup();

  /// Add a ray generation shader stage, and return the index of the created stage
  uint32_t addRayGenShaderStage(const VkDevice& device, const std::string& filename);
  /// Add a miss shader stage, and return the index of the created stage
  uint32_t addMissShaderStage(const VkDevice& device, const std::string& filename);

  /// Upon hitting a surface, a closest hit shader can issue a new TraceRay call. This parameter
  /// indicates the maximum level of recursion. Note that this depth should be kept as low as
  /// possible, typically 2, to allow hit shaders to trace shadow rays. Recursive ray tracing
  /// algorithms must be flattened to a loop in the ray generation program for best performance.
  void setMaxRecursionDepth(uint32_t maxDepth);

  /// Compiles the raytracing state object
  void createPipeline(const VkDevice &device, const VkDescriptorSetLayout &descriptorSetLayout, VkPipeline *pipeline, VkPipelineLayout *pipelineLayout);

private:
  /// Each shader stage belongs to a group. There are 3 group types: general, triangle hit and procedural hit.
  /// The general group type (VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV) is used for raygen, miss and callable shaders.
  /// The triangle hit group type (VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV) is used for closest hit and
  /// any hit shaders, when used together with the built-in ray-triangle intersection shader.
  /// The procedural hit group type (VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV) is used for custom
  /// intersection shaders, and also groups closest hit and any hit shaders that are used together with that intersection shader.
  std::vector<VkRayTracingShaderGroupCreateInfoNV> m_shaderGroups;

  /// Index of the current hit group
  uint32_t m_currentGroupIndex = 0;

  /// True if a group description is currently started
  bool m_isHitGroupOpen = false;

  /// Maximum recursion depth, initialized to 1 to at least allow tracing primary rays
  uint32_t m_maxRecursionDepth = 1;
};

/// Helper class to create and maintain a Shader Binding Table
class ShaderBindingTableGenerator
{
public:
	/// Add a ray generation program by name, with its list of data pointers or values according to
	/// the layout of its root signature
	void addRayGenerationProgram(uint32_t groupIndex, const std::vector<unsigned char>& inlineData);

	/// Add a miss program by name, with its list of data pointers or values according to
	/// the layout of its root signature
	void addMissProgram(uint32_t groupIndex, const std::vector<unsigned char>& inlineData);

	/// Add a hit group by name, with its list of data pointers or values according to
	/// the layout of its root signature
	void addHitGroup(uint32_t groupIndex, const std::vector<unsigned char>& inlineData);

	/// Compute the size of the SBT based on the set of programs and hit groups it contains
	VkDeviceSize computeSBTSize(const VkPhysicalDeviceRayTracingPropertiesNV& props);

	/// Build the SBT and store it into sbtBuffer, which has to be pre-allocated on the upload heap.
	/// Access to the raytracing pipeline object is required to fetch program identifiers using their
	/// names
	void populateSBT(const VkDevice& device, const VkPipeline& raytracingPipeline, const VmaAllocator& allocator, const VmaAllocation& sbtBufferAllocation);

	/// Reset the sets of programs and hit groups
	void reset();

	/// The following getters are used to simplify the call to DispatchRays where the offsets of the
	/// shader programs must be exactly following the SBT layout

	/// Get the size in bytes of the SBT section dedicated to ray generation programs
	VkDeviceSize getRayGenSectionSize() const;
	/// Get the size in bytes of one ray generation program entry in the SBT
	VkDeviceSize getRayGenEntrySize() const;

	VkDeviceSize getRayGenOffset() const;

	/// Get the size in bytes of the SBT section dedicated to miss programs
	VkDeviceSize getMissSectionSize() const;
	/// Get the size in bytes of one miss program entry in the SBT
	VkDeviceSize getMissEntrySize();

	VkDeviceSize getMissOffset() const;

	/// Get the size in bytes of the SBT section dedicated to hit groups
	VkDeviceSize getHitGroupSectionSize() const;
	/// Get the size in bytes of hit group entry in the SBT
	VkDeviceSize getHitGroupEntrySize() const;

	VkDeviceSize getHitGroupOffset() const;

private:
	/// Wrapper for SBT entries, each consisting of the name of the program and a list of values,
	/// which can be either offsets or raw 32-bit constants
	struct SBTEntry
	{
		SBTEntry(uint32_t groupIndex, std::vector<unsigned char> inlineData);

		uint32_t                         m_groupIndex;
		const std::vector<unsigned char> m_inlineData;
	};

	/// For each entry, copy the shader identifier followed by its resource pointers and/or root
	/// constants in outputData, with a stride in bytes of entrySize, and returns the size in bytes
	/// actually written to outputData.
	VkDeviceSize copyShaderData(uint8_t* outputData, const std::vector<SBTEntry>& shaders, VkDeviceSize entrySize, const uint8_t* shaderHandleStorage);

	/// Compute the size of the SBT entries for a set of entries, which is determined by the maximum
	/// number of parameters of their root signature
	VkDeviceSize getEntrySize(const std::vector<SBTEntry>& entries);

	/// Ray generation shader entries
	std::vector<SBTEntry> m_rayGen;
	/// Miss shader entries
	std::vector<SBTEntry> m_miss;
	/// Hit group entries
	std::vector<SBTEntry> m_hitGroup;

	/// For each category, the size of an entry in the SBT depends on the maximum number of resources
	/// used by the shaders in that category.The helper computes those values automatically in
	/// GetEntrySize()
	VkDeviceSize m_rayGenEntrySize = 0;
	VkDeviceSize m_missEntrySize = 0;
	VkDeviceSize m_hitGroupEntrySize = 0;

	/// The program names are translated into program identifiers.The size in bytes of an identifier
	/// is provided by the device and is the same for all categories.
	VkDeviceSize m_progIdSize = 0;
	VkDeviceSize m_sbtSize = 0;
};