#pragma once

#include "render_types.h"
#include <vector>

namespace vkinit {
	
	vk::CommandBufferBeginInfo CmdBeginInfo(vk::CommandBufferUsageFlags flags = {});
	vk::CommandBufferAllocateInfo CmdAllocateInfo(vk::CommandPool pool);
	vk::SubmitInfo CmdSubmitInfo(vk::CommandBuffer* cmd);

	vk::SemaphoreCreateInfo SemaphoreCreateInfo(vk::SemaphoreCreateFlags flags = {});
	vk::FenceCreateInfo FenceCreateInfo(vk::FenceCreateFlags flags = {});

	vk::ShaderModuleCreateInfo ShaderModuleInfo(const std::vector<uint32_t>& buffer);
	
	vk::RenderingAttachmentInfo RenderAttachmentInfo(vk::ImageView imageView, vk::ImageLayout imageLayout,
		vk::AttachmentLoadOp loadOp, vk::AttachmentStoreOp storeOp, vk::ClearValue clearValue);

	vk::PipelineLayoutCreateInfo PipelineLayoutInfo();
	
	vk::DescriptorSetLayoutBinding SetLayoutBinding(vk::DescriptorType type,
		vk::ShaderStageFlags stageFlags, uint32_t binding, uint32_t descCount = 1);
	vk::WriteDescriptorSet WriteDescriptorBuffer(vk::DescriptorType type, vk::DescriptorSet dstSet,
		vk::DescriptorBufferInfo* bufferInfo, uint32_t binding);

	vk::SamplerCreateInfo SamplerInfo(vk::Filter magFilter, vk::Filter minFilter, float anisotropy = -1.0f,
		float maxLod = VK_LOD_CLAMP_NONE, vk::SamplerAddressMode samplerAddressMode = vk::SamplerAddressMode::eRepeat);
	vk::WriteDescriptorSet WriteDescriptorImage(vk::DescriptorType type, vk::DescriptorSet dstSet,
		vk::DescriptorImageInfo* imageInfo, uint32_t binding, uint32_t descriptorCount = 1);
}
