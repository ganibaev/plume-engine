﻿#include "render_system.h"

#include "render_initializers.h"

#include "render_textures.h"
#include "render_lights.h"
#include "render_raytracing.h"
#include "render_shader.h"

// This will make initialization much less of a pain
#include "VkBootstrap.h"

#include <unordered_map>

#include <glm/gtx/transform.hpp>

#ifdef _DEBUG
	constexpr static bool ENABLE_VALIDATION_LAYERS = true;
#else
	constexpr static bool ENABLE_VALIDATION_LAYERS = false;
#endif // _DEBUG

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


void RenderSystem::unpack_init_data(const RenderSystem::InitData& initData)
{
	_pCamera = initData.pCam;
	_pLightManager = initData.pLightManager;

	_pWindow = initData.pWindow;
	_windowExtent = initData.windowExtent;
	
	_windowExtent3D.width = _windowExtent.width;
	_windowExtent3D.height = _windowExtent.height;
	_windowExtent3D.depth = 1;
}


void RenderSystem::init(const RenderSystem::InitData& initData)
{
	unpack_init_data(initData);
	
	// load core Vulkan structures
	init_vulkan();

	_descMng.init(&_device, &_mainDeletionQueue);

	init_swapchain();

	init_commands();

	init_gbuffer_attachments();

	init_prepass_attachments();

	init_raytracing();

	init_sync_structures();

	load_meshes();

	load_images();

	init_descriptors();

	init_scene();

	init_blas();

	init_tlas();

	init_rt_descriptors();

	_descMng.allocate_sets();

	init_pipelines();

	init_rt_pipeline();

	init_shader_binding_table();

	init_ui();

	_descMng.update_sets();

	// everything went well
	_isInitialized = true;
}


void RenderSystem::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Plume Start")
		.request_validation_layers(ENABLE_VALIDATION_LAYERS)
		.require_api_version(1, 3, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkb_inst.fp_vkGetInstanceProcAddr);
	// store instance
	_instance = vkb_inst.instance;
	// store debug messenger
	_debug_messenger = vkb_inst.debug_messenger;

	VkSurfaceKHR surfaceC;
	// get surface of the window we opened with SDL
	SDL_Vulkan_CreateSurface(_pWindow, _instance, nullptr, &surfaceC);
	_surface = surfaceC;

	// use VkBootstrap to select a GPU
	// the GPU should be able to write to SDL surface and support Vulkan 1.3
	vkb::PhysicalDeviceSelector selector{ vkb_inst };

	vk::PhysicalDeviceFeatures miscFeatures;
	miscFeatures.shaderInt64 = VK_TRUE;

	vk::PhysicalDeviceVulkan13Features v13Features;
	v13Features.synchronization2 = VK_TRUE;

	vkb::PhysicalDevice physicalDevice = selector
		.set_surface(_surface)
		.add_required_extension("VK_KHR_acceleration_structure")
		.add_required_extension("VK_KHR_ray_tracing_pipeline")
		.add_required_extension("VK_KHR_ray_query")
		.add_required_extension("VK_KHR_deferred_host_operations")
		.add_required_extension("VK_KHR_shader_clock")
		.add_required_extension("VK_KHR_shader_non_semantic_info")
		.set_required_features(miscFeatures)
		.select()
		.value();

	_cfg.SHADER_EXECUTION_REORDERING = physicalDevice.enable_extension_if_present("VK_NV_ray_tracing_invocation_reorder");

	// create final Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vk::PhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {};
	shaderDrawParametersFeatures.shaderDrawParameters = VK_TRUE;
	
	vk::PhysicalDeviceDescriptorIndexingFeatures descIndexingFeatures = {};
	descIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
	descIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
	descIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;

	// add extensions for ray tracing

	vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures;
	accelFeatures.accelerationStructure = VK_TRUE;
	vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures;
	rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
	vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures;
	rayQueryFeatures.rayQuery = VK_TRUE;
	vk::PhysicalDeviceBufferDeviceAddressFeatures addressFeatures;
	addressFeatures.bufferDeviceAddress = VK_TRUE;
	vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures;
	dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
	vk::PhysicalDeviceSynchronization2Features syncFeatures;
	syncFeatures.synchronization2 = VK_TRUE;
	vk::PhysicalDeviceScalarBlockLayoutFeatures scalarBlockFeatures;
	scalarBlockFeatures.scalarBlockLayout = VK_TRUE;
	vk::PhysicalDeviceShaderClockFeaturesKHR clockFeatures;
	clockFeatures.shaderDeviceClock = VK_TRUE;
	clockFeatures.shaderSubgroupClock = VK_TRUE;
	
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&descIndexingFeatures).add_pNext(&shaderDrawParametersFeatures)
		.add_pNext(&accelFeatures).add_pNext(&rayQueryFeatures).add_pNext(&rtPipelineFeatures).add_pNext(&addressFeatures)
		.add_pNext(&dynamicRenderingFeatures).add_pNext(&syncFeatures).add_pNext(&scalarBlockFeatures).add_pNext(&clockFeatures)
		.build().value();

	// get vk::Device handle for use in the rest of the application
	_chosenGPU = physicalDevice.physical_device;
	_device = vkbDevice.device;

	_gpuProperties = vk::PhysicalDeviceProperties2(physicalDevice.properties);

	// get Graphics-capable queue using VkBootstrap
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initialize Vulkan memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_device);
}


void RenderSystem::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR) // set vsync mode
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;

	std::vector<VkImage> swchImages = vkbSwapchain.get_images().value();
	_swapchainImages = std::vector<vk::Image>(swchImages.begin(), swchImages.end());
	std::vector<VkImageView> swchImageViews = vkbSwapchain.get_image_views().value();
	_swapchainImageViews = std::vector<vk::ImageView>(swchImageViews.begin(), swchImageViews.end());

	_mainDeletionQueue.push_function([=]() {
		for (auto& view : _swapchainImageViews)
		{
			_device.destroyImageView(view);
		}
	});

	_swapchainImageFormat = static_cast<vk::Format>(vkbSwapchain.image_format);
	_intermediateImageFormat = vk::Format::eR16G16B16A16Sfloat;

	vk::ImageCreateInfo intermediateImageCreateInfo;

	intermediateImageCreateInfo = vkinit::image_create_info(_intermediateImageFormat,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
		vk::ImageUsageFlagBits::eTransferSrc, _windowExtent3D, 1);

	AllocatedImage intermediateImage = create_image(intermediateImageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
	_intermediateImage = intermediateImage._image;

	vk::ImageViewCreateInfo intermediateViewCreateInfo;
	intermediateViewCreateInfo = vkinit::image_view_create_info(_intermediateImageFormat,
		intermediateImage._image, vk::ImageAspectFlagBits::eColor, 1);

	_intermediateImageView = _device.createImageView(intermediateViewCreateInfo);


	AllocatedImage prevFrameImage;

	auto prevFrameImageCreateInfo = vkinit::image_create_info(_intermediateImageFormat,
		vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
		_windowExtent3D, 1, vk::SampleCountFlagBits::e1, ImageType::eRTXOutput);
	prevFrameImage = create_image(prevFrameImageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);

	_prevFrameImage = prevFrameImage._image;

	vk::ImageViewCreateInfo prevFrameViewCreateInfo = vkinit::image_view_create_info(_intermediateImageFormat,
		prevFrameImage._image, vk::ImageAspectFlagBits::eColor, 1);
	_prevFrameImageView = _device.createImageView(prevFrameViewCreateInfo);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(_allocator, intermediateImage._image, intermediateImage._allocation);
		_device.destroyImageView(_intermediateImageView);
		vmaDestroyImage(_allocator, prevFrameImage._image, prevFrameImage._allocation);
		_device.destroyImageView(_prevFrameImageView);
	});

	_mainDeletionQueue.push_function([=]() {
		_device.destroySwapchainKHR(_swapchain);
	});

	_depthFormat = vk::Format::eD32Sfloat;
}


void RenderSystem::image_layout_transition(vk::CommandBuffer cmd, vk::AccessFlags srcAccessMask,
	vk::AccessFlags dstAccessMask, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::Image image,
	vk::ImageAspectFlags aspectMask, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask)
{
	vk::ImageMemoryBarrier transition;
	transition.dstAccessMask = dstAccessMask;
	transition.srcAccessMask = srcAccessMask;
	transition.oldLayout = oldLayout;
	transition.newLayout = newLayout;
	transition.image = image;

	transition.subresourceRange.aspectMask = aspectMask;
	transition.subresourceRange.levelCount = 1;
	transition.subresourceRange.layerCount = 1;

	cmd.pipelineBarrier(srcStageMask, dstStageMask, {}, {}, {}, transition);
}


void RenderSystem::switch_intermediate_image_layout(vk::CommandBuffer cmd, bool beforeRendering)
{
	if (beforeRendering)
	{
		vk::ImageMemoryBarrier transferToWritable;
		transferToWritable.dstAccessMask = (_renderMode == RenderMode::eHybrid) ? vk::AccessFlagBits::eColorAttachmentWrite :
			vk::AccessFlagBits::eShaderWrite;
		transferToWritable.oldLayout = vk::ImageLayout::eUndefined;
		transferToWritable.newLayout = (_renderMode == RenderMode::eHybrid) ? vk::ImageLayout::eColorAttachmentOptimal : 
			vk::ImageLayout::eGeneral;
		transferToWritable.image = _intermediateImage;

		transferToWritable.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		transferToWritable.subresourceRange.levelCount = 1;
		transferToWritable.subresourceRange.layerCount = 1;

		if (_renderMode == RenderMode::eHybrid)
		{
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
				{}, {}, {}, transferToWritable);
		}
		else
		{
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
				{}, {}, {}, transferToWritable);
		}
	}
	else
	{
		vk::ImageMemoryBarrier transferToSamplable;
		transferToSamplable.srcAccessMask = (_renderMode == RenderMode::eHybrid) ? vk::AccessFlagBits::eColorAttachmentWrite :
			vk::AccessFlagBits::eShaderWrite;
		transferToSamplable.oldLayout = vk::ImageLayout::eUndefined;
		transferToSamplable.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		transferToSamplable.image = _intermediateImage;

		transferToSamplable.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		transferToSamplable.subresourceRange.levelCount = 1;
		transferToSamplable.subresourceRange.layerCount = 1;

		if (_renderMode == RenderMode::eHybrid)
		{
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eVertexShader,
				{}, {}, {}, transferToSamplable);
		}
		else
		{
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR, vk::PipelineStageFlagBits::eTopOfPipe,
				{}, {}, {}, transferToSamplable);
		}
	}
}


void RenderSystem::switch_swapchain_image_layout(vk::CommandBuffer cmd, uint32_t swapchainImageIndex, bool beforeRendering)
{
	if (beforeRendering)
	{
		vk::ImageMemoryBarrier transferToWritable;
		transferToWritable.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		transferToWritable.oldLayout = vk::ImageLayout::eUndefined;
		transferToWritable.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		transferToWritable.image = _swapchainImages[swapchainImageIndex];

		transferToWritable.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		transferToWritable.subresourceRange.levelCount = 1;
		transferToWritable.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, {}, {}, transferToWritable);
	}
	else
	{
		vk::ImageMemoryBarrier transferToPresentable;
		transferToPresentable.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		transferToPresentable.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
		transferToPresentable.newLayout = vk::ImageLayout::ePresentSrcKHR;
		transferToPresentable.image = _swapchainImages[swapchainImageIndex];

		transferToPresentable.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		transferToPresentable.subresourceRange.levelCount = 1;
		transferToPresentable.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
			{}, {}, {}, transferToPresentable);
	}
}


