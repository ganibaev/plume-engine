#include "vk_initializers.h"

vk::CommandPoolCreateInfo vkinit::command_pool_create_info(uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags flags /* = 0 */)
{
	vk::CommandPoolCreateInfo commandPoolInfo = {};
	// this command pool will submit graphics commands
	commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
	// allow the pool to reset individual command buffers
	commandPoolInfo.flags = flags;
	return commandPoolInfo;
}

vk::CommandBufferAllocateInfo vkinit::command_buffer_allocate_info(vk::CommandPool pool, uint32_t count /* = 1 */, vk::CommandBufferLevel level /* = VK_COMMAND_BUFFER_LEVEL_PRIMARY */)
{
	vk::CommandBufferAllocateInfo cmdAllocInfo = {};
	// specify the command pool
	cmdAllocInfo.commandPool = pool;
	// allocate one command buffer
	cmdAllocInfo.commandBufferCount = count;
	// command buffer level is primary
	cmdAllocInfo.level = level;
	return cmdAllocInfo;
}

vk::CommandBufferBeginInfo vkinit::command_buffer_begin_info(vk::CommandBufferUsageFlags flags /* = 0 */)
{
	vk::CommandBufferBeginInfo info = {};
	info.pInheritanceInfo = nullptr;
	info.flags = flags;
	return info;
}

vk::SubmitInfo vkinit::submit_info(vk::CommandBuffer* cmd)
{
	vk::SubmitInfo info = {};
	info.waitSemaphoreCount = 0;
	info.pWaitSemaphores = nullptr;
	info.pWaitDstStageMask = nullptr;
	info.commandBufferCount = 1;
	info.pCommandBuffers = cmd;
	info.signalSemaphoreCount = 0;
	info.pSignalSemaphores = nullptr;

	return info;
}

vk::SemaphoreCreateInfo vkinit::semaphore_create_info(vk::SemaphoreCreateFlags flags /* = 0 */)
{
	vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.flags = flags;
	return semaphoreCreateInfo;
}

vk::FenceCreateInfo vkinit::fence_create_info(vk::FenceCreateFlags flags)
{
	vk::FenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.flags = flags;
	return fenceCreateInfo;
}

vk::ShaderModuleCreateInfo vkinit::sm_create_info(const std::vector<uint32_t>& buffer)
{
	vk::ShaderModuleCreateInfo smCreateInfo = {};
	// in bytes
	smCreateInfo.codeSize = buffer.size() * sizeof(uint32_t);

	smCreateInfo.pCode = buffer.data();
	return smCreateInfo;
}

vk::PipelineShaderStageCreateInfo vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits stage, vk::ShaderModule shaderModule)
{
	vk::PipelineShaderStageCreateInfo info = {};

	// shader stage
	info.stage = stage;
	// module with the code for the stage
	info.module = shaderModule;
	// shader entry point
	info.pName = "main";
	return info;
}

vk::PipelineVertexInputStateCreateInfo vkinit::vertex_input_state_create_info()
{
	vk::PipelineVertexInputStateCreateInfo info = {};

	// no vertex bindings or attributes (yet)
	info.vertexBindingDescriptionCount = 0;
	info.vertexAttributeDescriptionCount = 0;
	return info;
}

vk::PipelineInputAssemblyStateCreateInfo vkinit::input_assembly_create_info(vk::PrimitiveTopology topology)
{
	vk::PipelineInputAssemblyStateCreateInfo info = {};

	info.topology = topology;
	// no primitive restart (for now)
	info.primitiveRestartEnable = VK_FALSE;
	return info;
}

vk::PipelineRasterizationStateCreateInfo vkinit::rasterization_state_create_info(vk::PolygonMode polygonMode, vk::CullModeFlags cullMode /* = VK_CULL_MODE_NONE */)
{
	vk::PipelineRasterizationStateCreateInfo info = {};

	info.depthClampEnable = VK_FALSE;
	// don't discard primitives before rasterization stage
	info.rasterizerDiscardEnable = VK_FALSE;

	// wireframe of solid, for example
	info.polygonMode = polygonMode;
	info.lineWidth = 1.0f;
	// no backface culling (bad for raytracing, for instance). Might enable for very complex rasterized scenes though
	info.cullMode = cullMode;
	info.frontFace = vk::FrontFace::eClockwise;
	// no depth bias
	info.depthBiasEnable = VK_FALSE;
	info.depthBiasClamp = 0.0f;
	info.depthBiasConstantFactor = 0.0f;
	info.depthBiasSlopeFactor = 0.0f;
	
	return info;
}

vk::PipelineMultisampleStateCreateInfo vkinit::multisampling_state_create_info(vk::SampleCountFlagBits numSamples)
{
	vk::PipelineMultisampleStateCreateInfo info = {};

	// MSAA
	info.sampleShadingEnable = VK_FALSE;
	info.rasterizationSamples = numSamples;
	info.minSampleShading = 1.0f;
	info.pSampleMask = nullptr;
	info.alphaToCoverageEnable = VK_FALSE;
	info.alphaToOneEnable = VK_FALSE;

	return info;
}

