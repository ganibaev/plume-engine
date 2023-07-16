// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_types.h"
#include <vector>

namespace vkinit {

	vk::CommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags flags = {});
	vk::CommandBufferAllocateInfo command_buffer_allocate_info(vk::CommandPool pool, uint32_t count = 1, 
		vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	
	vk::CommandBufferBeginInfo command_buffer_begin_info(vk::CommandBufferUsageFlags flags = {});
	vk::SubmitInfo submit_info(vk::CommandBuffer* cmd);

	vk::SemaphoreCreateInfo semaphore_create_info(vk::SemaphoreCreateFlags flags = {});
	vk::FenceCreateInfo fence_create_info(vk::FenceCreateFlags flags = {});

	vk::ShaderModuleCreateInfo sm_create_info(const std::vector<uint32_t>& buffer);

	vk::PipelineShaderStageCreateInfo pipeline_shader_stage_create_info(vk::ShaderStageFlagBits stage, vk::ShaderModule shaderModule);
	vk::PipelineVertexInputStateCreateInfo vertex_input_state_create_info();
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_create_info(vk::PrimitiveTopology topology);
	vk::PipelineRasterizationStateCreateInfo rasterization_state_create_info(vk::PolygonMode polygonMode, 
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone);
	vk::PipelineMultisampleStateCreateInfo multisampling_state_create_info(vk::SampleCountFlagBits numSamples);
	vk::PipelineDepthStencilStateCreateInfo depth_stencil_create_info(bool bDepthTest, bool bDepthWrite, vk::CompareOp compareOp);
	
	vk::RenderPassBeginInfo render_pass_begin_info(vk::RenderPass renderPass, vk::Extent2D windowExtent, vk::Framebuffer framebuffer);
	
	vk::PipelineColorBlendAttachmentState color_blend_attachment_state(vk::Bool32 blendEnable = VK_FALSE);
	vk::PipelineLayoutCreateInfo pipeline_layout_create_info();

	vk::ImageCreateInfo image_create_info(vk::Format format, vk::ImageUsageFlags usageFlags, vk::Extent3D extent,
		uint32_t mipLevels, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1);
	vk::ImageViewCreateInfo image_view_create_info(vk::Format format, vk::Image image, vk::ImageAspectFlags aspectFlags, 
		uint32_t mipLevels);
	
	vk::DescriptorSetLayoutBinding descriptor_set_layout_binding(vk::DescriptorType type,
		vk::ShaderStageFlags stageFlags, uint32_t binding, uint32_t descCount = 1);
	vk::WriteDescriptorSet write_descriptor_buffer(vk::DescriptorType type, vk::DescriptorSet dstSet,
		vk::DescriptorBufferInfo* bufferInfo, uint32_t binding);

	vk::SamplerCreateInfo sampler_create_info(vk::Filter magFilter, vk::Filter minFilter, uint32_t mipLevels = 1,
		float maxLod = VK_LOD_CLAMP_NONE, vk::SamplerAddressMode samplerAddressMode = vk::SamplerAddressMode::eRepeat);
	vk::WriteDescriptorSet write_descriptor_image(vk::DescriptorType type, vk::DescriptorSet dstSet,
		vk::DescriptorImageInfo* imageInfo, uint32_t binding, uint32_t descriptorCount = 1);
}
