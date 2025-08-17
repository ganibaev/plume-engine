#include "render_initializers.h"


vk::CommandBufferBeginInfo vkinit::CmdBeginInfo(vk::CommandBufferUsageFlags flags /* = 0 */)
{
	vk::CommandBufferBeginInfo info = {};
	info.pInheritanceInfo = nullptr;
	info.flags = flags;
	return info;
}


vk::CommandBufferAllocateInfo vkinit::CmdAllocateInfo(vk::CommandPool pool)
{
	vk::CommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.commandPool = pool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
	return cmdAllocInfo;
}


vk::SubmitInfo vkinit::CmdSubmitInfo(vk::CommandBuffer* cmd)
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


vk::SemaphoreCreateInfo vkinit::SemaphoreCreateInfo(vk::SemaphoreCreateFlags flags /* = 0 */)
{
	vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.flags = flags;
	return semaphoreCreateInfo;
}


vk::FenceCreateInfo vkinit::FenceCreateInfo(vk::FenceCreateFlags flags)
{
	vk::FenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.flags = flags;
	return fenceCreateInfo;
}


vk::ShaderModuleCreateInfo vkinit::ShaderModuleInfo(const std::vector<uint32_t>& buffer)
{
	vk::ShaderModuleCreateInfo smCreateInfo = {};
	// in bytes
	smCreateInfo.codeSize = buffer.size() * sizeof(uint32_t);

	smCreateInfo.pCode = buffer.data();
	return smCreateInfo;
}


vk::RenderingAttachmentInfo vkinit::RenderAttachmentInfo(vk::ImageView imageView, vk::ImageLayout imageLayout,
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


vk::PipelineLayoutCreateInfo vkinit::PipelineLayoutInfo()
{
	vk::PipelineLayoutCreateInfo info = {};

	// default values
	info.setLayoutCount = 0;
	info.pSetLayouts = nullptr;
	info.pushConstantRangeCount = 0;
	info.pPushConstantRanges = nullptr;
	return info;
}


vk::DescriptorSetLayoutBinding vkinit::SetLayoutBinding(vk::DescriptorType type, vk::ShaderStageFlags stageFlags, uint32_t binding, uint32_t descCount /* = 1 */)
{
	vk::DescriptorSetLayoutBinding setBind = {};
	setBind.binding = binding;
	setBind.descriptorCount = descCount;
	setBind.descriptorType = type;
	setBind.pImmutableSamplers = nullptr;
	setBind.stageFlags = stageFlags;

	return setBind;
}


vk::WriteDescriptorSet vkinit::WriteDescriptorBuffer(vk::DescriptorType type, vk::DescriptorSet dstSet, vk::DescriptorBufferInfo* bufferInfo, uint32_t binding)
{
	vk::WriteDescriptorSet write = {};

	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = bufferInfo;

	return write;
}


vk::SamplerCreateInfo vkinit::SamplerInfo(vk::Filter magFilter, vk::Filter minFilter, float anisotropy /* = -1.0f */, float maxLod /* = VK_LOD_CLAMP_NONE */, vk::SamplerAddressMode samplerAddressMode /* = vk::SamplerAddressMode::eRepeat */)
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


vk::WriteDescriptorSet vkinit::WriteDescriptorImage(vk::DescriptorType type, vk::DescriptorSet dstSet, vk::DescriptorImageInfo* imageInfo, uint32_t binding, uint32_t descriptorCount /* = 1 */)
{
	vk::WriteDescriptorSet write = {};

	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = descriptorCount;
	write.descriptorType = type;
	write.pImageInfo = imageInfo;

	return write;
}
