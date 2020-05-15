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
		minSamples = 4;
		sampleSphericalBuffer = VK_NULL_HANDLE;
		sampleSphericalBufferAllocation = VK_NULL_HANDLE;
		mptrSampleSphericalBuffer = nullptr;

		sampleCartesianBuffer = VK_NULL_HANDLE;
		sampleCartesianBufferAllocation = VK_NULL_HANDLE;
		mptrSampleCartesianBuffer = nullptr;

		feedbackBuffer = VK_NULL_HANDLE;
		feedbackBufferAllocation = VK_NULL_HANDLE;
		mptrFeedbackBuffer = nullptr;
		ptrFeedbackBuffer = nullptr;

		seed = 5;
		nSamples = 32;

		dataUpdated = false;
		moveSampleInTime = true;
		choosePattern = 0;

		xPixelQuery = 1;
		yPixelQuery = 1;
		extent = { 10,10 };

		randomSamplesSpherical.reserve(maxSamples);
		randomSamplesCartesian.reserve(maxSamples);

		nonRaytracedSamples.reserve(maxSamples);
		raytracedSamples.reserve(maxSamples);
		intersectedSamples.reserve(maxSamples);
	}

	void createBuffers(const VkDevice& device, const VmaAllocator& allocator, const VkQueue& queue, const VkCommandPool& commandPool)
	{
		mptrSampleSphericalBuffer = static_cast<glm::vec2*>(createBuffer(allocator, sampleSphericalBuffer, sampleSphericalBufferAllocation, maxSamples * sizeof(glm::vec2), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
		mptrSampleCartesianBuffer = static_cast<glm::vec4*>(createBuffer(allocator, sampleCartesianBuffer, sampleCartesianBufferAllocation, maxSamples * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
		mptrFeedbackBuffer = createBuffer(allocator, feedbackBuffer, feedbackBufferAllocation, maxSamples * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
		ptrFeedbackBuffer = new uint32_t[maxSamples];
	}

	void updateDataPre(const VkExtent2D &extent)
	{	
		bool writeToBuffer = false;

		if (dataUpdated == false) {
			randomSamplesSpherical.clear();
			randomSamplesCartesian.clear();

			if (choosePattern) {
				RandomGenerator rGen(seed);
							   
				for (uint32_t i = 0; i < nSamples; i++) {
					float theta = std::acos(1 - 2.0f * (rGen.getNextUint32_t() / 4294967295.0f));
					float phi = (2 * PI * (rGen.getNextUint32_t()) / 4294967295.0f);

					randomSamplesSpherical.push_back(glm::vec2(theta, phi));
				}
			}
			else {
				for (uint32_t i = 0; i < nSamples; i++) {
					float theta = std::acos(1 - 2.0f * ((float)i / nSamples));
					float phi = 2 * PI * 0;

					randomSamplesSpherical.push_back(glm::vec2(theta, phi));
				}
			}

			std::sort(randomSamplesSpherical.begin(), randomSamplesSpherical.end(), [](const glm::vec2& lhs, const glm::vec2& rhs)
			{
				if (lhs.x == rhs.x)
					return lhs.y < rhs.y;

				return lhs.x < rhs.x;
			});

			for (const auto& sample : randomSamplesSpherical)
				randomSamplesCartesian.push_back(glm::vec4(sphericalToCartesian(glm::vec3(1, sample)), nSamples));
									
			writeToBuffer = true;
			dataUpdated = true;
		}

		if (moveSampleInTime) {
			for (uint32_t i = 0; i < nSamples; i++) {
				randomSamplesSpherical[i].y += 0.1f;
				randomSamplesSpherical[i].y = randomSamplesSpherical[i].y > 2 * PI ? randomSamplesSpherical[i].y - 2 * PI : randomSamplesSpherical[i].y;
				randomSamplesCartesian[i] = glm::vec4(sphericalToCartesian(glm::vec3(1, randomSamplesSpherical[i])), nSamples);
			}
			writeToBuffer = true;
		}

		if (writeToBuffer) {
			memcpy(mptrSampleSphericalBuffer, randomSamplesSpherical.data(), randomSamplesSpherical.size() * sizeof(glm::vec2));
			memcpy(mptrSampleCartesianBuffer, randomSamplesCartesian.data(), randomSamplesCartesian.size() * sizeof(glm::vec4));
		}
		this->extent = extent;
	}

	void updateDataPost()
	{	
		memcpy(ptrFeedbackBuffer, mptrFeedbackBuffer, nSamples * sizeof(uint32_t));

		nonRaytracedSamples.clear();
		raytracedSamples.clear();
		intersectedSamples.clear();
		for (uint32_t i = 0; i < nSamples; i++) {
			if (ptrFeedbackBuffer[i] == 0)
				nonRaytracedSamples.push_back(randomSamplesSpherical[i]);
			else if (ptrFeedbackBuffer[i] == 1)
				raytracedSamples.push_back(randomSamplesSpherical[i]);
			else
				intersectedSamples.push_back(randomSamplesSpherical[i]);
		}
	}

	void cleanUp(const VmaAllocator& allocator)
	{	
		vmaDestroyBuffer(allocator, sampleSphericalBuffer, sampleSphericalBufferAllocation);
		vmaDestroyBuffer(allocator, sampleCartesianBuffer, sampleCartesianBufferAllocation);
		vmaDestroyBuffer(allocator, feedbackBuffer, feedbackBufferAllocation);
		delete[] ptrFeedbackBuffer;
	}

	void widget(uint32_t &collectData, uint32_t &pixelInfo)
	{
		if (ImGui::CollapsingHeader("RandomPattern")) {
			ImGui::RadioButton("Dynamic samples##UID_RndomSphericalPattern", &moveSampleInTime, 1); ImGui::SameLine();
			ImGui::RadioButton("Static samples##UID_RndomSphericalPattern", &moveSampleInTime, 0);

			int nS = static_cast<int>(nSamples);
			ImGui::SliderInt("Sample size##UID_RndomSphericalPattern", &nS, static_cast<int>(minSamples), static_cast<int>(maxSamples));
			dataUpdated = (nS == nSamples);
			nSamples = nS;

			int cP = choosePattern;
			ImGui::RadioButton("Random pattern##UID_RndomSphericalPattern", &cP, 1); ImGui::SameLine();
			ImGui::RadioButton("Regular pattern##UID_RndomSphericalPattern", &cP, 0);
			dataUpdated = (cP == choosePattern);
			choosePattern = cP;

			if (choosePattern) {
				int sd = static_cast<int>(seed);
				ImGui::SliderInt("Sample seed##UID_RndomSphericalPattern", &sd, 1, static_cast<int>(maxSamples));
				dataUpdated = dataUpdated && (sd == seed);
				seed = sd;
			}
			else {

			}
			

			if (ImGui::CollapsingHeader("CollectSamples##UID_RndomSphericalPattern")) {

				ImGui::SliderInt("Pixel X##UID_RndomSphericalPattern", &xPixelQuery, 0, static_cast<int>(extent.width));
				ImGui::SliderInt("Pixel Y##UID_RndomSphericalPattern", &yPixelQuery, 0, static_cast<int>(extent.height));

				collectData = 1;
				pixelInfo = (xPixelQuery & 0xffff) | (yPixelQuery & 0xffff) << 16;

				ImGui::SetNextPlotRange(0, 2 * PI, 0, PI, ImGuiCond_Always);
				if (ImGui::BeginPlot("Scatter Plot##UID_RndomSphericalPattern", "phi", "theta")) {
					ImGui::Text(("Intersected Samples:" + std::to_string(intersectedSamples.size())).c_str());
					ImGui::PushPlotStyleVar(ImPlotStyleVar_LineWeight, 0);
					ImGui::PushPlotStyleVar(ImPlotStyleVar_Marker, ImMarker_Circle);
					ImGui::PushPlotStyleVar(ImPlotStyleVar_MarkerSize, 4);
					auto getter = [](void* data, int idx) {
						glm::vec2 d = static_cast<const glm::vec2*>(data)[idx];
						return ImVec2(d.y, d.x);
					};
					ImGui::Plot("Not Raytraced##UID_RndomSphericalPattern", static_cast<ImVec2(*)(void*, int)>(getter), static_cast<void*>(nonRaytracedSamples.data()), static_cast<int>(nonRaytracedSamples.size()), 0);
					ImGui::Plot("Raytraced##UID_RndomSphericalPattern", static_cast<ImVec2(*)(void*, int)>(getter), static_cast<void*>(raytracedSamples.data()), static_cast<int>(raytracedSamples.size()), 0);
					ImGui::Plot("Intersected##UID_RndomSphericalPattern", static_cast<ImVec2(*)(void*, int)>(getter), static_cast<void*>(intersectedSamples.data()), static_cast<int>(intersectedSamples.size()), 0);
					ImGui::PopPlotStyleVar(2);
					ImGui::EndPlot();
				}
			}
			else
				collectData = 0;
		}
	}

	VkDescriptorBufferInfo getSphericalSamplesDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = sampleSphericalBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkDescriptorBufferInfo getCartesianSamplesDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = sampleCartesianBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

	VkDescriptorBufferInfo getFeedbackDescriptorBufferInfo() const
	{
		VkDescriptorBufferInfo descriptorBufferInfo = {};
		descriptorBufferInfo.buffer = feedbackBuffer;
		descriptorBufferInfo.offset = 0;
		descriptorBufferInfo.range = VK_WHOLE_SIZE;

		return descriptorBufferInfo;
	}

private:
	VkDeviceSize maxSamples;
	VkDeviceSize minSamples;

	VkDeviceSize nSamples;
	uint32_t seed;

	bool dataUpdated;
	int moveSampleInTime;
	int choosePattern; // Random or deterministic

	std::vector<glm::vec2> randomSamplesSpherical;
	VkBuffer sampleSphericalBuffer;
	VmaAllocation sampleSphericalBufferAllocation;
	glm::vec2* mptrSampleSphericalBuffer;

	std::vector<glm::vec4> randomSamplesCartesian;
	VkBuffer sampleCartesianBuffer;
	VmaAllocation sampleCartesianBufferAllocation;
	glm::vec4* mptrSampleCartesianBuffer;

	VkBuffer feedbackBuffer;
	VmaAllocation feedbackBufferAllocation;
	void* mptrFeedbackBuffer;
	uint32_t* ptrFeedbackBuffer;

	int xPixelQuery;
	int yPixelQuery;
	VkExtent2D extent;

	std::vector<glm::vec2> nonRaytracedSamples;
	std::vector<glm::vec2> raytracedSamples;
	std::vector<glm::vec2> intersectedSamples;
};