void RenderSystem::switch_frame_image_layout(vk::Image image, vk::CommandBuffer cmd)
{
	vk::ImageMemoryBarrier transferToWritable;
	transferToWritable.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
	transferToWritable.oldLayout = vk::ImageLayout::eUndefined;
	transferToWritable.newLayout = vk::ImageLayout::eGeneral;
	transferToWritable.image = image;

	transferToWritable.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	transferToWritable.subresourceRange.levelCount = 1;
	transferToWritable.subresourceRange.layerCount = 1;

	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR,
		{}, {}, {}, transferToWritable);
}


void RenderSystem::memory_barrier(vk::CommandBuffer cmd, vk::AccessFlags2 srcMask, vk::AccessFlags2 dstMask,
	vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage)
{
	vk::MemoryBarrier2 barrier;
	barrier.srcAccessMask = srcMask;
	barrier.dstAccessMask = dstMask;
	barrier.srcStageMask = srcStage;
	barrier.dstStageMask = dstStage;

	vk::DependencyInfo depInfo;
	depInfo.setMemoryBarriers(barrier);

	cmd.pipelineBarrier2(depInfo);
}


void RenderSystem::buffer_memory_barrier(vk::CommandBuffer cmd, vk::Buffer buffer, vk::AccessFlags2 srcMask, vk::AccessFlags2 dstMask,
	vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage)
{
	vk::BufferMemoryBarrier2 barrier;
	barrier.srcAccessMask = srcMask;
	barrier.dstAccessMask = dstMask;
	barrier.srcStageMask = srcStage;
	barrier.dstStageMask = dstStage;
	barrier.buffer = buffer;
	barrier.size = VK_WHOLE_SIZE;

	vk::DependencyInfo depInfo;
	depInfo.setBufferMemoryBarriers(barrier);

	cmd.pipelineBarrier2(depInfo);
}


FrameData& RenderSystem::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}


void RenderSystem::init_commands()
{
	vk::CommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);

	_uploadContext._commandPool = _device.createCommandPool(uploadCommandPoolInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyCommandPool(_uploadContext._commandPool);
	});

	// default instant commands buffer

	vk::CommandBufferAllocateInfo upCmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);

	_uploadContext._commandBuffer = _device.allocateCommandBuffers(upCmdAllocInfo)[0];

	// create a command pool for rendering

	vk::CommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	
	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		_frames[i]._commandPool = _device.createCommandPool(commandPoolInfo);

		vk::CommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		_frames[i]._mainCommandBuffer = _device.allocateCommandBuffers(cmdAllocInfo)[0];

		_mainDeletionQueue.push_function([=]() {
			_device.destroyCommandPool(_frames[i]._commandPool);
		});
	}
}


void RenderSystem::init_gbuffer_attachments()
{
	// define attachment formats
	vk::Format positionFormat = vk::Format::eR32G32B32A32Sfloat;
	vk::Format normalFormat = vk::Format::eR32G32B32A32Sfloat;
	vk::Format albedoFormat = vk::Format::eR16G16B16A16Sfloat;
	vk::Format metallicRoughnessFormat = vk::Format::eR32G32B32A32Sfloat;

	// color attachments
	vk::RenderingAttachmentInfo positionAttachment;
	create_attachment(positionFormat, vk::ImageUsageFlagBits::eColorAttachment, positionAttachment,
		&_gBufferImages[GBUFFER_POSITION_SLOT]);

	vk::RenderingAttachmentInfo normalAttachment;
	create_attachment(normalFormat, vk::ImageUsageFlagBits::eColorAttachment, normalAttachment,
		&_gBufferImages[GBUFFER_NORMAL_SLOT]);

	vk::RenderingAttachmentInfo albedoAttachment;
	create_attachment(albedoFormat, vk::ImageUsageFlagBits::eColorAttachment, albedoAttachment,
		&_gBufferImages[GBUFFER_ALBEDO_SLOT]);

	vk::RenderingAttachmentInfo metallicRoughnessAttachment;
	create_attachment(metallicRoughnessFormat, vk::ImageUsageFlagBits::eColorAttachment, metallicRoughnessAttachment,
		&_gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT]);

	// depth attachment
	create_attachment(_depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment, _gBufferDepthAttachment,
		&_depthImage);

	_gBufferColorAttachments = {
		positionAttachment, normalAttachment, albedoAttachment, metallicRoughnessAttachment
	};

	_geometryPassInfo.setColorAttachments(_gBufferColorAttachments);
	_geometryPassInfo.setPDepthAttachment(&_gBufferDepthAttachment);
	_geometryPassInfo.layerCount = 1;
	_geometryPassInfo.renderArea.extent = _windowExtent;

	_colorAttachmentFormats = {
		positionFormat, normalFormat, albedoFormat, metallicRoughnessFormat
	};

	_geometryPassPipelineInfo.setColorAttachmentFormats(_colorAttachmentFormats);
	_geometryPassPipelineInfo.setDepthAttachmentFormat(_depthFormat);

	vk::ImageCreateInfo lightingDepthImageInfo = vkinit::image_create_info(_depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
		_windowExtent3D, 1);

	_lightingDepthImage = create_image(lightingDepthImageInfo, VMA_MEMORY_USAGE_GPU_ONLY);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(_allocator, _lightingDepthImage._image, _lightingDepthImage._allocation);
	});

	vk::ImageViewCreateInfo lightingDepthViewInfo = vkinit::image_view_create_info(_depthFormat, _lightingDepthImage._image,
		vk::ImageAspectFlagBits::eDepth, 1);

	_lightingDepthImageView = _device.createImageView(lightingDepthViewInfo);

	vk::ImageViewCreateInfo depthViewInfo = vkinit::image_view_create_info(_depthFormat, _depthImage,
		vk::ImageAspectFlagBits::eDepth, 1);
	_depthImageView = _device.createImageView(depthViewInfo);

	_lightingPassPipelineInfo.setColorAttachmentFormats(_intermediateImageFormat);
	_lightingPassPipelineInfo.setDepthAttachmentFormat(_depthFormat);


	// Path tracing GBuffer
	auto ptPositionsImageCreateInfo = vkinit::image_create_info(positionFormat,
		vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc, _windowExtent3D, 1, vk::SampleCountFlagBits::e1);
	auto ptPositionsImage = create_image(ptPositionsImageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
	_ptPositionImage = ptPositionsImage._image;

	vk::ImageViewCreateInfo ptPositionsViewCreateInfo = vkinit::image_view_create_info(positionFormat,
		ptPositionsImage._image, vk::ImageAspectFlagBits::eColor, 1);
	auto ptPositionsImageView = _device.createImageView(ptPositionsViewCreateInfo);

	vk::SamplerCreateInfo gBufferSamplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear,
		1.0, vk::SamplerAddressMode::eClampToEdge);

	vk::Sampler gBufferSampler = _device.createSampler(gBufferSamplerInfo);

	std::vector<Render::DescriptorManager::ImageInfo> ptPositionInfos(FRAME_OVERLAP);
	for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		ptPositionInfos[i].imageType = vk::DescriptorType::eStorageImage;
		ptPositionInfos[i].imageView = ptPositionsImageView;
		ptPositionInfos[i].layout = vk::ImageLayout::eGeneral;
		ptPositionInfos[i].sampler = gBufferSampler;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR,
		ptPositionInfos, 3, 1, false, true);


	auto prevPositionsImageCreateInfo = vkinit::image_create_info(positionFormat,
		vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, _windowExtent3D, 1, vk::SampleCountFlagBits::e1);
	auto prevPositionsImage = create_image(prevPositionsImageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
	_prevPositionImage = prevPositionsImage._image;

	vk::ImageViewCreateInfo prevPositionsViewCreateInfo = vkinit::image_view_create_info(positionFormat,
		prevPositionsImage._image, vk::ImageAspectFlagBits::eColor, 1);
	auto prevPositionsImageView = _device.createImageView(prevPositionsViewCreateInfo);

	std::vector<Render::DescriptorManager::ImageInfo> prevPosInfos(FRAME_OVERLAP);
	for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		prevPosInfos[i].imageType = vk::DescriptorType::eCombinedImageSampler;
		prevPosInfos[i].imageView = prevPositionsImageView;
		prevPosInfos[i].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
		prevPosInfos[i].sampler = gBufferSampler;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR,
		prevPosInfos, 4, 1, false, true);


	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(_lightingDepthImageView);
		_device.destroyImageView(_depthImageView);

		vmaDestroyImage(_allocator, ptPositionsImage._image, ptPositionsImage._allocation);
		_device.destroyImageView(ptPositionsImageView);

		vmaDestroyImage(_allocator, prevPositionsImage._image, prevPositionsImage._allocation);
		_device.destroyImageView(prevPositionsImageView);

		_device.destroySampler(gBufferSampler);
	});
}


void RenderSystem::create_attachment(vk::Format format, vk::ImageUsageFlagBits usage, vk::RenderingAttachmentInfo& attachmentInfo,
	vk::Image* image, vk::ImageView* imageView)
{
	vk::ImageAspectFlags aspectMask;
	vk::ImageLayout imageLayout = {};
	vk::ClearValue clearValue;

	if (usage & vk::ImageUsageFlagBits::eColorAttachment)
	{
		aspectMask = vk::ImageAspectFlagBits::eColor;
		imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	}

	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
	{
		aspectMask = vk::ImageAspectFlagBits::eDepth;
		if (format >= vk::Format::eD16UnormS8Uint)
		{
			aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}
		imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		clearValue.depthStencil = vk::ClearDepthStencilValue({ 1.0f, 0 });
	}

	vk::Extent3D extent;
	extent.width = _windowExtent.width;
	extent.height = _windowExtent.height;
	extent.depth = 1;

	vk::ImageCreateInfo imageInfo = vkinit::image_create_info(format, usage, extent, 1);
	imageInfo.usage |= vk::ImageUsageFlagBits::eSampled;

	AllocatedImage attachment = create_image(imageInfo, VMA_MEMORY_USAGE_GPU_ONLY);
	if (image)
	{
		*image = attachment._image;
	}


	vk::ImageViewCreateInfo viewCreateInfo = vkinit::image_view_create_info(format, attachment._image, aspectMask, 1);

	vk::ImageView attachmentView = _device.createImageView(viewCreateInfo);

	attachmentInfo.imageView = attachmentView;
	attachmentInfo.imageLayout = imageLayout;
	attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
	attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
	attachmentInfo.clearValue = clearValue;

	if (imageView)
	{
		*imageView = attachmentView;
	}

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(_allocator, attachment._image, attachment._allocation);
		_device.destroyImageView(attachmentView);
	});
}


void RenderSystem::init_prepass_attachments()
{
	create_attachment(_motionVectorFormat, vk::ImageUsageFlagBits::eColorAttachment, _motionVectorAttachment,
		&_motionVectorImage, &_motionVectorImageView);
}


void RenderSystem::init_raytracing()
{
	_gpuProperties.pNext = &_rtProperties;
	_chosenGPU.getProperties2(&_gpuProperties);
}


