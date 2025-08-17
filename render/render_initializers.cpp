#include "render_initializers.h"


vk::CommandBufferBeginInfo vkinit::command_buffer_begin_info(vk::CommandBufferUsageFlags flags /* = 0 */)
{
	vk::CommandBufferBeginInfo info = {};
	info.pInheritanceInfo = nullptr;
	info.flags = flags;
	return info;
}


vk::CommandBufferAllocateInfo vkinit::command_buffer_allocate_info(vk::CommandPool pool)
{
	vk::CommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.commandPool = pool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
	return cmdAllocInfo;
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


vk::SamplerCreateInfo vkinit::sampler_create_info(vk::Filter magFilter, vk::Filter minFilter, float anisotropy /* = -1.0f */, float maxLod /* = VK_LOD_CLAMP_NONE */, vk::SamplerAddressMode samplerAddressMode /* = vk::SamplerAddressMode::eRepeat */)
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

	info.anisotropyEnable = anisotropy >= 0.0f;
	info.maxAnisotropy = anisotropy;
	
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