vk::PipelineDepthStencilStateCreateInfo vkinit::depth_stencil_create_info(bool bDepthTest, bool bDepthWrite, vk::CompareOp compareOp)
{
	vk::PipelineDepthStencilStateCreateInfo info = {};

	info.depthTestEnable = bDepthTest ? VK_TRUE : VK_FALSE;
	info.depthWriteEnable = bDepthWrite ? VK_TRUE : VK_FALSE;
	info.depthCompareOp = bDepthTest ? compareOp : vk::CompareOp::eAlways;
	info.depthBoundsTestEnable = VK_FALSE;
	// info.minDepthBounds = 0.0f;
	// info.maxDepthBounds = 1.0f;
	info.stencilTestEnable = VK_FALSE;
	
	return info;
}

vk::RenderingAttachmentInfo vkinit::rendering_attachment_info(vk::ImageView imageView, vk::ImageLayout imageLayout,
	vk::AttachmentLoadOp loadOp, vk::AttachmentStoreOp storeOp, vk::ClearValue clearValue)
{
	vk::RenderingAttachmentInfo attachmentInfo;
	
	attachmentInfo.imageView = imageView;
	attachmentInfo.imageLayout = imageLayout;
	attachmentInfo.loadOp = loadOp;
	attachmentInfo.storeOp = storeOp;
	attachmentInfo.clearValue = clearValue;

	return attachmentInfo;
}

vk::PipelineColorBlendAttachmentState vkinit::color_blend_attachment_state(vk::Bool32 blendEnable /* = VK_FALSE */)
{
	vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	colorBlendAttachment.blendEnable = blendEnable;
	colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
	colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
	
	return colorBlendAttachment;
}

vk::PipelineLayoutCreateInfo vkinit::pipeline_layout_create_info()
{
	vk::PipelineLayoutCreateInfo info = {};

	// default values
	info.setLayoutCount = 0;
	info.pSetLayouts = nullptr;
	info.pushConstantRangeCount = 0;
	info.pPushConstantRanges = nullptr;
	return info;
}

vk::ImageCreateInfo vkinit::image_create_info(vk::Format format, vk::ImageUsageFlags usageFlags, vk::Extent3D extent,
	uint32_t mipLevels, vk::SampleCountFlagBits numSamples /* = VK_SAMPLE_COUNT_1_BIT */,
	ImageType type /* = ImageType::eTexture */)
{
	vk::ImageCreateInfo info = {};

	info.imageType = vk::ImageType::e2D;
	
	info.format = format;
	info.extent = extent;
	
	info.mipLevels = mipLevels;
	info.arrayLayers = (type == ImageType::eCubemap) ? 6 : 1;
	info.samples = numSamples;
	info.tiling = vk::ImageTiling::eOptimal;
	info.usage = usageFlags;
	info.flags = (type == ImageType::eCubemap) ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlagBits(0);

	return info;
}

vk::ImageViewCreateInfo vkinit::image_view_create_info(vk::Format format, vk::Image image,
	vk::ImageAspectFlags aspectFlags, uint32_t mipLevels, ImageType type/* = ImageType::eTexture */)
{
	vk::ImageViewType viewType = {};

	switch (type)
	{
	case ImageType::eTexture:
		viewType = vk::ImageViewType::e2D;
		break;
	case ImageType::eCubemap:
		viewType = vk::ImageViewType::eCube;
		break;
	default:
		break;
	}

	vk::ImageViewCreateInfo info = {};

	info.viewType = viewType;
	info.image = image;
	info.format = format;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = mipLevels;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = (type == ImageType::eCubemap) ? 6 : 1;
	info.subresourceRange.aspectMask = aspectFlags;
	
	return info;
}

vk::DescriptorSetLayoutBinding vkinit::descriptor_set_layout_binding(vk::DescriptorType type, vk::ShaderStageFlags stageFlags, uint32_t binding, uint32_t descCount /* = 1 */)
{
	vk::DescriptorSetLayoutBinding setBind = {};
	setBind.binding = binding;
	setBind.descriptorCount = descCount;
	setBind.descriptorType = type;
	setBind.pImmutableSamplers = nullptr;
	setBind.stageFlags = stageFlags;

	return setBind;
}

vk::WriteDescriptorSet vkinit::write_descriptor_buffer(vk::DescriptorType type, vk::DescriptorSet dstSet, vk::DescriptorBufferInfo* bufferInfo, uint32_t binding)
{
	vk::WriteDescriptorSet write = {};

	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = bufferInfo;

	return write;
}

vk::SamplerCreateInfo vkinit::sampler_create_info(vk::Filter magFilter, vk::Filter minFilter, uint32_t mipLevels /* = 1 */, float maxLod /* = VK_LOD_CLAMP_NONE */, vk::SamplerAddressMode samplerAddressMode /* = VK_SAMPLER_ADDRESS_MODE_REPEAT */)
{
	vk::SamplerCreateInfo info = {};
	
	info.magFilter = magFilter;
	info.minFilter = minFilter;
	info.addressModeU = samplerAddressMode;
	info.addressModeV = samplerAddressMode;
	info.addressModeW = samplerAddressMode;

	info.mipmapMode = vk::SamplerMipmapMode::eLinear;
	info.minLod = 0.0f;
	info.maxLod = maxLod;
	info.mipLodBias = 0.0f;
	
	return info;
}

vk::WriteDescriptorSet vkinit::write_descriptor_image(vk::DescriptorType type, vk::DescriptorSet dstSet, vk::DescriptorImageInfo* imageInfo, uint32_t binding, uint32_t descriptorCount /* = 1 */)
{
	vk::WriteDescriptorSet write = {};

	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = descriptorCount;
	write.descriptorType = type;
	write.pImageInfo = imageInfo;

	return write;
}