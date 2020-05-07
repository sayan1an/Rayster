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
#include "implot.h"
#include <string>
#include <vector>
#include <map>
#include <random>
#include <chrono>
#include <algorithm>

class RandomGenerator
{
public:
	RandomGenerator(uint32_t seed = -1)
	{	
		if (seed == -1) {
			using namespace std::chrono;
			microseconds ms = duration_cast<microseconds>(system_clock::now().time_since_epoch());
			uint64_t t = ms.count();
		
			generator.seed(t & 0xffffffff);
		}
		else
			generator.seed(seed & 0xffffffff);
		
		uniformUInt32Distribution = std::uniform_int_distribution<uint32_t>();

		data = nullptr;
		allocSizeBytes = 0;
		stateMemory = VK_NULL_HANDLE;
		stateMemoryAllocation = VK_NULL_HANDLE;
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool, VkExtent2D canvasExtent)
	{	
		data = new XorShiftState[static_cast<uint64_t>(canvasExtent.width) * canvasExtent.height];
		allocSizeBytes = sizeof(XorShiftState) * canvasExtent.width * canvasExtent.height;
		
		uint32_t allocSizeInUnit32 = allocSizeBytes / (sizeof(uint32_t));
		for (uint32_t i = 0; i < allocSizeInUnit32; i++)
			(static_cast<uint32_t*>(data))[i] = uniformUInt32Distribution(generator);

		createBuffer(device, allocator, queue, commandPool, stateMemory, stateMemoryAllocation, allocSizeBytes, data, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	}

	void cleanUp(const VmaAllocator& allocator)
	{
		vmaDestroyBuffer(allocator, stateMemory, stateMemoryAllocation);
	}

	VkDescriptorBufferInfo getDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = stateMemory;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	uint32_t getNextUint32_t()
	{
		return uniformUInt32Distribution(generator);
	}
private:
	VkBuffer stateMemory;
	VmaAllocation stateMemoryAllocation;

	void* data;
	uint32_t allocSizeBytes;

	std::default_random_engine generator;
	std::uniform_int_distribution<uint32_t> uniformUInt32Distribution;

	struct XorShiftState {
		uint32_t a;
	};
};

class RandomSphericalPattern
{
public:
	RandomSphericalPattern()
	{

	}

	void createBuffers(uint32_t seed, uint32_t nSamples)
	{
		RandomGenerator rGen(seed);
		randomSamplesSpherical.reserve(nSamples);

		for (uint32_t i = 0; i < nSamples; i++) {
			float theta = std::acos(1 - (2.0f * rGen.getNextUint32_t()) / 4294967295);
			float phi = (2 * PI * rGen.getNextUint32_t()) / 4294967295;
			randomSamplesSpherical.push_back(glm::vec2(theta, phi));
		}

		std::sort(randomSamplesSpherical.begin(), randomSamplesSpherical.end(), [](const glm::vec2& lhs, const glm::vec2& rhs)
		{
			if (lhs.x == rhs.x)
				return lhs.y < rhs.y;

			return lhs.x < rhs.x;
		});
	}
private:
	std::vector<glm::vec2> randomSamplesSpherical;
	const float pi = 3.14159265358979323846f;
};