void RenderSystem::init_sync_structures()
{
	vk::FenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();

	_uploadContext._uploadFence = _device.createFence(uploadFenceCreateInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyFence(_uploadContext._uploadFence);
	});

	// create in the signaled state, so that the first wait call returns immediately
	vk::FenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(vk::FenceCreateFlagBits::eSignaled);
	vk::SemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
	
	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		_frames[i]._renderFence = _device.createFence(fenceCreateInfo);

		_mainDeletionQueue.push_function([=]() {
			_device.destroyFence(_frames[i]._renderFence);
		});

		_frames[i]._renderSemaphore = _device.createSemaphore(semaphoreCreateInfo);
		_frames[i]._presentSemaphore = _device.createSemaphore(semaphoreCreateInfo);

		_mainDeletionQueue.push_function([=]() {
			_device.destroySemaphore(_frames[i]._renderSemaphore);
			_device.destroySemaphore(_frames[i]._presentSemaphore);
		});
	}
}


void RenderSystem::init_descriptors()
{
	const size_t camSceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(CameraDataGPU) + sizeof(LightingData));

	_camSceneBuffer = create_buffer(camSceneParamBufferSize, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_CPU_TO_GPU);

	Render::DescriptorManager::BufferInfo camSceneBufferInfo;
	camSceneBufferInfo.buffer = _camSceneBuffer._buffer;
	camSceneBufferInfo.bufferType = vk::DescriptorType::eUniformBufferDynamic;
	camSceneBufferInfo.offset = 0;
	camSceneBufferInfo.range = sizeof(CameraDataGPU) + sizeof(LightingData);

	_descMng.register_buffer(Render::RegisteredDescriptorSet::eGlobal, vk::ShaderStageFlagBits::eVertex |
		vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
		{ camSceneBufferInfo }, 0);

	std::vector<Render::DescriptorManager::BufferInfo> objectBufferInfos(FRAME_OVERLAP);

	vk::SamplerCreateInfo gBufferSamplerInfo = vkinit::sampler_create_info(vk::Filter::eNearest, vk::Filter::eNearest,
		1.0, vk::SamplerAddressMode::eClampToEdge);

	vk::Sampler gBufferSampler = _device.createSampler(gBufferSamplerInfo);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _camSceneBuffer._buffer, _camSceneBuffer._allocation);
		_device.destroySampler(gBufferSampler);
	});

	std::vector<Render::DescriptorManager::ImageInfo> postprocessInfos(FRAME_OVERLAP);

	Render::DescriptorManager::ImageInfo gBufferImageInfo;
	gBufferImageInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	gBufferImageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	gBufferImageInfo.sampler = gBufferSampler;

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		constexpr int MAX_OBJECTS = 10000;
		_frames[i]._objectBuffer = create_buffer(sizeof(ObjectData) * MAX_OBJECTS,
			vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);

		_mainDeletionQueue.push_function([=]() {
			vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(_frames[i]._objectBuffer._buffer),
			_frames[i]._objectBuffer._allocation);
		});

		objectBufferInfos[i].buffer = _frames[i]._objectBuffer._buffer;
		objectBufferInfos[i].bufferType = vk::DescriptorType::eStorageBuffer;
		objectBufferInfos[i].offset = 0;
		objectBufferInfos[i].range = sizeof(ObjectData) * MAX_OBJECTS;

		postprocessInfos[i] = gBufferImageInfo;
		postprocessInfos[i].imageView = _intermediateImageView;
	}

	Render::DescriptorManager::ImageInfo positionInfo;
	Render::DescriptorManager::ImageInfo normalInfo;
	Render::DescriptorManager::ImageInfo albedoInfo;
	Render::DescriptorManager::ImageInfo metallicRoughnessInfo;

	positionInfo = gBufferImageInfo;
	positionInfo.imageView = _gBufferColorAttachments[GBUFFER_POSITION_SLOT].imageView;

	normalInfo = gBufferImageInfo;
	normalInfo.imageView = _gBufferColorAttachments[GBUFFER_NORMAL_SLOT].imageView;

	albedoInfo = gBufferImageInfo;
	albedoInfo.imageView = _gBufferColorAttachments[GBUFFER_ALBEDO_SLOT].imageView;

	metallicRoughnessInfo = gBufferImageInfo;
	metallicRoughnessInfo.imageView = _gBufferColorAttachments[GBUFFER_METALLIC_ROUGHNESS_SLOT].imageView;

	_descMng.register_buffer(Render::RegisteredDescriptorSet::eObjects, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR, objectBufferInfos, 0, 1, true);

	_descMng.register_image(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { positionInfo },
		GBUFFER_POSITION_SLOT);
	_descMng.register_image(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { normalInfo },
		GBUFFER_NORMAL_SLOT);
	_descMng.register_image(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { albedoInfo },
		GBUFFER_ALBEDO_SLOT);
	_descMng.register_image(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { metallicRoughnessInfo },
		GBUFFER_METALLIC_ROUGHNESS_SLOT);

	_descMng.register_image(Render::RegisteredDescriptorSet::ePostprocess, vk::ShaderStageFlagBits::eVertex |
		vk::ShaderStageFlagBits::eFragment, postprocessInfos, 0, 1, false, true);

}


void RenderSystem::init_pipelines()
{
	Model* scene = get_model("scene");

	vk::PipelineLayoutCreateInfo texturedPipelineLayoutInfo;

	Render::DescriptorSetFlags texturedPipelineUsedDescs = Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eObjects |
		Render::DescriptorSetFlagBits::eDiffuseTextures | Render::DescriptorSetFlagBits::eMetallicTextures |
		Render::DescriptorSetFlagBits::eRoughnessTextures | Render::DescriptorSetFlagBits::eNormalMapTextures | Render::DescriptorSetFlagBits::eTLAS;

	auto texSetLayouts = _descMng.get_layouts(texturedPipelineUsedDescs);

	texturedPipelineLayoutInfo.setSetLayouts(texSetLayouts);

	vk::PipelineLayout texturedPipelineLayout;
	texturedPipelineLayout = _device.createPipelineLayout(texturedPipelineLayoutInfo);

	vk::PipelineLayoutCreateInfo skyboxPipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	vk::PushConstantRange skyboxPushConstants;
	skyboxPushConstants.offset = 0;
	skyboxPushConstants.size = sizeof(MeshPushConstants);
	skyboxPushConstants.stageFlags = vk::ShaderStageFlagBits::eVertex;

	skyboxPipelineLayoutInfo.setPushConstantRanges(skyboxPushConstants);
	
	Render::DescriptorSetFlags skyboxPipelineUsedDescs = Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eSkyboxTextures;

	auto skyboxSetLayouts = _descMng.get_layouts(skyboxPipelineUsedDescs);

	skyboxPipelineLayoutInfo.setSetLayouts(skyboxSetLayouts);

	vk::PipelineLayout skyboxPipelineLayout = _device.createPipelineLayout(skyboxPipelineLayoutInfo);

	// build stage_create_info for vertex and fragment stages
	// this lets pipeline know about shader modules
	PipelineBuilder pipelineBuilder;

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(vk::PrimitiveTopology::eTriangleList);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = static_cast<float>(_windowExtent.width);
	pipelineBuilder._viewport.height = static_cast<float>(_windowExtent.height);
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;
	pipelineBuilder._scissor.offset = vk::Offset2D{ 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	vk::PipelineRenderingCreateInfo texRenderingCreateInfo;
	texRenderingCreateInfo.setColorAttachmentFormats(_intermediateImageFormat);
	texRenderingCreateInfo.setDepthAttachmentFormat(_depthFormat);

	pipelineBuilder._renderingCreateInfo = texRenderingCreateInfo;

	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(vk::PolygonMode::eFill);
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info(vk::SampleCountFlagBits::e1);
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, vk::CompareOp::eLessOrEqual);

	// build mesh pipeline 

	VertexInputDescription vertexDescription;
	vertexDescription.construct_from_vertex();

	// use the vertex info with the pipeline builder
	pipelineBuilder._vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);

	pipelineBuilder._vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

	pipelineBuilder._pipelineLayout = texturedPipelineLayout;
	
	// init geometry pass pipeline

	Render::Shader geometryPassVertexShader;
	geometryPassVertexShader.create(&_device, "geometry_pass.vert", vk::ShaderStageFlagBits::eVertex);

	Render::Shader geometryPassFragmentShader;
	geometryPassFragmentShader.create(&_device, "geometry_pass.frag", vk::ShaderStageFlagBits::eFragment);

	pipelineBuilder._renderingCreateInfo = _geometryPassPipelineInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(geometryPassVertexShader.get_stage_create_info());
	pipelineBuilder._shaderStages.push_back(geometryPassFragmentShader.get_stage_create_info());

	vk::Pipeline geometryPassPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(geometryPassPipeline, texturedPipelineLayout, PipelineType::eGeometryPass, texturedPipelineUsedDescs);

	// init lighting pass pipeline

	Render::Shader lightingPassVertexShader;
	lightingPassVertexShader.create(&_device, "lighting_pass.vert", vk::ShaderStageFlagBits::eVertex);

	Render::Shader lightingPassFragmentShader;
	lightingPassFragmentShader.create(&_device, "lighting_pass.frag", vk::ShaderStageFlagBits::eFragment);


	vk::PipelineLayoutCreateInfo lightingPassPipelineLayoutInfo;

	Render::DescriptorSetFlags lightingPassDescFlags = Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eGBuffer |
		Render::DescriptorSetFlagBits::eTLAS;

	auto lightingPassSetLayouts = _descMng.get_layouts(lightingPassDescFlags);

	lightingPassPipelineLayoutInfo.setSetLayouts(lightingPassSetLayouts);

	auto lightingPassPipelineLayout = _device.createPipelineLayout(lightingPassPipelineLayoutInfo);

	pipelineBuilder._pipelineLayout = lightingPassPipelineLayout;

	pipelineBuilder._renderingCreateInfo = _lightingPassPipelineInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(lightingPassVertexShader.get_stage_create_info());
	pipelineBuilder._shaderStages.push_back(lightingPassFragmentShader.get_stage_create_info());

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	auto lightingPassPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(lightingPassPipeline, lightingPassPipelineLayout, PipelineType::eLightingPass, lightingPassDescFlags);


	// init skybox pipeline

	Render::Shader skyboxVertShader;
	skyboxVertShader.create(&_device, "skybox.vert", vk::ShaderStageFlagBits::eVertex);

	Render::Shader skyboxFragShader;
	skyboxFragShader.create(&_device, "skybox.frag", vk::ShaderStageFlagBits::eFragment);

	// use the vertex info with the pipeline builder
	pipelineBuilder._vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);

	pipelineBuilder._vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

	pipelineBuilder._renderingCreateInfo = texRenderingCreateInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(skyboxVertShader.get_stage_create_info());
	pipelineBuilder._shaderStages.push_back(skyboxFragShader.get_stage_create_info());
	pipelineBuilder._pipelineLayout = skyboxPipelineLayout;

	vk::Pipeline skyboxPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(skyboxPipeline, skyboxPipelineLayout, PipelineType::eSkybox, skyboxPipelineUsedDescs);

	// init postprocessing pass pipeline

	Render::Shader postPassVertexShader;
	postPassVertexShader.create(&_device, "postprocess.vert", vk::ShaderStageFlagBits::eVertex);

	Render::Shader fxaaFragmentShader;
	fxaaFragmentShader.create(&_device, "fxaa.frag", vk::ShaderStageFlagBits::eFragment);

	Render::Shader denoiserFragmentShader;
	denoiserFragmentShader.create(&_device, "morrone_denoiser.frag", vk::ShaderStageFlagBits::eFragment);


	vk::PipelineLayoutCreateInfo postPassPipelineLayoutInfo;

	Render::DescriptorSetFlags postPassDescFlags = Render::DescriptorSetFlagBits::ePostprocess;

	auto postPassSetLayouts = _descMng.get_layouts(postPassDescFlags);

	postPassPipelineLayoutInfo.setSetLayouts(postPassSetLayouts);

	vk::PushConstantRange postPushConstants;
	postPushConstants.offset = 0;
	postPushConstants.size = sizeof(static_cast<int32_t>(_cfg.FXAA));
	postPushConstants.stageFlags = vk::ShaderStageFlagBits::eFragment;

	postPassPipelineLayoutInfo.setPushConstantRanges(postPushConstants);

	auto postPassPipelineLayout = _device.createPipelineLayout(postPassPipelineLayoutInfo);

	pipelineBuilder._pipelineLayout = postPassPipelineLayout;

	vk::PipelineRenderingCreateInfo postPassPipelineRenderingInfo;
	postPassPipelineRenderingInfo.setColorAttachmentFormats(_swapchainImageFormat);
	postPassPipelineRenderingInfo.setDepthAttachmentFormat(_depthFormat);

	pipelineBuilder._renderingCreateInfo = postPassPipelineRenderingInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(postPassVertexShader.get_stage_create_info());
	if (_renderMode == RenderMode::eHybrid)
	{
		pipelineBuilder._shaderStages.push_back(fxaaFragmentShader.get_stage_create_info());
	}
	else
	{
		pipelineBuilder._shaderStages.push_back(denoiserFragmentShader.get_stage_create_info());
	}
	

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	auto postPassPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(postPassPipeline, postPassPipelineLayout, PipelineType::ePostprocess, postPassDescFlags);

	// init motion vector pass pipeline

	Render::Shader motionVectorFragmentShader;
	motionVectorFragmentShader.create(&_device, "motion_vectors.frag", vk::ShaderStageFlagBits::eFragment);

	vk::PipelineLayoutCreateInfo mvPipelineLayoutInfo;

	Render::DescriptorSetFlags mvDescFlags = Render::DescriptorSetFlagBits::eGlobal;
	auto mvSetLayouts = _descMng.get_layouts(mvDescFlags);

	mvPipelineLayoutInfo.setSetLayouts(mvSetLayouts);

	auto mvPassPipelineLayout = _device.createPipelineLayout(mvPipelineLayoutInfo);
	pipelineBuilder._pipelineLayout = mvPassPipelineLayout;

	vk::PipelineRenderingCreateInfo mvPipelineRenderingInfo;
	mvPipelineRenderingInfo.setColorAttachmentFormats(_motionVectorFormat);
	mvPipelineRenderingInfo.setDepthAttachmentFormat(_depthFormat);

	pipelineBuilder._renderingCreateInfo = mvPipelineRenderingInfo;
	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(postPassVertexShader.get_stage_create_info());
	pipelineBuilder._shaderStages.push_back(motionVectorFragmentShader.get_stage_create_info());

	auto mvPassPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(mvPassPipeline, mvPassPipelineLayout, PipelineType::eMotionVectors, mvDescFlags);

	// shader modules are now built into the pipelines, we don't need them anymore

	geometryPassVertexShader.destroy();
	geometryPassFragmentShader.destroy();
	lightingPassVertexShader.destroy();
	lightingPassFragmentShader.destroy();
	postPassVertexShader.destroy();
	fxaaFragmentShader.destroy();
	denoiserFragmentShader.destroy();
	motionVectorFragmentShader.destroy();
	skyboxVertShader.destroy();
	skyboxFragShader.destroy();

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipeline(geometryPassPipeline);
		_device.destroyPipeline(lightingPassPipeline);
		_device.destroyPipeline(skyboxPipeline);
		_device.destroyPipeline(postPassPipeline);
		_device.destroyPipeline(mvPassPipeline);
		
		_device.destroyPipelineLayout(texturedPipelineLayout);
		_device.destroyPipelineLayout(skyboxPipelineLayout);
		_device.destroyPipelineLayout(lightingPassPipelineLayout);
		_device.destroyPipelineLayout(postPassPipelineLayout);
		_device.destroyPipelineLayout(mvPassPipelineLayout);
	});
}


