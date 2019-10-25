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

class PipelineGenerator
{
protected:
	/// Shader stages contained in the pipeline
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
	std::vector<VkShaderModule> m_shaderModules;
	
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

		m_shaderStages.emplace_back(stageCreate);
		m_shaderModules.push_back(shaderModule);
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
		for (auto& sm : m_shaderModules)
			vkDestroyShaderModule(device, sm, nullptr);

		m_shaderModules.clear();
		m_shaderStages.clear();
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
  void createPipeline(const VkDevice &device, VkDescriptorSetLayout descriptorSetLayout, VkPipeline *pipeline, VkPipelineLayout *layout);

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
