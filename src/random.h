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
		maxSamples = 1024;

		sampleSphericalBuffer = VK_NULL_HANDLE;
		sampleSphericalBufferAllocation = VK_NULL_HANDLE;
		mptrSampleSphericalBuffer = nullptr;

		sampleCartesianBuffer = VK_NULL_HANDLE;
		sampleCartesianBufferAllocation = VK_NULL_HANDLE;
		mptrSampleCartesianBuffer = nullptr;

		seed = 5;
		nSamples = 32;
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
	{
		randomSamplesSpherical.reserve(maxSamples);
		randomSamplesCartesian.reserve(maxSamples);

		// an extra space to transfer the number of samples
		mptrSampleSphericalBuffer = static_cast<glm::vec2*>(createBuffer(allocator, sampleSphericalBuffer, sampleSphericalBufferAllocation, (maxSamples + 1) * sizeof(glm::vec2), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
		mptrSampleCartesianBuffer = static_cast<glm::vec3*>(createBuffer(allocator, sampleCartesianBuffer, sampleCartesianBufferAllocation, (maxSamples) * sizeof(glm::vec3), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
	}

	void updateData()
	{
		RandomGenerator rGen(seed);

		randomSamplesSpherical.clear();
		randomSamplesCartesian.clear();
		
		for (uint32_t i = 0; i < nSamples; i++) {
			float theta = std::acos(1 - 2.0f * (rGen.getNextUint32_t() / 4294967295.0f));
			float phi = (2 * PI * (rGen.getNextUint32_t()) / 4294967295.0f);

			randomSamplesSpherical.push_back(glm::vec2(theta, phi));
		}

		std::sort(randomSamplesSpherical.begin(), randomSamplesSpherical.end(), [](const glm::vec2& lhs, const glm::vec2& rhs)
		{
			if (lhs.x == rhs.x)
				return lhs.y < rhs.y;

			return lhs.x < rhs.x;
		});

		for (const auto& sample : randomSamplesSpherical)
			randomSamplesCartesian.push_back(sphericalToCartesian(glm::vec3(1, sample)));

		glm::vec2 nS(nSamples, 0);
		memcpy(mptrSampleSphericalBuffer, &nS, sizeof(glm::vec2));
		memcpy(&mptrSampleSphericalBuffer[1], randomSamplesSpherical.data(), randomSamplesSpherical.size() * sizeof(glm::vec2));
		memcpy(&mptrSampleCartesianBuffer, randomSamplesCartesian.data(), randomSamplesCartesian.size() * sizeof(glm::vec3));
	}

	void cleanUp(const VmaAllocator& allocator)
	{	
		vmaDestroyBuffer(allocator, sampleSphericalBuffer, sampleSphericalBufferAllocation);
		vmaDestroyBuffer(allocator, sampleCartesianBuffer, sampleCartesianBufferAllocation);
	}

	void widget() const
	{
		if (ImGui::CollapsingHeader("RandomPattern")) {
			ImGui::SetNextPlotRange(0, 2 * PI, 0, PI, ImGuiCond_Always);
			if (ImGui::BeginPlot("Scatter Plot", NULL, NULL)) {
				ImGui::PushPlotStyleVar(ImPlotStyleVar_LineWeight, 0);
				ImGui::PushPlotStyleVar(ImPlotStyleVar_Marker, ImMarker_Cross);
				ImGui::PushPlotStyleVar(ImPlotStyleVar_MarkerSize, 3);
				auto getter = [](const void* data, int idx) {
					glm::vec2 d = static_cast<const glm::vec2*>(data)[idx];
					return ImVec2(d.y, d.x);
				};
				ImGui::Plot("Samples", static_cast<ImVec2 (*)(const void*, int)>(getter), static_cast<const void*>(randomSamplesSpherical.data()), static_cast<int>(randomSamplesSpherical.size()), 0);
				ImGui::PopPlotStyleVar(2);
				ImGui::EndPlot();
			}
		}
	}
private:
	VkDeviceSize maxSamples;
	VkDeviceSize nSamples;
	uint32_t seed;

	std::vector<glm::vec2> randomSamplesSpherical;
	VkBuffer sampleSphericalBuffer;
	VmaAllocation sampleSphericalBufferAllocation;
	glm::vec2* mptrSampleSphericalBuffer;

	std::vector<glm::vec3> randomSamplesCartesian;
	VkBuffer sampleCartesianBuffer;
	VmaAllocation sampleCartesianBufferAllocation;
	glm::vec3* mptrSampleCartesianBuffer;
};