void RenderSystem::immediate_submit(std::function<void(vk::CommandBuffer cmd)>&& function, vk::CommandBuffer cmd)
{
	vk::CommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	
	cmd.begin(cmdBeginInfo);

	function(cmd);

	cmd.end();

	vk::SubmitInfo submitInfo = vkinit::submit_info(&cmd);

	_graphicsQueue.submit(submitInfo, _uploadContext._uploadFence);

	VK_CHECK(_device.waitForFences(_uploadContext._uploadFence, true, 9999999999));
	_device.resetFences(_uploadContext._uploadFence);
	
	_device.resetCommandPool(_uploadContext._commandPool);
}


void RenderSystem::load_meshes()
{
	Model suzanneModel;
	suzanneModel._parentScene = &_scene;
	suzanneModel.load_assimp("../../../assets/suzanne/Suzanne.gltf");

	for (Mesh& mesh : suzanneModel._meshes)
	{
		upload_mesh(mesh);
	}

	_scene._models["suzanne"] = suzanneModel;

	Model scene;
	scene._parentScene = &_scene;
	scene.load_assimp("../../../assets/sponza/Sponza.gltf");

	for (Mesh& mesh : scene._meshes)
	{
		upload_mesh(mesh);
	}

	_scene._models["scene"] = scene;

	Model cube;
	cube._parentScene = &_scene;
	cube.load_assimp("../../../assets/cube.gltf");

	for (Mesh& mesh : cube._meshes)
	{
		upload_mesh(mesh);
	}

	_scene._models["cube"] = cube;
}


void RenderSystem::init_blas()
{
	std::vector<BLASInput> blasInputs;

	for (size_t i = 0; i < _renderables.size() - 1; ++i)
	{
		blasInputs.emplace_back(convert_to_blas_input(*_renderables[i].mesh));
	}

	RenderRT::buildBlas(this, blasInputs);
}


void RenderSystem::init_tlas()
{
	std::vector<vk::AccelerationStructureInstanceKHR> tlas;
	tlas.reserve(_renderables.size() - 1);

	for (uint32_t i = 0; i < _renderables.size() - 1; ++i)
	{
		vk::AccelerationStructureInstanceKHR accelInst;
		accelInst.setTransform(RenderRT::convertToTransformKHR(_renderables[i].transformMatrix));
		accelInst.setInstanceCustomIndex(i);
		accelInst.setAccelerationStructureReference(RenderRT::getBlasDeviceAddress(this, i));
		accelInst.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
		accelInst.setMask(0xFF);

		tlas.emplace_back(accelInst);
	}

	RenderRT::buildTlas(this, tlas);

	_descMng.register_accel_structure(Render::RegisteredDescriptorSet::eTLAS, vk::ShaderStageFlagBits::eFragment,
		_topLevelAS._structure, 0);

	_descMng.register_accel_structure(Render::RegisteredDescriptorSet::eRTXGeneral, vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eClosestHitKHR, _topLevelAS._structure, 0);
}


void RenderSystem::init_rt_descriptors()
{
	vk::SamplerCreateInfo mvSamplerInfo = vkinit::sampler_create_info(vk::Filter::eNearest, vk::Filter::eNearest,
		1.0, vk::SamplerAddressMode::eClampToEdge);
	vk::Sampler mvSampler = _device.createSampler(mvSamplerInfo);

	vk::SamplerCreateInfo frameSamplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear,
		1.0, vk::SamplerAddressMode::eClampToEdge);
	vk::Sampler frameSampler = _device.createSampler(frameSamplerInfo);

	std::vector<Render::DescriptorManager::ImageInfo> outImageInfos(FRAME_OVERLAP);
	std::vector<Render::DescriptorManager::ImageInfo> mvInfos(FRAME_OVERLAP);
	std::vector<Render::DescriptorManager::ImageInfo> prevFrameInfos(FRAME_OVERLAP);

	for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		Render::DescriptorManager::ImageInfo fullFrameImage;
		fullFrameImage.imageType = vk::DescriptorType::eCombinedImageSampler;
		fullFrameImage.layout = vk::ImageLayout::eGeneral;
		fullFrameImage.sampler = frameSampler;

		outImageInfos[i] = fullFrameImage;
		outImageInfos[i].imageType = vk::DescriptorType::eStorageImage;
		outImageInfos[i].imageView = _intermediateImageView;

		mvInfos[i].imageType = vk::DescriptorType::eCombinedImageSampler;
		mvInfos[i].sampler = mvSampler;
		mvInfos[i].imageView = _motionVectorImageView;
		mvInfos[i].layout = vk::ImageLayout::eShaderReadOnlyOptimal;

		prevFrameInfos[i] = fullFrameImage;
		prevFrameInfos[i].layout = vk::ImageLayout::eGeneral;
		prevFrameInfos[i].imageView = _prevFrameImageView;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR, outImageInfos, 0,
		1, false, true);

	_descMng.register_image(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR, mvInfos, 1,
		1, false, true);

	_descMng.register_image(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR, prevFrameInfos, 2,
		1, false, true);

	_mainDeletionQueue.push_function([=]() {
		_device.destroySampler(mvSampler);
		_device.destroySampler(frameSampler);
	});
}


