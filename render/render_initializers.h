#pragma once

#include "render_types.h"
#include <vector>

namespace vkinit {
	
	vk::CommandBufferBeginInfo command_buffer_begin_info(vk::CommandBufferUsageFlags flags = {});
	vk::CommandBufferAllocateInfo command_buffer_allocate_info(vk::CommandPool pool);
	vk::SubmitInfo submit_info(vk::CommandBuffer* cmd);

	vk::SemaphoreCreateInfo semaphore_create_info(vk::SemaphoreCreateFlags flags = {});
	vk::FenceCreateInfo fence_create_info(vk::FenceCreateFlags flags = {});

	vk::ShaderModuleCreateInfo sm_create_info(const std::vector<uint32_t>& buffer);
	
	vk::RenderingAttachmentInfo rendering_attachment_info(vk::ImageView imageView, vk::ImageLayout imageLayout,
		vk::AttachmentLoadOp loadOp, vk::AttachmentStoreOp storeOp, vk::ClearValue clearValue);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info();
	
	vk::DescriptorSetLayoutBinding descriptor_set_layout_binding(vk::DescriptorType type,
		vk::ShaderStageFlags stageFlags, uint32_t binding, uint32_t descCount = 1);
	vk::WriteDescriptorSet write_descriptor_buffer(vk::DescriptorType type, vk::DescriptorSet dstSet,
		vk::DescriptorBufferInfo* bufferInfo, uint32_t binding);

	vk::SamplerCreateInfo sampler_create_info(vk::Filter magFilter, vk::Filter minFilter,
		float maxLod = VK_LOD_CLAMP_NONE, vk::SamplerAddressMode samplerAddressMode = vk::SamplerAddressMode::eRepeat);
	vk::WriteDescriptorSet write_descriptor_image(vk::DescriptorType type, vk::DescriptorSet dstSet,
		vk::DescriptorImageInfo* imageInfo, uint32_t binding, uint32_t descriptorCount = 1);
}
