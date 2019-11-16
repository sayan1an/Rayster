#pragma once

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include "generator.h"

class Gui
{	
public:
	TextureGenerator fontTexGen;
	VkImage fontTexImage;
	VkImageView fontTexImageView;
	VkSampler fontTexSampler;
	VmaAllocation fontTexAllocation;

	DescriptorSetGenerator descGen;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;

	GraphicsPipelineGenerator gfxPipeGen;

	struct PushConstBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstBlock;
	std::vector<VkPushConstantRange> pushConstatRanges;

	std::vector<VkDynamicState> dynamicStates;

	const uint32_t VERTEX_BINDING_ID = 0;
	std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
	void* vertexBufferPtr = nullptr;
	int vertexCount = 0;

	VkBuffer indexBuffer = VK_NULL_HANDLE;
	VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
	void* indexBufferPtr = nullptr;
	int indexCount = 0;

	const int bufferAllocMultiplier = 5; 

	void guiSetup()
	{
		ImGui::NewFrame();

		// Init imGui windows and elements

		ImVec4 clear_color = ImColor(114, 144, 154);
		static float f = 0.0f;
		ImGui::Text("Camera");
		ImGui::TextUnformatted("This is statement 2");

		//ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
		bool hello;
		ImGui::Begin("Example settings");
		ImGui::Checkbox("Render models", &hello);
		ImGui::End();
		//ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
		//ImGui::ShowDemoWindow();

		// Render to generate draw buffers
		ImGui::Render();
	}

	void updateData(const VmaAllocator& allocator)
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();

		auto createMappedBuffer = [](const VmaAllocator& allocator, VkBuffer& buffer, VmaAllocation& bufferAllocation, void*& mappedPtr, VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsageFlags)
		{
			VkBufferCreateInfo bufferCreateInfo = {};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = bufferSize;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | bufferUsageFlags;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocCreateInfo = {};
			allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &buffer, &bufferAllocation, nullptr) != VK_SUCCESS)
				throw std::runtime_error("Failed to create buffer!");

			vmaMapMemory(allocator, bufferAllocation, &mappedPtr);
		};

		VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		if (vertexBufferSize == 0 || indexBufferSize == 0) {
			std::cout << "GUI vertex and index buffer size are zero" << std::endl;
			return;
		}

		// We create a buffer of size larger than the required size to avoid buffer re-creation at runtime
		if (imDrawData->TotalVtxCount > bufferAllocMultiplier * vertexCount) {
			if (vertexCount != 0) {
				vmaUnmapMemory(allocator, vertexBufferAllocation);
				vmaDestroyBuffer(allocator, vertexBuffer, vertexBufferAllocation);
			}
			
			createMappedBuffer(allocator, vertexBuffer, vertexBufferAllocation, vertexBufferPtr, bufferAllocMultiplier * vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			vertexCount = imDrawData->TotalVtxCount;
		}

		if (imDrawData->TotalIdxCount > bufferAllocMultiplier * indexCount) {
			if (indexCount != 0) {
				vmaUnmapMemory(allocator, indexBufferAllocation);
				vmaDestroyBuffer(allocator, indexBuffer, indexBufferAllocation);
			}

			createMappedBuffer(allocator, indexBuffer, indexBufferAllocation, indexBufferPtr, bufferAllocMultiplier * indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
			indexCount = imDrawData->TotalIdxCount;
		}

		ImDrawVert* vtxDst = (ImDrawVert*)vertexBufferPtr;
		ImDrawIdx* idxDst = (ImDrawIdx*)indexBufferPtr;

		for (int n = 0; n < imDrawData->CmdListsCount; n++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];
			memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmd_list->VtxBuffer.Size;
			idxDst += cmd_list->IdxBuffer.Size;
		}
	}

	void cmdDraw(const VkCommandBuffer &cmdBuf)
	{
		ImGuiIO& io = ImGui::GetIO();

		vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkViewport viewport = {};
		viewport.width = io.DisplaySize.x;
		viewport.height = io.DisplaySize.y;
		viewport.maxDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

		// UI scale and translate via push constants
		pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
		pushConstBlock.translate = glm::vec2(-1.0f);
		vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

		// Render commands
		ImDrawData* imDrawData = ImGui::GetDrawData();
		int32_t vertexOffset = 0;
		int32_t indexOffset = 0;

		if (imDrawData->CmdListsCount > 0) {

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffer, offsets);
			vkCmdBindIndexBuffer(cmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

			for (int32_t i = 0; i < imDrawData->CmdListsCount; i++)
			{
				const ImDrawList* cmd_list = imDrawData->CmdLists[i];
				for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
				{
					const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
					VkRect2D scissorRect;
					scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
					scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
					scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
					scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
					vkCmdSetScissor(cmdBuf, 0, 1, &scissorRect);
					vkCmdDrawIndexed(cmdBuf, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
					indexOffset += pcmd->ElemCount;
				}
				vertexOffset += cmd_list->VtxBuffer.Size;
			}
		}
	}

	void createResources(const VkPhysicalDevice &physicalDevice, const VkDevice &device, const VmaAllocator &allocator, const VkQueue &queue, const VkCommandPool &cmdPool, const VkRenderPass &renderPass)
	{	
		fontTexGen.addTexture(Image2d(true));
		fontTexGen.createTexture(physicalDevice, device, allocator, queue, cmdPool, fontTexImage, fontTexImageView, fontTexSampler, fontTexAllocation);
		
		descGen.bindImage({ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }, { fontTexSampler, fontTexImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		descGen.generateDescriptorSet(device, &descriptorSetLayout, &descriptorPool, &descriptorSet);

		gfxPipeGen.addVertexShaderStage(device, ROOT + +"/shaders/ImGui/uiVert.spv");
		gfxPipeGen.addFragmentShaderStage(device, ROOT + +"/shaders/ImGui/uiFrag.spv");
		
		pushConstatRanges.push_back({ VK_SHADER_STAGE_VERTEX_BIT , 0, sizeof(PushConstBlock) });

		gfxPipeGen.addRasterizationState(VK_CULL_MODE_NONE);
		gfxPipeGen.addColorBlendAttachmentState(1, true); // enable alpha blending
		gfxPipeGen.addDepthStencilState(VK_FALSE, VK_FALSE);

		dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
		dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
		gfxPipeGen.addDynamicStates(dynamicStates);

		vertexBindingDescriptions.push_back({ VERTEX_BINDING_ID, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX });
		vertexAttributeDescriptions.push_back({ 0, VERTEX_BINDING_ID, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos) });
		vertexAttributeDescriptions.push_back({ 1, VERTEX_BINDING_ID,  VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv) });
		vertexAttributeDescriptions.push_back({ 2, VERTEX_BINDING_ID,  VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col) });
		gfxPipeGen.addVertexInputState(vertexBindingDescriptions, vertexAttributeDescriptions);

		std::cout << "Create gui pipe" << std::endl;
		gfxPipeGen.createPipeline(device, descriptorSetLayout, renderPass, 1, &pipeline, &pipelineLayout, pushConstatRanges);
		std::cout << "Done gui pipe" << std::endl;
	}

	Gui()
	{
		ImGui::CreateContext();
	}

	void setup()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.2f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.2f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.2f);
		style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.2f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 0.2f);
		// Dimensions
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(400, 400);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}
};