void RenderSystem::init_rt_pipeline()
{
	enum StageIndices
	{
		eRaygen,
		eMiss,
		eShadowMiss,
		eClosestHit,
		eAnyHit,
		eShaderGroupCount
	};

	std::array<vk::PipelineShaderStageCreateInfo, StageIndices::eShaderGroupCount> stages;

	// Raygen
	Render::Shader pathTracingRayGen;
	pathTracingRayGen.create(&_device, "path_tracing.rgen", vk::ShaderStageFlagBits::eRaygenKHR);

	stages[StageIndices::eRaygen] = pathTracingRayGen.get_stage_create_info();

	// Miss
	Render::Shader pathTracingMiss;
	pathTracingMiss.create(&_device, "path_tracing.rmiss", vk::ShaderStageFlagBits::eMissKHR);

	stages[StageIndices::eMiss] = pathTracingMiss.get_stage_create_info();

	// Shadow miss
	Render::Shader shadowMiss;
	shadowMiss.create(&_device, "trace_shadow.rmiss", vk::ShaderStageFlagBits::eMissKHR);

	stages[StageIndices::eShadowMiss] = shadowMiss.get_stage_create_info();

	// Closest hit
	Render::Shader pathTracingClosestHit;
	pathTracingClosestHit.create(&_device, "path_tracing.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);

	stages[StageIndices::eClosestHit] = pathTracingClosestHit.get_stage_create_info();

	// Any hit
	Render::Shader pathTracingAnyHit;
	pathTracingAnyHit.create(&_device, "path_tracing.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);

	stages[StageIndices::eAnyHit] = pathTracingAnyHit.get_stage_create_info();

	// Define shader groups
	vk::RayTracingShaderGroupCreateInfoKHR shaderGroupInfo;
	shaderGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

	// Raygen
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
	shaderGroupInfo.generalShader = StageIndices::eRaygen;
	_rtShaderGroups.push_back(shaderGroupInfo);

	// Miss
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
	shaderGroupInfo.generalShader = StageIndices::eMiss;
	_rtShaderGroups.push_back(shaderGroupInfo);

	// Shadow miss
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
	shaderGroupInfo.generalShader = StageIndices::eShadowMiss;
	_rtShaderGroups.push_back(shaderGroupInfo);

	// Closest hit and any hit
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
	shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.closestHitShader = StageIndices::eClosestHit;
	shaderGroupInfo.anyHitShader = StageIndices::eAnyHit;
	_rtShaderGroups.push_back(shaderGroupInfo);

	vk::PipelineLayoutCreateInfo rtPipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	vk::PushConstantRange rayPushConstants;
	rayPushConstants.offset = 0;
	rayPushConstants.size = sizeof(RayPushConstants);
	rayPushConstants.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

	rtPipelineLayoutInfo.setPushConstantRanges(rayPushConstants);

	Render::DescriptorSetFlags rtPipelineDescFlags = Render::DescriptorSetFlagBits::eRTXGeneral | Render::DescriptorSetFlagBits::eRTXPerFrame |
		Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eObjects | Render::DescriptorSetFlagBits::eDiffuseTextures |
		Render::DescriptorSetFlagBits::eMetallicTextures | Render::DescriptorSetFlagBits::eRoughnessTextures |
		Render::DescriptorSetFlagBits::eNormalMapTextures | Render::DescriptorSetFlagBits::eSkyboxTextures;

	std::vector<vk::DescriptorSetLayout> rtPipelineSetLayouts = _descMng.get_layouts(rtPipelineDescFlags);

	rtPipelineLayoutInfo.setSetLayouts(rtPipelineSetLayouts);
	auto rtPipelineLayout = _device.createPipelineLayout(rtPipelineLayoutInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipelineLayout(rtPipelineLayout);
	});

	vk::RayTracingPipelineCreateInfoKHR rtPipelineInfo;
	rtPipelineInfo.setStages(stages);
	rtPipelineInfo.setGroups(_rtShaderGroups);
	rtPipelineInfo.maxPipelineRayRecursionDepth = 2;
	rtPipelineInfo.layout = rtPipelineLayout;

	if (_rtProperties.maxRayRecursionDepth <= 1) {
		std::cout << "Device doesn't support ray recursion (maxRayRecursionDepth <= 1)" << std::endl;
		abort();
	}

	auto rtPipelineResVal = _device.createRayTracingPipelineKHR({}, {}, rtPipelineInfo);

	VK_CHECK(rtPipelineResVal.result);
	auto rtPipeline = rtPipelineResVal.value;
	create_material_set(rtPipeline, rtPipelineLayout, PipelineType::eRayTracing, rtPipelineDescFlags);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipeline(rtPipeline);
	});

	for (auto& stage : stages)
	{
		_device.destroyShaderModule(stage.module);
	}
}


void RenderSystem::init_shader_binding_table()
{
	uint32_t rmissCount = 2;
	uint32_t rchitCount = 1;
	uint32_t handleCount = 1 + rmissCount + rchitCount;
	uint32_t handleSize = _rtProperties.shaderGroupHandleSize;

	vk::DeviceSize alignedHandleSize = align_up(handleSize, _rtProperties.shaderGroupHandleAlignment);

	_rgenRegion.stride = align_up(alignedHandleSize, _rtProperties.shaderGroupBaseAlignment);
	_rgenRegion.size = _rgenRegion.stride; // for raygen size == stride
	_rmissRegion.stride = alignedHandleSize;
	_rmissRegion.size = align_up(rmissCount * alignedHandleSize, _rtProperties.shaderGroupBaseAlignment);
	_rchitRegion.stride = alignedHandleSize;
	_rchitRegion.size = align_up(rchitCount * alignedHandleSize, _rtProperties.shaderGroupBaseAlignment);

	MaterialSet* pRtMaterial = get_material_set(PipelineType::eRayTracing);

	// get shader group handles
	uint32_t dataSize = handleCount * handleSize;
	std::vector<uint8_t> handles(dataSize);
	VK_CHECK(_device.getRayTracingShaderGroupHandlesKHR(pRtMaterial->pipeline, 0, handleCount, dataSize, handles.data()));

	vk::DeviceSize sbtSize = _rgenRegion.size + _rmissRegion.size + _rchitRegion.size + _rcallRegion.size;
	_rtSbtBuffer = create_buffer(sbtSize, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | 
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eShaderBindingTableKHR, VMA_MEMORY_USAGE_CPU_TO_GPU);

	// find shader group device addresses
	vk::BufferDeviceAddressInfo addressInfo;
	addressInfo.buffer = _rtSbtBuffer._buffer;
	vk::DeviceAddress sbtAddress = _device.getBufferAddress(addressInfo);
	_rgenRegion.deviceAddress = sbtAddress;
	_rmissRegion.deviceAddress = sbtAddress + _rgenRegion.size;
	_rchitRegion.deviceAddress = sbtAddress + _rgenRegion.size + _rmissRegion.size;

	// helper lambda to retrieve ith handle data
	auto getHandle = [&](int i) {
		return handles.data() + i * handleSize;
	};

	// upload the SBT
	std::vector<uint8_t> sbtBufferRaw(sbtSize);
	uint8_t* pData = sbtBufferRaw.data();
	uint32_t handleIndex = 0;
	
	// copy raygen region
	std::memcpy(pData, getHandle(handleIndex++), handleSize);

	// copy miss region
	pData = sbtBufferRaw.data() + _rgenRegion.size;
	for (uint32_t c = 0; c < rmissCount; ++c)
	{
		std::memcpy(pData, getHandle(handleIndex++), handleSize);
		pData += _rmissRegion.stride;
	}

	// copy hit region
	pData = sbtBufferRaw.data() + _rgenRegion.size + _rmissRegion.size;
	for (uint32_t c = 0; c < rchitCount; ++c)
	{
		std::memcpy(pData, getHandle(handleIndex++), handleSize);
		pData += _rchitRegion.stride;
	}

	upload_buffer(sbtBufferRaw, _rtSbtBuffer, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eShaderBindingTableKHR);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _rtSbtBuffer._buffer, _rtSbtBuffer._allocation);
	});
}


void RenderSystem::init_ui()
{
	vk::DescriptorPoolSize poolSizes[] = { { vk::DescriptorType::eSampler, 1000 },
		{ vk::DescriptorType::eCombinedImageSampler, 1000 },
		{ vk::DescriptorType::eSampledImage, 1000 },
		{ vk::DescriptorType::eStorageImage, 1000 },
		{ vk::DescriptorType::eUniformTexelBuffer, 1000 },
		{ vk::DescriptorType::eStorageTexelBuffer, 1000 },
		{ vk::DescriptorType::eUniformBuffer, 1000 },
		{ vk::DescriptorType::eStorageBuffer, 1000 },
		{ vk::DescriptorType::eUniformBufferDynamic, 1000 },
		{ vk::DescriptorType::eStorageBufferDynamic, 1000 },
		{ vk::DescriptorType::eInputAttachment, 1000 } };

	vk::DescriptorPoolCreateInfo poolInfo = {};
	poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	poolInfo.maxSets = 1000;
	poolInfo.setPoolSizes(poolSizes);

	VkDescriptorPool imguiPool = _device.createDescriptorPool(poolInfo);

	ImGui::CreateContext();

	ImGui_ImplSDL3_InitForVulkan(_pWindow);

	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = _instance;
	initInfo.PhysicalDevice = _chosenGPU;
	initInfo.Device = _device;
	initInfo.Queue = _graphicsQueue;
	initInfo.DescriptorPool = imguiPool;
	initInfo.MinImageCount = FRAME_OVERLAP;
	initInfo.ImageCount = FRAME_OVERLAP;
	initInfo.UseDynamicRendering = true;

	initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = reinterpret_cast<VkFormat*>(&_swapchainImageFormat);


	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);

	ImGui_ImplVulkan_CreateFontsTexture();

	_mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		_device.destroyDescriptorPool(imguiPool);
	});
}


bool RenderSystem::load_material_texture(Texture& tex, const std::string& texName, const std::string& matName,
	uint32_t texSlot, bool generateMipmaps/* = true */, vk::Format format/* = vk::Format::eR8G8B8A8Srgb */)
{
	if (!RenderUtil::load_image_from_file(this, texName.data(), tex.image, generateMipmaps, format))
	{
		return false;
	}

	vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(format,
		tex.image._image, vk::ImageAspectFlagBits::eColor, tex.image._mipLevels);
	tex.imageView = _device.createImageView(imageViewInfo);

	_loadedTextures[matName][texSlot] = tex;

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(tex.imageView);
	});

	return true;
}


void RenderSystem::load_skybox(Texture& skybox, const std::string& directory)
{
	std::vector<std::string> files = {
		"px.png", "nx.png",
		"py.png", "ny.png",
		"pz.png", "nz.png"
	};

	for (size_t i = 0; i < files.size(); ++i)
	{
		files[i] = directory + files[i];
	}

	RenderUtil::load_cubemap_from_files(this, files, skybox.image);
}


