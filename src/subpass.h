#pragma once

#include "vulkan/vulkan.h"
#include  <vector>

class DescriptorSet {
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;
};

class Pipeline {
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

class Subpass {
	Pipeline pipeline;
	std::vector<DescriptorSet> descriptorSets;

	VkSubpassDescription subpass;
};