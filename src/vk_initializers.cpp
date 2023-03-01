#include <vk_initializers.h>

VkCommandPoolCreateInfo vkinit::command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags /* = 0 */)
{
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;

	// this command pool will submit graphics commands
	commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
	// allow the pool to reset individual command buffers
	commandPoolInfo.flags = flags;
	return commandPoolInfo;
}

VkCommandBufferAllocateInfo vkinit::command_buffer_allocate_info(VkCommandPool pool, uint32_t count /* = 1 */, VkCommandBufferLevel level /* = VK_COMMAND_BUFFER_LEVEL_PRIMARY */)
{
	VkCommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;

	// specify the command pool
	cmdAllocInfo.commandPool = pool;
	// allocate one command buffer
	cmdAllocInfo.commandBufferCount = count;
	// command buffer level is primary
	cmdAllocInfo.level = level;
	return cmdAllocInfo;
}

VkSemaphoreCreateInfo vkinit::semaphore_create_info(VkSemaphoreCreateFlags flags /* = 0 */)
{
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = flags;
	return semaphoreCreateInfo;
}

VkFenceCreateInfo vkinit::fence_create_info(VkFenceCreateFlags flags)
{
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	fenceCreateInfo.flags = flags;
	return fenceCreateInfo;
}

VkShaderModuleCreateInfo vkinit::sm_create_info(const std::vector<uint32_t>& buffer)
{
	VkShaderModuleCreateInfo smCreateInfo = {};
	smCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smCreateInfo.pNext = nullptr;

	// in bytes
	smCreateInfo.codeSize = buffer.size() * sizeof(uint32_t);

	smCreateInfo.pCode = buffer.data();
	return smCreateInfo;
}

VkPipelineShaderStageCreateInfo vkinit::pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule)
{
	VkPipelineShaderStageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.pNext = nullptr;

	// shader stage
	info.stage = stage;
	// module with the code for the stage
	info.module = shaderModule;
	// shader entry point
	info.pName = "main";
	return info;
}

VkPipelineVertexInputStateCreateInfo vkinit::vertex_input_state_create_info()
{
	VkPipelineVertexInputStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	info.pNext = nullptr;

	// no vertex bindings or attributes (yet)
	info.vertexBindingDescriptionCount = 0;
	info.vertexAttributeDescriptionCount = 0;
	return info;
}

VkPipelineInputAssemblyStateCreateInfo vkinit::input_assembly_create_info(VkPrimitiveTopology topology)
{
	VkPipelineInputAssemblyStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	info.pNext = nullptr;

	info.topology = topology;
	// no primitive restart (for now)
	info.primitiveRestartEnable = VK_FALSE;
	return info;
}

VkPipelineRasterizationStateCreateInfo vkinit::rasterization_state_create_info(VkPolygonMode polygonMode, VkCullModeFlags cullMode /* = VK_CULL_MODE_NONE */)
{
	VkPipelineRasterizationStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	info.pNext = nullptr;

	info.depthClampEnable = VK_FALSE;
	// don't discard primitives before rasterization stage
	info.rasterizerDiscardEnable = VK_FALSE;

	// wireframe of solid, for example
	info.polygonMode = polygonMode;
	info.lineWidth = 1.0f;
	// no backface culling (bad for raytracing, for instance). Might enable for very complex rasterized scenes though
	info.cullMode = cullMode;
	info.frontFace = VK_FRONT_FACE_CLOCKWISE;
	// no depth bias
	info.depthBiasEnable = VK_FALSE;
	info.depthBiasClamp = 0.0f;
	info.depthBiasConstantFactor = 0.0f;
	info.depthBiasSlopeFactor = 0.0f;
	
	return info;
}

VkPipelineMultisampleStateCreateInfo vkinit::multisampling_state_create_info()
{
	VkPipelineMultisampleStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	info.pNext = nullptr;

	// no MSAA
	info.sampleShadingEnable = VK_FALSE;
	info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	info.minSampleShading = 1.0f;
	info.pSampleMask = nullptr;
	info.alphaToCoverageEnable = VK_FALSE;
	info.alphaToOneEnable = VK_FALSE;

	return info;
}

VkPipelineColorBlendAttachmentState vkinit::color_blend_attachment_state()
{
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	return colorBlendAttachment;
}

VkPipelineLayoutCreateInfo vkinit::pipeline_layout_create_info()
{
	VkPipelineLayoutCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.pNext = nullptr;

	// default values
	info.flags = 0;
	info.setLayoutCount = 0;
	info.pSetLayouts = nullptr;
	info.pushConstantRangeCount = 0;
	info.pPushConstantRanges = nullptr;
	return info;
}