void RenderSystem::load_images()
{
	Model* curModel = get_model("scene");

	vk::SamplerCreateInfo samplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear);

	vk::Sampler smoothSampler = _device.createSampler(samplerInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroySampler(smoothSampler);
	});

	Texture defaultTex = {};

	RenderUtil::load_image_from_file(this, "../../../assets/null-texture.png", defaultTex.image);

	vk::ImageViewCreateInfo defaultTexViewInfo = vkinit::image_view_create_info(vk::Format::eR8G8B8A8Srgb,
		defaultTex.image._image, vk::ImageAspectFlagBits::eColor, defaultTex.image._mipLevels);
	defaultTex.imageView = _device.createImageView(defaultTexViewInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(defaultTex.imageView);
	});


	Render::DescriptorManager::ImageInfo texInfo;
	texInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	texInfo.sampler = smoothSampler;
	texInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

	std::vector<Render::DescriptorManager::ImageInfo> diffuseInfos(_scene._diffuseTexNames.size());

	for (size_t i = 0; i < _scene._diffuseTexNames.size(); ++i)
	{
		Texture diffuse = {};

		if (!_scene._diffuseTexNames[i].empty())
		{
			bool loadRes = load_material_texture(diffuse, _scene._diffuseTexNames[i], _scene._matNames[i], DIFFUSE_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
		}
		
		diffuseInfos[i] = texInfo;
		diffuseInfos[i].imageView = _loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT].imageView;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eDiffuseTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eAnyHitKHR, diffuseInfos, 0, static_cast<uint32_t>(_scene._diffuseTexNames.size()),
		true);


	std::vector<Render::DescriptorManager::ImageInfo> metallicInfos(_scene._metallicTexNames.size());
	for (size_t i = 0; i < _scene._metallicTexNames.size(); ++i)
	{
		Texture metallic = {};

		if (!_scene._metallicTexNames[i].empty())
		{
			bool loadRes = load_material_texture(metallic, _scene._metallicTexNames[i], _scene._matNames[i], METALLIC_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
		}

		metallicInfos[i] = texInfo;
		metallicInfos[i].imageView = _loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT].imageView;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eMetallicTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR,
		metallicInfos, 0, static_cast<uint32_t>(_scene._metallicTexNames.size()), true);


	std::vector<Render::DescriptorManager::ImageInfo> roughnessInfos(_scene._roughnessTexNames.size());
	for (size_t i = 0; i < _scene._roughnessTexNames.size(); ++i)
	{
		Texture roughness = {};

		if (!_scene._roughnessTexNames[i].empty())
		{
			bool loadRes = load_material_texture(roughness, _scene._roughnessTexNames[i], _scene._matNames[i], ROUGHNESS_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
		}

		roughnessInfos[i] = texInfo;
		roughnessInfos[i].imageView = _loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT].imageView;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eRoughnessTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR, roughnessInfos, 0,
		static_cast<uint32_t>(_scene._roughnessTexNames.size()), true);


	std::vector<Render::DescriptorManager::ImageInfo> normalMapInfos(_scene._normalMapNames.size());
	for (size_t i = 0; i < _scene._normalMapNames.size(); ++i)
	{
		Texture normalMap = {};
		Texture defaultNormal = {};

		if (!_scene._normalMapNames[i].empty())
		{
			bool loadRes = load_material_texture(normalMap, _scene._normalMapNames[i], _scene._matNames[i], NORMAL_MAP_SLOT,
				true, vk::Format::eR32G32B32A32Sfloat);

			if (!loadRes)
			{
				RenderUtil::load_image_from_file(this, "../../../assets/null-normal.png", defaultNormal.image, true,
					vk::Format::eR32G32B32A32Sfloat);

				vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(vk::Format::eR32G32B32A32Sfloat,
					defaultNormal.image._image, vk::ImageAspectFlagBits::eColor, defaultNormal.image._mipLevels);
				defaultNormal.imageView = _device.createImageView(imageViewInfo);

				_loadedTextures[_scene._matNames[i]][NORMAL_MAP_SLOT] = defaultNormal;

				_mainDeletionQueue.push_function([=]() {
					_device.destroyImageView(defaultNormal.imageView);
				});
			}
		}
		else
		{
			RenderUtil::load_image_from_file(this, "../../../assets/null-normal.png", defaultNormal.image, true,
				vk::Format::eR32G32B32A32Sfloat);

			vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(vk::Format::eR32G32B32A32Sfloat,
				defaultNormal.image._image, vk::ImageAspectFlagBits::eColor, defaultNormal.image._mipLevels);
			defaultNormal.imageView = _device.createImageView(imageViewInfo);

			_loadedTextures[_scene._matNames[i]][NORMAL_MAP_SLOT] = defaultNormal;

			_mainDeletionQueue.push_function([=]() {
				_device.destroyImageView(defaultNormal.imageView);
			});
		}

		normalMapInfos[i] = texInfo;
		normalMapInfos[i].imageView = _loadedTextures[_scene._matNames[i]][NORMAL_MAP_SLOT].imageView;
	}

	_descMng.register_image(Render::RegisteredDescriptorSet::eNormalMapTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eClosestHitKHR |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR, normalMapInfos,
		0, static_cast<uint32_t>(_scene._normalMapNames.size()), true);


	load_skybox(_skybox, "../../../assets/skybox/");

	vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(vk::Format::eR8G8B8A8Srgb,
		_skybox.image._image, vk::ImageAspectFlagBits::eColor, _skybox.image._mipLevels, ImageType::eCubemap);
	_skybox.imageView = _device.createImageView(imageViewInfo);

	Render::DescriptorManager::ImageInfo skyboxInfo;
	skyboxInfo.imageView = _skybox.imageView;
	skyboxInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	skyboxInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	skyboxInfo.sampler = smoothSampler;

	_descMng.register_image(Render::RegisteredDescriptorSet::eSkyboxTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eMissKHR, { skyboxInfo }, 0);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(_skybox.imageView);
	});
}


void RenderSystem::reset_frame()
{
	_rayConstants.frame = -1;
}


void RenderSystem::update_frame()
{
	assert(_pCamera);

	const PlumeCamera& camera = *_pCamera;

	static glm::mat4 refCamMatrix = {};
	static float refFov = camera._zoom;
	static glm::vec3 refPosition{ 0.0f };

	const glm::mat4 m = camera.get_view_matrix();
	const float fov = camera._zoom;
	const glm::vec3 position = camera._position;

	if (std::memcmp(&refCamMatrix[0][0], &m[0][0], sizeof(glm::mat4)) != 0 || refFov != fov ||
		std::memcmp(&refPosition[0], &position[0], sizeof(glm::vec3)) != 0)
	{
		refCamMatrix = m;
		refFov = fov;
		refPosition = position;
		if (!_cfg.MOTION_VECTORS)
		{
			reset_frame();
		}
	}
	++_rayConstants.frame;
}


void RenderSystem::update_buffer_memory(const float* sourceData, size_t bufferSize,
	AllocatedBuffer& targetBuffer, vk::CommandBuffer* cmd, AllocatedBuffer* stagingBuffer/* = nullptr*/)
{
	if (targetBuffer._memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
	{
		memcpy(targetBuffer._allocationInfo.pMappedData, sourceData, bufferSize);
	}
	else
	{
		memcpy(stagingBuffer->_allocationInfo.pMappedData, sourceData, bufferSize);
		vmaFlushAllocation(_allocator, stagingBuffer->_allocation, 0, VK_WHOLE_SIZE);
		buffer_memory_barrier(*cmd, stagingBuffer->_buffer, vk::AccessFlagBits2::eHostWrite, vk::AccessFlagBits2::eTransferRead,
			vk::PipelineStageFlagBits2::eHost, vk::PipelineStageFlagBits2::eCopy);
		vk::BufferCopy bufCopy;
		bufCopy.srcOffset = 0;
		bufCopy.dstOffset = 0;
		bufCopy.size = bufferSize;
		cmd->copyBuffer(stagingBuffer->_buffer, targetBuffer._buffer, bufCopy);
	}
}


void RenderSystem::init_scene()
{
	RenderObject suzanne = {};
	suzanne.model = get_model("suzanne");
	suzanne.materialSet = get_material_set(PipelineType::eGeometryPass);
	suzanne.mesh = &(suzanne.model->_meshes.front());
	//glm::mat4 meshScale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(5.0f, 5.0f, 5.0f));
	glm::mat4 meshTranslate = glm::translate(glm::mat4{ 1.0f }, glm::vec3(2.8f, -8.0f, 0));
	//glm::mat4 meshRotate = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	suzanne.transformMatrix = meshTranslate;

	_renderables.push_back(suzanne);

	Model* pSceneModel = get_model("scene");
	if (!pSceneModel)
	{
		std::cout << "Plume::Can't load scene model." << std::endl;
		return;
	}

	glm::mat4 sceneTransform = glm::translate(glm::vec3{ 5, -10, 0 }) * glm::rotate(glm::radians(90.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4{ 1.0f }, glm::vec3(0.05f, 0.05f, 0.05f));

	MaterialSet* pSceneMatSet = get_material_set(PipelineType::eGeometryPass);

	for (Mesh& mesh : pSceneModel->_meshes)
	{
		RenderObject renderSceneMesh = {};
		renderSceneMesh.model = pSceneModel;
		renderSceneMesh.mesh = &mesh;
		renderSceneMesh.materialSet = pSceneMatSet;
		renderSceneMesh.transformMatrix = sceneTransform;

		_renderables.push_back(std::move(renderSceneMesh));
	}

	_skyboxObject.model = get_model("cube");
	_skyboxObject.materialSet = get_material_set(PipelineType::eSkybox);
	_skyboxObject.mesh = &(_skyboxObject.model->_meshes.front());
	glm::mat4 meshScale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(600.0f, 600.0f, 600.0f));
	//glm::mat4 meshTranslate = glm::translate(glm::mat4{ 1.0f }, glm::vec3(2.8f, -8.0f, 0));
	//glm::mat4 meshRotate = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	_skyboxObject.transformMatrix = meshScale;
}


void RenderSystem::upload_mesh(Mesh& mesh)
{
	vk::BufferUsageFlags vertexBufferUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
		vk::BufferUsageFlagBits::eStorageBuffer;

	upload_buffer(mesh._vertices, mesh._vertexBuffer, vertexBufferUsage);

	vk::BufferUsageFlags indexBufferUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
		vk::BufferUsageFlagBits::eStorageBuffer;

	upload_buffer(mesh._indices, mesh._indexBuffer, indexBufferUsage);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(mesh._vertexBuffer._buffer),
			mesh._vertexBuffer._allocation);
		vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(mesh._indexBuffer._buffer),
			mesh._indexBuffer._allocation);
	});
}


void RenderSystem::cleanup()
{
	if (!_isInitialized)
	{
		return;
	}

	--_frameNumber;
	VK_CHECK(_device.waitForFences(get_current_frame()._renderFence, true, 1000000000));
	++_frameNumber;

	_mainDeletionQueue.flush();

	vmaDestroyAllocator(_allocator);
	_device.destroy();
	_instance.destroySurfaceKHR(_surface);
	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	_instance.destroy();
}


