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

/*
Contacts for feedback:
- pgautron@nvidia.com (Pascal Gautron)
- mlefrancois@nvidia.com (Martin-Karl Lefrancois)

The raytracing pipeline combines the raytracing shaders into a state object,
that can be thought of as an executable GPU program. For that, it requires the
shaders compiled as DXIL libraries, where each library exports symbols in a way
similar to DLLs. Those symbols are then used to refer to these shaders libraries
when creating hit groups, associating the shaders to their root signatures and
declaring the steps of the pipeline. All the calls to this helper class can be
done in arbitrary order. Some basic sanity checks are also performed when
compiling in debug mode.

Simple usage of this class:

pipeline.AddLibrary(m_rayGenLibrary.Get(), {L"RayGen"});
pipeline.AddLibrary(m_missLibrary.Get(), {L"Miss"});
pipeline.AddLibrary(m_hitLibrary.Get(), {L"ClosestHit"});

pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");



pipeline.SetMaxRecursionDepth(1);

rtStateObject = pipeline.Generate();

*/

#pragma once
#include "vulkan/vulkan.h"
#include "helper.h"
#include <string>
#include <vector>

class DescriptorSetGenerator {
public:
	void bindBuffer(VkDescriptorSetLayoutBinding layout, VkDescriptorBufferInfo bufferInfo) {
		VkDescriptorImageInfo imageInfo = {};
		VkWriteDescriptorSetAccelerationStructureNV tlasInfo = {};
		bindings.push_back(layout);
		descriptorTypeInfo.push_back({ bufferInfo, imageInfo, tlasInfo, TYPE_BUFFER });
	}

	void bindImage(VkDescriptorSetLayoutBinding layout, VkDescriptorImageInfo imageInfo) {
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

	void generateDescriptorSet(const VkDevice &device, VkDescriptorSetLayout* layout, VkDescriptorPool* descriptorPool, VkDescriptorSet* descriptorSets, uint32_t maxSets = 1) {
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
protected:
	/// Shader stages contained in the pipeline
	std::vector<VkPipelineShaderStageCreateInfo> shaderStageCIs;
	std::vector<VkShaderModule> shaderModules;

	void createPipelineLayout(const VkDevice& device, const VkDescriptorSetLayout& descriptorSetLayout, VkPipelineLayout *pipelineLayout) {
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

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

	void addDepthStencilState(VkBool32 depthWriteEnable = VK_TRUE)
	{
		depthStencilStateCI.depthWriteEnable = depthWriteEnable;
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
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		addInputAssemblyState();

		viewport.x = 0.0f;
		viewport.y = 0.0f;
		scissor.offset = { 0, 0 };
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;
		
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.depthClampEnable = VK_FALSE;
		rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.lineWidth = 1.0f;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.depthBiasEnable = VK_FALSE;
		addRasterizationState();

		msaaStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msaaStateCI.sampleShadingEnable = VK_FALSE;
		addMsaaSate();

		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_TRUE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
		depthStencilStateCI.stencilTestEnable = VK_FALSE;
		addDepthStencilState();
				
		colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachmentState.blendEnable = VK_FALSE;

		colorBlendAttachmentStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendAttachmentStateCI.logicOpEnable = VK_FALSE;
		colorBlendAttachmentStateCI.logicOp = VK_LOGIC_OP_COPY;
		colorBlendAttachmentStateCI.attachmentCount = 1;
		colorBlendAttachmentStateCI.pAttachments = &colorBlendAttachmentState;
		colorBlendAttachmentStateCI.blendConstants[0] = 0.0f;
		colorBlendAttachmentStateCI.blendConstants[1] = 0.0f;
		colorBlendAttachmentStateCI.blendConstants[2] = 0.0f;
		colorBlendAttachmentStateCI.blendConstants[3] = 0.0f;
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

	VkPipelineColorBlendAttachmentState colorBlendAttachmentState;
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