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

The ShaderBindingTable is a helper to construct the SBT. It helps to maintain the
proper offsets of each element, required when constructing the SBT, but also when filling the
dispatch rays description.

*/

#include "generator.h"
#include <algorithm>
#include <stdexcept>

//--------------------------------------------------------------------------------------------------
//
// Add a ray generation program by group index, with its list of offsets or values
void ShaderBindingTableGenerator::addRayGenerationProgram(
    uint32_t                          groupIndex,
    const std::vector<unsigned char>& inlineData)
{
  m_rayGen.emplace_back(SBTEntry(groupIndex, inlineData));
}

//--------------------------------------------------------------------------------------------------
//
// Add a miss program by group index, with its list of offsets or values
void ShaderBindingTableGenerator::addMissProgram(uint32_t                          groupIndex,
                                                 const std::vector<unsigned char>& inlineData)
{
  m_miss.emplace_back(SBTEntry(groupIndex, inlineData));
}

//--------------------------------------------------------------------------------------------------
//
// Add a hit group by group index, with its list of offsets or values
void ShaderBindingTableGenerator::addHitGroup(uint32_t                          groupIndex,
                                              const std::vector<unsigned char>& inlineData)
{
  m_hitGroup.emplace_back(SBTEntry(groupIndex, inlineData));
}

//--------------------------------------------------------------------------------------------------
//
// Compute the size of the SBT based on the set of programs and hit groups it contains
VkDeviceSize ShaderBindingTableGenerator::computeSBTSize(
    const VkPhysicalDeviceRayTracingPropertiesNV& props)
{
  // Size of a program identifier
  m_progIdSize = props.shaderGroupHandleSize;
  m_shaderGroupAlignment = props.shaderGroupBaseAlignment;
 
  // Compute the entry size of each program type depending on the maximum number of parameters in
  // each category
  m_rayGenEntrySize   = getEntrySize(m_rayGen);
  m_missEntrySize     = getEntrySize(m_miss);
  m_hitGroupEntrySize = getEntrySize(m_hitGroup);

  // The total SBT size is the sum of the entries for ray generation, miss and hit groups
  m_sbtSize = m_rayGenEntrySize * static_cast<VkDeviceSize>(m_rayGen.size())
              + m_missEntrySize * static_cast<VkDeviceSize>(m_miss.size())
              + m_hitGroupEntrySize * static_cast<VkDeviceSize>(m_hitGroup.size());
  return m_sbtSize;
}