void RenderSystem::render_frame()
{
	// wait until the GPU has finished rendering the last frame, with timeout of 1 second
	VK_CHECK(_device.waitForFences(get_current_frame()._renderFence, true, 1000000000));
	_device.resetFences(get_current_frame()._renderFence);

	// request image to draw to, 1 second timeout
	uint32_t swapchainImageIndex = _device.acquireNextImageKHR(_swapchain, 1000000000,
		get_current_frame()._presentSemaphore, {}).value;

	// we know that everything finished rendering, so we safely reset the command buffer and reuse it
	VK_CHECK( vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0) );

	vk::CommandBuffer cmd = get_current_frame()._mainCommandBuffer;

	// begin recording the command buffer, letting Vulkan know that we will submit cmd exactly once per frame
	vk::CommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.pInheritanceInfo = nullptr; // no secondary command buffers
	cmdBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	cmd.begin(cmdBeginInfo);

	vk::ClearValue clearValue;
	clearValue.color = vk::ClearColorValue({ 2.0f / 255.0f, 150.0f / 255.0f, 254.0f / 255.0f, 1.0f });

	vk::ClearValue mvClearValue;
	clearValue.color = vk::ClearColorValue({ 0.0f, 0.0f, 1.0f, 1.0f });

	vk::ClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	// dynamic rendering
	vk::RenderingAttachmentInfo intermediateColorAttachmentInfo = vkinit::rendering_attachment_info(
		_intermediateImageView, vk::ImageLayout::eColorAttachmentOptimal,
		vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, clearValue);

	vk::RenderingAttachmentInfo finalColorAttachmentInfo = vkinit::rendering_attachment_info(_swapchainImageViews[swapchainImageIndex],
		vk::ImageLayout::eColorAttachmentOptimal, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, clearValue);

	vk::RenderingAttachmentInfo cubeDepthAttachmentInfo = vkinit::rendering_attachment_info(_depthImageView,
		vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, depthClear);

	vk::RenderingInfo skyboxRenderInfo;
	skyboxRenderInfo.renderArea.extent = _windowExtent;
	skyboxRenderInfo.layerCount = 1;
	skyboxRenderInfo.setColorAttachments(intermediateColorAttachmentInfo);
	skyboxRenderInfo.setPDepthAttachment(&cubeDepthAttachmentInfo);


	vk::RenderingAttachmentInfo lightingDepthAttachmentInfo = vkinit::rendering_attachment_info(_lightingDepthImageView,
		vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, depthClear);

	// Motion vector pass
	vk::RenderingAttachmentInfo motionVectorAttachmentInfo = vkinit::rendering_attachment_info(_motionVectorImageView,
		vk::ImageLayout::eColorAttachmentOptimal, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, mvClearValue);

	vk::RenderingInfo mvPassRenderInfo;
	mvPassRenderInfo.renderArea.extent = _windowExtent;
	mvPassRenderInfo.layerCount = 1;
	mvPassRenderInfo.setColorAttachments(motionVectorAttachmentInfo);
	mvPassRenderInfo.setPDepthAttachment(&lightingDepthAttachmentInfo);

	// Lighting pass setup
	_lightingPassInfo.renderArea.extent = _windowExtent;
	_lightingPassInfo.layerCount = 1;
	_lightingPassInfo.setColorAttachments(intermediateColorAttachmentInfo);
	_lightingPassInfo.setPDepthAttachment(&lightingDepthAttachmentInfo);

	vk::RenderingInfo postPassRenderInfo;
	postPassRenderInfo.renderArea.extent = _windowExtent;
	postPassRenderInfo.layerCount = 1;
	postPassRenderInfo.setColorAttachments(finalColorAttachmentInfo);
	postPassRenderInfo.setPDepthAttachment(&lightingDepthAttachmentInfo);

	if (_renderMode == RenderMode::eHybrid)
	{
		std::sort(_renderables.begin(), _renderables.end());
	}

	switch_intermediate_image_layout(cmd, true);

	if (_renderMode == RenderMode::ePathTracing)
	{
		switch_frame_image_layout(_prevFrameImage, cmd);
	}
	
	// ========================================   RENDERING   ========================================

	upload_cam_scene_data(cmd, _renderables.data(), _renderables.size());

	if (_renderMode == RenderMode::eHybrid)
	{
		cmd.beginRendering(_geometryPassInfo);

		draw_objects(cmd, _renderables.data(), _renderables.size());

		cmd.endRendering();

		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, _gBufferImages[GBUFFER_POSITION_SLOT],
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader);

		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, _gBufferImages[GBUFFER_NORMAL_SLOT],
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader);

		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, _gBufferImages[GBUFFER_ALBEDO_SLOT],
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader);

		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, _gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT],
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader);

		cmd.beginRendering(_lightingPassInfo);

		draw_lighting_pass(cmd);

		cmd.endRendering();

		cmd.beginRendering(skyboxRenderInfo);

		draw_skybox(cmd, _skyboxObject);

		cmd.endRendering();

		image_layout_transition(cmd, vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, _intermediateImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eVertexShader);

		switch_intermediate_image_layout(cmd, false);

		switch_swapchain_image_layout(cmd, swapchainImageIndex, true);

		cmd.beginRendering(postPassRenderInfo);

		std::vector<vk::DescriptorSet> postPassSets;

		draw_screen_quad(cmd, PipelineType::ePostprocess);

		cmd.endRendering();
	}
	else if (_renderMode == RenderMode::ePathTracing)
	{
		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, _ptPositionImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR);

		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, _prevPositionImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR);

		cmd.beginRendering(mvPassRenderInfo);

		draw_screen_quad(cmd, PipelineType::eMotionVectors);

		cmd.endRendering();

		image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, _motionVectorImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eRayTracingShaderKHR);

		trace_rays(cmd, swapchainImageIndex);

		//image_layout_transition(cmd, {}, vk::AccessFlagBits::eMemoryWrite, vk::ImageLayout::eUndefined,
		//	vk::ImageLayout::eTransferDstOptimal, _swapchainImages[swapchainImageIndex], vk::ImageAspectFlagBits::eColor,
		//	vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eRayTracingShaderKHR);

		image_layout_transition(cmd, vk::AccessFlagBits::eShaderRead, {},
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal, _prevFrameImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eRayTracingShaderKHR, vk::PipelineStageFlagBits::eVertexShader);

		image_layout_transition(cmd, vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal, _intermediateImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eVertexShader);


		image_layout_transition(cmd, vk::AccessFlagBits::eShaderRead, {},
			vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal, _prevPositionImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eRayTracingShaderKHR, vk::PipelineStageFlagBits::eVertexShader);

		image_layout_transition(cmd, vk::AccessFlagBits::eShaderRead, {},
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal, _ptPositionImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eRayTracingShaderKHR, vk::PipelineStageFlagBits::eVertexShader);


		copy_image(cmd, vk::ImageAspectFlagBits::eColor, _intermediateImage, vk::ImageLayout::eTransferSrcOptimal,
			_prevFrameImage, vk::ImageLayout::eTransferDstOptimal, _windowExtent3D);

		copy_image(cmd, vk::ImageAspectFlagBits::eColor, _ptPositionImage, vk::ImageLayout::eTransferSrcOptimal,
			_prevPositionImage, vk::ImageLayout::eTransferDstOptimal, _windowExtent3D);

		image_layout_transition(cmd, vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, _intermediateImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eVertexShader);

		switch_intermediate_image_layout(cmd, false);

		switch_swapchain_image_layout(cmd, swapchainImageIndex, true);

		cmd.beginRendering(postPassRenderInfo);

		draw_screen_quad(cmd, PipelineType::ePostprocess);

		cmd.endRendering();
	}

	if (_showDebugUi)
	{
		draw_debug_ui(cmd, _swapchainImageViews[swapchainImageIndex]);
	}

	// ======================================== END RENDERING ========================================

	// transfer swapchain image to presentable layout

	switch_swapchain_image_layout(cmd, swapchainImageIndex, false);

	// stop recording to command buffer (we can no longer add commands, but it can now be submitted and executed)
	cmd.end();

	_prevCamera = *_pCamera;

	// prepare the vk::Queue submission
	// wait for the present semaphore to present image
	// signal the render semaphore, showing that rendering is finished

	vk::SubmitInfo submit = {};
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	
	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &get_current_frame()._presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &get_current_frame()._renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	// submit the buffer
	// render fence blocks until graphics commands are done
	_graphicsQueue.submit(submit, get_current_frame()._renderFence);

	// display image
	// wait on render semaphore so that rendered image is complete 

	vk::PresentInfoKHR presentInfo = {};
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK( _graphicsQueue.presentKHR(presentInfo) );

	++_frameNumber;
}


size_t RenderSystem::pad_uniform_buffer_size(size_t originalSize)
{
	// calculate alignment based on min device offset alignment
	size_t minUBOAlignment = _gpuProperties.properties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUBOAlignment > 0)
	{
		alignedSize = (alignedSize + minUBOAlignment - 1) & ~(minUBOAlignment - 1);
	}
	return alignedSize;
}


vk::DeviceSize RenderSystem::align_up(vk::DeviceSize originalSize, vk::DeviceSize alignment)
{
	vk::DeviceSize alignedSize = originalSize;
	if (alignment > 0)
	{
		alignedSize = (alignedSize + alignment - 1) & ~(alignment - 1);
	}
	return alignedSize;
}


AllocatedBuffer RenderSystem::create_buffer(size_t allocSize, vk::BufferUsageFlags usage, VmaMemoryUsage memUsage,
	VmaAllocationCreateFlags flags/* = 0 */, vk::MemoryPropertyFlags reqFlags/* = {}*/)
{
	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memUsage;
	vmaAllocInfo.flags = flags;
	vmaAllocInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(reqFlags);

	AllocatedBuffer newBuffer;

	VkBuffer cBuffer;
	auto cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
	VK_CHECK( vmaCreateBuffer(_allocator, &cBufferInfo, &vmaAllocInfo,
		&cBuffer, &newBuffer._allocation, &newBuffer._allocationInfo) );

	newBuffer._buffer = static_cast<vk::Buffer>(cBuffer);
	return newBuffer;
}


AllocatedImage RenderSystem::create_image(const vk::ImageCreateInfo& createInfo, VmaMemoryUsage memUsage)
{
	AllocatedImage newImage;

	newImage._mipLevels = createInfo.mipLevels;

	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = memUsage;

	VkImageCreateInfo imageInfoC = static_cast<VkImageCreateInfo>(createInfo);
	VkImage imageC = {};

	vmaCreateImage(_allocator, &imageInfoC, &imgAllocInfo, &imageC, &newImage._allocation, nullptr);

	newImage._image = imageC;
	return newImage;
}


void RenderSystem::copy_image(vk::CommandBuffer cmd, vk::ImageAspectFlags aspectMask, vk::Image srcImage,
	vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::Extent3D extent)
{
	vk::ImageCopy copyInfo;
	copyInfo.srcSubresource.aspectMask = aspectMask;
	copyInfo.srcSubresource.layerCount = 1;
	copyInfo.srcSubresource.mipLevel = 0;
	copyInfo.srcSubresource.baseArrayLayer = 0;
	copyInfo.dstSubresource = copyInfo.srcSubresource;
	copyInfo.extent = extent;
	copyInfo.srcOffset = { { 0, 0, 0 } };
	copyInfo.dstOffset = { { 0, 0, 0 } };

	cmd.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, copyInfo);
}


vk::DeviceAddress RenderSystem::get_buffer_device_address(vk::Buffer buffer)
{
	vk::BufferDeviceAddressInfo bufferAddressInfo;
	bufferAddressInfo.setBuffer(buffer);
	return _device.getBufferAddress(bufferAddressInfo);
}


BLASInput RenderSystem::convert_to_blas_input(Mesh& mesh)
{
	vk::DeviceAddress vertexAddress = get_buffer_device_address(mesh._vertexBuffer._buffer);
	vk::DeviceAddress indexAddress = get_buffer_device_address(mesh._indexBuffer._buffer);

	uint32_t maxVertices = static_cast<uint32_t>(mesh._indices.size());
	uint32_t maxPrimCount = static_cast<uint32_t>(mesh._indices.size()) / 3;

	BLASInput blasInput = {};

	blasInput._triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
	blasInput._triangles.vertexData.deviceAddress = vertexAddress;
	blasInput._triangles.vertexStride = sizeof(Vertex);

	blasInput._triangles.indexType = vk::IndexType::eUint32;
	blasInput._triangles.indexData = indexAddress;
	blasInput._triangles.maxVertex = maxVertices;

	blasInput._geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
	blasInput._geometry.flags = (_renderMode == RenderMode::ePathTracing) ?
		vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation : vk::GeometryFlagBitsKHR::eOpaque;
	blasInput._geometry.setGeometry(blasInput._triangles);

	blasInput._buildRangeInfo.firstVertex = 0;
	blasInput._buildRangeInfo.primitiveCount = maxPrimCount;
	blasInput._buildRangeInfo.primitiveOffset = 0;
	blasInput._buildRangeInfo.transformOffset = 0;

	return blasInput;
}


MaterialSet* RenderSystem::create_material_set(vk::Pipeline pipeline, vk::PipelineLayout layout, PipelineType pipelineType,
	Render::DescriptorSetFlags descSetFlags)
{
	MaterialSet matSet;
	matSet.pipeline = pipeline;
	matSet.pipelineLayout = layout;
	matSet.usedDescriptorSets = descSetFlags;

	auto setId = static_cast<size_t>(pipelineType);

	_materialSets[setId] = matSet;
	return &(_materialSets[setId]);
}


MaterialSet* RenderSystem::get_material_set(PipelineType pipelineType)
{
	auto setId = static_cast<size_t>(pipelineType);

	return &(_materialSets[setId]);
}


