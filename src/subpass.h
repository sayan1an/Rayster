#pragma once

#include "vulkan/vulkan.h"
#include  <vector>

#include "helper.h"

class DescriptorSet {
public:
	VkDescriptorSetLayout descriptorSetLayout; // create first
	VkDescriptorPool descriptorPool; // create fifth
	std::vector<VkDescriptorSet> descriptorSets; // create sixth
};

class Pipeline : public DescriptorSet {
public:
	VkPipelineLayout pipelineLayout; // create second
	VkPipeline pipeline; // create third
		
	void createShaderStageInfo(const std::string &filename, const VkDevice &device, VkShaderStageFlagBits stage) {
		auto shaderCode = readFile(filename);
		VkShaderModule shaderModule = createShaderModule(shaderCode, device);

		VkPipelineShaderStageCreateInfo shaderStageInfo = {};
		shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStageInfo.stage = stage;
		shaderStageInfo.module = shaderModule;
		shaderStageInfo.pName = "main";

		shaderModules.push_back(shaderModule);
		shaderStageInfos.push_back(shaderStageInfo);
	}

	void createVertexInputStateInfo(const std::vector<VkVertexInputBindingDescription> &bindingDescription, const std::vector<VkVertexInputAttributeDescription> &attributeDescriptions, VkPipelineVertexInputStateCreateInfo &vertexInputInfo) {
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size());
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexBindingDescriptions = bindingDescription.data();
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
	}

	void createViewportStateInfo(const VkExtent2D &swapChainExtent, VkPipelineViewportStateCreateInfo &viewportState) {
		viewport = std::make_shared<VkViewport>();
		viewport->x = 0.0f;
		viewport->y = 0.0f;
		viewport->width = (float)swapChainExtent.width;
		viewport->height = (float)swapChainExtent.height;
		viewport->minDepth = 0.0f;
		viewport->maxDepth = 1.0f;

		scissor = std::make_shared<VkRect2D>();
		scissor->offset = { 0, 0 };
		scissor->extent = swapChainExtent;

		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = viewport.get();
		viewportState.scissorCount = 1;
		viewportState.pScissors = scissor.get();
	}

	void createRasterizationStateInfo(VkPipelineRasterizationStateCreateInfo &rasterizer) {
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
	}

	void createDepthStencilStateInfo(VkPipelineDepthStencilStateCreateInfo &depthStencil) {
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;
	}

	void createColorBlendStateInfo(VkPipelineColorBlendStateCreateInfo &colorBlending) {
		colorBlendAttachment = std::make_shared<VkPipelineColorBlendAttachmentState>();
		colorBlendAttachment->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment->blendEnable = VK_FALSE;

		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = colorBlendAttachment.get();
		colorBlending.blendConstants[0] = 0.0f;
		colorBlending.blendConstants[1] = 0.0f;
		colorBlending.blendConstants[2] = 0.0f;
		colorBlending.blendConstants[3] = 0.0f;
	}

	void createPipelineLayout(const VkDevice& device, const VkDescriptorSetLayout &descriptorSetLayout) {
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout!");
		}
	}

	void cleanPipelineInternalState(const VkDevice &device) {
		for (auto module : shaderModules)
			vkDestroyShaderModule(device, module, nullptr);

		shaderModules.clear();
		shaderStageInfos.clear();
	}

	void createDefaultGraphicsPipelineInfo(const VkDevice &device, const std::string &vertShaderFile, const std::string& fragShaderFile,
		std::vector<VkVertexInputBindingDescription> &bindingDescription, std::vector<VkVertexInputAttributeDescription> &attributeDescription,
		const VkExtent2D &swapChainExtent, const VkSampleCountFlagBits &msaaSamples,
		VkGraphicsPipelineCreateInfo &pipelineInfo) {
		
		createShaderStageInfo(vertShaderFile, device, VK_SHADER_STAGE_VERTEX_BIT);
		createShaderStageInfo(fragShaderFile, device, VK_SHADER_STAGE_FRAGMENT_BIT);

		vertexInputInfo = std::make_shared<VkPipelineVertexInputStateCreateInfo>();
		createVertexInputStateInfo(bindingDescription, attributeDescription, vertexInputInfo.get()[0]);

		inputAssembly = std::make_shared<VkPipelineInputAssemblyStateCreateInfo>();
		inputAssembly->sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly->primitiveRestartEnable = VK_FALSE;

		viewportState = std::make_shared<VkPipelineViewportStateCreateInfo>();
		createViewportStateInfo(swapChainExtent, viewportState.get()[0]);

		rasterizer = std::make_shared<VkPipelineRasterizationStateCreateInfo>();
		createRasterizationStateInfo(rasterizer.get()[0]);

		multisampling = std::make_shared<VkPipelineMultisampleStateCreateInfo>();
		multisampling->sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling->sampleShadingEnable = VK_FALSE;
		multisampling->rasterizationSamples = msaaSamples;

		depthStencil = std::make_shared<VkPipelineDepthStencilStateCreateInfo>();
		createDepthStencilStateInfo(depthStencil.get()[0]);

		colorBlending = std::make_shared<VkPipelineColorBlendStateCreateInfo>();
		createColorBlendStateInfo(colorBlending.get()[0]);

		createPipelineLayout(device, descriptorSetLayout);

		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStageInfos.size());
		pipelineInfo.pStages = shaderStageInfos.data();
		pipelineInfo.pVertexInputState = vertexInputInfo.get();
		pipelineInfo.pInputAssemblyState = inputAssembly.get();
		pipelineInfo.pViewportState = viewportState.get();
		pipelineInfo.pRasterizationState = rasterizer.get();
		pipelineInfo.pMultisampleState = multisampling.get();
		pipelineInfo.pDepthStencilState = depthStencil.get();
		pipelineInfo.pColorBlendState = colorBlending.get();
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	}

private:
	// temporary variables
	std::vector<VkShaderModule> shaderModules;
	std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos;

	std::shared_ptr<VkViewport> viewport;
	std::shared_ptr<VkRect2D> scissor;
	std::shared_ptr<VkPipelineColorBlendAttachmentState> colorBlendAttachment;
	std::shared_ptr<VkPipelineVertexInputStateCreateInfo> vertexInputInfo;
	std::shared_ptr<VkPipelineInputAssemblyStateCreateInfo> inputAssembly;
	std::shared_ptr<VkPipelineViewportStateCreateInfo> viewportState;
	std::shared_ptr<VkPipelineRasterizationStateCreateInfo> rasterizer;
	std::shared_ptr<VkPipelineMultisampleStateCreateInfo> multisampling;
	std::shared_ptr<VkPipelineDepthStencilStateCreateInfo> depthStencil;
	std::shared_ptr< VkPipelineColorBlendStateCreateInfo> colorBlending;
	
	static VkShaderModule createShaderModule(const std::vector<char>& code, const VkDevice& device) {
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			throw std::runtime_error("failed to create shader module!");
		}

		return shaderModule;
	}
};

class Subpass : public Pipeline {
public:
	VkSubpassDescription subpass; // create fourth
};