//--------------------------------------------------------------------------------------------------
//
// Build the SBT and store it into sbtBuffer, which has to be pre-allocated in zero-copy memory.
// Access to the raytracing pipeline object is required to fetch program identifiers
void ShaderBindingTableGenerator::populateSBT(const VkDevice &device, const VkPipeline &raytracingPipeline, const VmaAllocator &allocator, const VmaAllocation &sbtBufferAllocation)
{

	uint32_t groupCount = static_cast<uint32_t>(m_rayGen.size())
                        + static_cast<uint32_t>(m_miss.size())
                        + static_cast<uint32_t>(m_hitGroup.size());

	// Fetch all the shader handles used in the pipeline, so that they can be written in the SBT
	// Note that this could be also done by fetching the handles one by one when writing the SBT entries
	auto shaderHandleStorage = new uint8_t[groupCount * m_progIdSize];
	PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesNV>(vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesNV"));
	VK_CHECK(vkGetRayTracingShaderGroupHandlesNV(device, raytracingPipeline, 0, groupCount, m_progIdSize * groupCount, shaderHandleStorage),
		"SbtGenerator: SBT failed to get shader group handles");

	// Map the SBT
	void* vData;
	VK_CHECK(vmaMapMemory(allocator, sbtBufferAllocation, &vData),
		"SbtGenerator: vkMapMemory failed");
  
	auto* data = static_cast<uint8_t*>(vData);

	data += copyShaderData(data, m_rayGen, m_rayGenEntrySize, shaderHandleStorage);
	data += copyShaderData(data, m_miss, m_missEntrySize, shaderHandleStorage);
	copyShaderData(data, m_hitGroup, m_hitGroupEntrySize, shaderHandleStorage);

	// Unmap the SBT
	vmaUnmapMemory(allocator, sbtBufferAllocation);
 
	delete []shaderHandleStorage;
}

//--------------------------------------------------------------------------------------------------
//
// Reset the sets of programs and hit groups
void ShaderBindingTableGenerator::reset()
{
  m_rayGen.clear();
  m_miss.clear();
  m_hitGroup.clear();

  m_rayGenEntrySize   = 0;
  m_missEntrySize     = 0;
  m_hitGroupEntrySize = 0;
  m_progIdSize        = 0;
}

//--------------------------------------------------------------------------------------------------
// The following getters are used to simplify the call to DispatchRays where the offsets of the
// shader programs must be exactly following the SBT layout

//--------------------------------------------------------------------------------------------------
//
// Get the size in bytes of the SBT section dedicated to ray generation programs
VkDeviceSize ShaderBindingTableGenerator::getRayGenSectionSize() const
{
  return m_rayGenEntrySize * static_cast<VkDeviceSize>(m_rayGen.size());
}

//--------------------------------------------------------------------------------------------------
//
// Get the size in bytes of one ray generation program entry in the SBT
VkDeviceSize ShaderBindingTableGenerator::getRayGenEntrySize() const
{
  return m_rayGenEntrySize;
}

VkDeviceSize ShaderBindingTableGenerator::getRayGenOffset() const
{
  return 0;
}

//--------------------------------------------------------------------------------------------------
//
// Get the size in bytes of the SBT section dedicated to miss programs
VkDeviceSize ShaderBindingTableGenerator::getMissSectionSize() const
{
  return m_missEntrySize * static_cast<VkDeviceSize>(m_miss.size());
}

//--------------------------------------------------------------------------------------------------
//
// Get the size in bytes of one miss program entry in the SBT
VkDeviceSize ShaderBindingTableGenerator::getMissEntrySize()
{
  return m_missEntrySize;
}

VkDeviceSize ShaderBindingTableGenerator::getMissOffset() const
{
  // Miss is right after raygen
  return getRayGenSectionSize();
}

//--------------------------------------------------------------------------------------------------
//
// Get the size in bytes of the SBT section dedicated to hit groups
VkDeviceSize ShaderBindingTableGenerator::getHitGroupSectionSize() const
{
  return m_hitGroupEntrySize * static_cast<VkDeviceSize>(m_hitGroup.size());
}

//--------------------------------------------------------------------------------------------------
//
// Get the size in bytes of one hit group entry in the SBT
VkDeviceSize ShaderBindingTableGenerator::getHitGroupEntrySize() const
{
  return m_hitGroupEntrySize;
}

VkDeviceSize ShaderBindingTableGenerator::getHitGroupOffset() const
{
  // hit groups are after raygen and miss
  return getRayGenSectionSize() + getMissSectionSize();
}

//--------------------------------------------------------------------------------------------------
//
// For each entry, copy the shader identifier followed by its resource pointers and/or root
// constants in outputData, with a stride in bytes of entrySize, and returns the size in bytes
// actually written to outputData.
VkDeviceSize ShaderBindingTableGenerator::copyShaderData(uint8_t* outputData, const std::vector<SBTEntry>& shaders, VkDeviceSize entrySize, const uint8_t* shaderHandleStorage)
{
  uint8_t* pData = outputData;
  for(const auto& shader : shaders)  {
    // Copy the shader identifier that was previously obtained with
    // vkGetRayTracingShaderGroupHandlesNV
    memcpy(pData, shaderHandleStorage + shader.m_groupIndex * m_progIdSize, m_progIdSize);

    // Copy all its resources pointers or values in bulk
    if(!shader.m_inlineData.empty())
    {
      memcpy(pData + m_progIdSize, shader.m_inlineData.data(), shader.m_inlineData.size());
    }

    pData += entrySize;
  }
  // Return the number of bytes actually written to the output buffer
  return static_cast<uint32_t>(shaders.size()) * entrySize;
}

//--------------------------------------------------------------------------------------------------
//
// Compute the size of the SBT entries for a set of entries, which is determined by their maximum
// number of parameters
VkDeviceSize ShaderBindingTableGenerator::getEntrySize(const std::vector<SBTEntry>& entries)
{
  // Find the maximum number of parameters used by a single entry
  size_t maxArgs = 0;
  for(const auto& shader : entries)
  {
    maxArgs = std::max(maxArgs, shader.m_inlineData.size());
  }
  // A SBT entry is made of a program ID and a set of 4-byte parameters (offsets or push constants)
  VkDeviceSize entrySize = m_progIdSize + static_cast<VkDeviceSize>(maxArgs);

  // The entries of the shader binding table must be a multiple of shaderGroupAlignment
  entrySize = ROUND_UP(entrySize, m_shaderGroupAlignment);

  return entrySize;
}

//--------------------------------------------------------------------------------------------------
//
//
ShaderBindingTableGenerator::SBTEntry::SBTEntry(uint32_t             groupIndex,
                                                std::vector<uint8_t> inlineData)
    : m_groupIndex(groupIndex)
    , m_inlineData(std::move(inlineData))
{
}