Model* RenderSystem::get_model(const std::string& name)
{
	auto it = _scene._models.find(name);
	if (it == _scene._models.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}


void RenderSystem::upload_cam_scene_data(vk::CommandBuffer cmd, RenderObject* first, size_t count)
{
	assert(_pCamera);
	assert(_pLightManager);

	const PlumeCamera& camera = *_pCamera;
	CameraDataGPU camDataGpu = camera.make_gpu_camera_data(_prevCamera, { _windowExtent.width, _windowExtent.height });

	char* data;

	vmaMapMemory(_allocator, _camSceneBuffer._allocation, (void**)&data);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	data += pad_uniform_buffer_size(sizeof(CameraDataGPU) + sizeof(LightingData)) * frameIndex;
	memcpy(data, &camDataGpu, sizeof(CameraDataGPU));
	data += sizeof(CameraDataGPU);

	LightingData lightingData = Render::LightManager::make_lighting_data(_pLightManager->GetLights());
	memcpy(data, &lightingData, sizeof(LightingData));

	vmaUnmapMemory(_allocator, _camSceneBuffer._allocation);


	void* objectData;

	vmaMapMemory(_allocator, get_current_frame()._objectBuffer._allocation, &objectData);

	ObjectData* objectSSBO = reinterpret_cast<ObjectData*>(objectData);

	for (int i = 0; i < count; ++i)
	{
		RenderObject& object = first[i];
		objectSSBO[i].model = object.transformMatrix;
		if (object.mesh)
		{
			uint64_t indexAddress = get_buffer_device_address(object.mesh->_indexBuffer._buffer);
			uint64_t vertexAddress = get_buffer_device_address(object.mesh->_vertexBuffer._buffer);
			objectSSBO[i].matIndex = object.mesh->_matIndex;
			objectSSBO[i].indexBufferAddress = indexAddress;
			objectSSBO[i].vertexBufferAddress = vertexAddress;
			objectSSBO[i].emittance = object.mesh->_emittance;
		}
	}

	vmaUnmapMemory(_allocator, get_current_frame()._objectBuffer._allocation);
}


void RenderSystem::draw_objects(vk::CommandBuffer cmd, RenderObject* first, size_t count)
{
	const PlumeCamera& camera = *_pCamera;

	glm::mat4 view = camera.get_view_matrix();

	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(camera._zoom),
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	Model* lastModel = nullptr;
	MaterialSet* lastMaterialSet = nullptr;
	for (int i = 0; i < count; ++i)
	{
		RenderObject& object = first[i];

		if (!object.model || !object.materialSet)
		{
			continue;
		}

		// don't bind already bound pipeline
		if (object.materialSet != lastMaterialSet)
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, object.materialSet->pipeline);
			lastMaterialSet = object.materialSet;

			// scene & camera descriptor
			uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(CameraDataGPU) +
				sizeof(LightingData)) * frameIndex);

			Render::DescriptorSetFlags descFlags = object.materialSet->usedDescriptorSets;

			auto pipelineDescriptorSets = _descMng.get_descriptor_sets(descFlags, frameIndex);

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 0,
				pipelineDescriptorSets, uniformOffset);
		}

		glm::mat4 model = object.transformMatrix;
		// final render matrix
		glm::mat4 mesh_matrix = projection * view * model;
		
		if (object.model != lastModel)
		{
			vk::DeviceSize offset = 0;
			cmd.bindVertexBuffers(0, object.mesh->_vertexBuffer._buffer, offset);
			cmd.bindIndexBuffer(object.mesh->_indexBuffer._buffer, offset, vk::IndexType::eUint32);
		}

		cmd.drawIndexed(static_cast<uint32_t>(object.mesh->_indices.size()), 1, 0, 0, i);
	}
}


void RenderSystem::draw_lighting_pass(vk::CommandBuffer cmd)
{
	MaterialSet* lightingMatSet = get_material_set(PipelineType::eLightingPass);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, lightingMatSet->pipeline);

	// scene & camera descriptor set
	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(CameraDataGPU) +
		sizeof(LightingData)) * frameIndex);

	Render::DescriptorSetFlags lightingDescFlags = lightingMatSet->usedDescriptorSets;

	auto lightingDescSets = _descMng.get_descriptor_sets(lightingDescFlags, frameIndex);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, lightingMatSet->pipelineLayout, 0,
		lightingDescSets, uniformOffset);

	cmd.draw(3, 1, 0, 0);
}


void RenderSystem::draw_screen_quad(vk::CommandBuffer cmd, PipelineType pipelineType)
{
	MaterialSet* screenQuadMaterial = get_material_set(pipelineType);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, screenQuadMaterial->pipeline);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(CameraDataGPU) +
		sizeof(LightingData)) * frameIndex);

	Render::DescriptorSetFlags quadDescFlags = screenQuadMaterial->usedDescriptorSets;

	auto quadDescSets = _descMng.get_descriptor_sets(quadDescFlags, frameIndex);

	if (quadDescFlags & static_cast<Render::DescriptorSetFlags>(Render::DescriptorSetFlagBits::eGlobal))
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, screenQuadMaterial->pipelineLayout, 0,
			quadDescSets, uniformOffset);
	}
	else
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, screenQuadMaterial->pipelineLayout, 0,
			quadDescSets, {});
	}

	if (pipelineType == PipelineType::ePostprocess)
	{
		if (_renderMode == RenderMode::ePathTracing)
		{
			cmd.pushConstants(screenQuadMaterial->pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
				sizeof(static_cast<int32_t>(_cfg.DENOISING)), &_cfg.DENOISING);
		}
		else
		{
			cmd.pushConstants(screenQuadMaterial->pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
				sizeof(static_cast<int32_t>(_cfg.FXAA)), &_cfg.FXAA);
		}
	}

	cmd.draw(3, 1, 0, 0);
}


void RenderSystem::draw_skybox(vk::CommandBuffer cmd, RenderObject& object)
{
	const PlumeCamera& camera = *_pCamera;

	glm::mat4 view = camera.get_view_matrix();

	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(camera._zoom),
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	if (!object.model || !object.materialSet)
	{
		return;
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, object.materialSet->pipeline);

	// scene & camera descriptor
	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(CameraDataGPU) +
		sizeof(LightingData)) * frameIndex);

	Render::DescriptorSetFlags descFlags = object.materialSet->usedDescriptorSets;

	auto descSets = _descMng.get_descriptor_sets(descFlags, frameIndex);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 0,
		descSets, uniformOffset);

	glm::mat4 model = object.transformMatrix;
	// final render matrix
	glm::mat4 mesh_matrix = projection * view * model;

	MeshPushConstants constants = {};
	constants.render_matrix = object.transformMatrix;

	// upload to GPU
	cmd.pushConstants(object.materialSet->pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
		sizeof(MeshPushConstants), &constants);

	vk::DeviceSize offset = 0;
	cmd.bindVertexBuffers(0, object.mesh->_vertexBuffer._buffer, offset);
	cmd.bindIndexBuffer(object.mesh->_indexBuffer._buffer, offset, vk::IndexType::eUint32);

	cmd.drawIndexed(static_cast<uint32_t>(object.mesh->_indices.size()), 1, 0, 0, 0);
}


void RenderSystem::draw_debug_ui(vk::CommandBuffer cmd, vk::ImageView targetImageView)
{
	vk::RenderingAttachmentInfo uiAttachmentInfo = vkinit::rendering_attachment_info(targetImageView,
		vk::ImageLayout::eColorAttachmentOptimal, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, {});

	vk::RenderingInfo uiRenderInfo;
	uiRenderInfo.renderArea.extent = _windowExtent;
	uiRenderInfo.layerCount = 1;
	uiRenderInfo.setColorAttachments(uiAttachmentInfo);

	cmd.beginRendering(uiRenderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	cmd.endRendering();
}


void RenderSystem::trace_rays(vk::CommandBuffer cmd, uint32_t swapchainImageIndex)
{
	update_frame();

	if (_rayConstants.frame >= _maxAccumFrames)
	{
		return;
	}

	FrameData& currentFrame = _frames[swapchainImageIndex];

	MaterialSet* rtMaterial = get_material_set(PipelineType::eRayTracing);
	Render::DescriptorSetFlags rtDescFlags = rtMaterial->usedDescriptorSets;

	std::vector<vk::DescriptorSet> rtSets = _descMng.get_descriptor_sets(rtDescFlags, swapchainImageIndex);

	cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, rtMaterial->pipeline);

	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(CameraDataGPU) +
		sizeof(LightingData)) * swapchainImageIndex);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, rtMaterial->pipelineLayout,
		0, rtSets, uniformOffset);

	_rayConstants.MAX_BOUNCES = _cfg.MAX_BOUNCES;
	_rayConstants.USE_MOTION_VECTORS = _cfg.MOTION_VECTORS;
	_rayConstants.USE_SHADER_EXECUTION_REORDERING = _cfg.SHADER_EXECUTION_REORDERING;
	_rayConstants.USE_TEMPORAL_ACCUMULATION = _cfg.TEMPORAL_ACCUMULATION;

	// upload to GPU
	cmd.pushConstants(rtMaterial->pipelineLayout, vk::ShaderStageFlagBits::eRaygenKHR, 0,
		sizeof(RayPushConstants), &_rayConstants);

	cmd.traceRaysKHR(_rgenRegion, _rmissRegion, _rchitRegion, _rcallRegion, _windowExtent.width, _windowExtent.height, 1);
}


void RenderSystem::setup_debug_ui_frame()
{
	if (!_showDebugUi)
	{
		return;
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowSize(ImVec2(300, 300));
	ImGui::Begin("Options");
	if (_renderMode == RenderMode::ePathTracing)
	{
		ImGui::SliderInt("Max Bounces", &_cfg.MAX_BOUNCES, 0, 30);
		ImGui::Checkbox("Use Denoising", &_cfg.DENOISING);
		ImGui::Checkbox("Use Temporal Accumulation", &_cfg.TEMPORAL_ACCUMULATION);
		bool prevMv = _cfg.MOTION_VECTORS;
		ImGui::Checkbox("Use Motion Vectors", &_cfg.MOTION_VECTORS);
		if (_cfg.MOTION_VECTORS != prevMv)
		{
			reset_frame();
		}
		ImGui::Checkbox("Use Shader Execution Reordering", &_cfg.SHADER_EXECUTION_REORDERING);
	}
	else if (_renderMode == RenderMode::eHybrid)
	{
		ImGui::Checkbox("Use FXAA", &_cfg.FXAA);
	}
	ImGui::End();

	ImGui::Render();
}


vk::Pipeline PipelineBuilder::buildPipeline(vk::Device device)
{
	// make viewport state from viewpoint + scissor (only one of each for now)
	vk::PipelineViewportStateCreateInfo viewportState = {};
	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	std::vector<vk::PipelineColorBlendAttachmentState> blendStates(_renderingCreateInfo.colorAttachmentCount);

	for (auto& state : blendStates)
	{
		state = _colorBlendAttachment;
	}

	// dummy color blending (no blending), just writing to color attachment for now
	vk::PipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = vk::LogicOp::eCopy;
	colorBlending.setAttachments(blendStates);
	
	// finally assemble the pipeline
	vk::GraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.pNext = &_renderingCreateInfo;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	vk::Pipeline newPipeline;

	// check pipeline build errors
	auto pipelineResVal = device.createGraphicsPipelines({}, pipelineInfo);
	if (pipelineResVal.result != vk::Result::eSuccess)
	{
		std::cout << "failed to create pipeline" << std::endl;
		return VK_NULL_HANDLE;
	}
	else
	{
		newPipeline = pipelineResVal.value[0];
		return newPipeline;
	}
}