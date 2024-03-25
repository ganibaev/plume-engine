#include "vk_engine.h"

#include "vk_initializers.h"

#include "vk_textures.h"
#include "vk_raytracing.h"

// This will make initialization much less of a pain
#include "VkBootstrap.h"

#include <fstream>

#include <unordered_map>

#include <glm/gtx/transform.hpp>

constexpr static bool ENABLE_VALIDATION_LAYERS = true;
constexpr static float DRAW_DISTANCE = 6000.0f;

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"Plume",
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);
	
	// load core Vulkan structures
	init_vulkan();

	init_swapchain();

	init_commands();

	init_gbuffer_attachments();

	init_prepass_attachments();

	init_raytracing();

	init_sync_structures();

	load_meshes();

	init_descriptors();

	init_pipelines();

	load_images();

	init_scene();

	init_blas();

	init_tlas();

	if (_renderMode == RenderMode::ePathTracing)
	{
		init_rt_descriptors();

		init_rt_pipeline();

		init_shader_binding_table();
	}

	// everything went well
	_isInitialized = true;
}

void VulkanEngine::init_vulkan()
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
	SDL_Vulkan_CreateSurface(_window, _instance, &surfaceC);
	_surface = surfaceC;

	// use VkBootstrap to select a GPU
	// the GPU should be able to write to SDL surface and support Vulkan 1.3
	vkb::PhysicalDeviceSelector selector{ vkb_inst };

	vk::PhysicalDeviceFeatures miscFeatures;
	miscFeatures.shaderInt64 = VK_TRUE;

	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_surface(_surface)
		.add_required_extension("VK_KHR_acceleration_structure")
		.add_required_extension("VK_KHR_ray_tracing_pipeline")
		.add_required_extension("VK_KHR_ray_query")
		.add_required_extension("VK_KHR_deferred_host_operations")
		.add_required_extension("VK_KHR_shader_clock")
		.set_required_features(miscFeatures)
		.select()
		.value();

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
	vk::PhysicalDeviceScalarBlockLayoutFeatures scalarBlockFeatures;
	scalarBlockFeatures.scalarBlockLayout = VK_TRUE;
	vk::PhysicalDeviceShaderClockFeaturesKHR clockFeatures;
	clockFeatures.shaderDeviceClock = VK_TRUE;
	clockFeatures.shaderSubgroupClock = VK_TRUE;
	
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&descIndexingFeatures).add_pNext(&shaderDrawParametersFeatures)
		.add_pNext(&accelFeatures).add_pNext(&rayQueryFeatures).add_pNext(&rtPipelineFeatures).add_pNext(&addressFeatures)
		.add_pNext(&dynamicRenderingFeatures).add_pNext(&scalarBlockFeatures).add_pNext(&clockFeatures).build().value();

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

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // set vsync mode
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

	vk::ImageCreateInfo intermediateImageCreateInfo;

	if (_renderMode == RenderMode::ePathTracing)
	{
		intermediateImageCreateInfo = vkinit::image_create_info(vk::Format::eR16G16B16A16Sfloat,
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
			_windowExtent3D, 1, vk::SampleCountFlagBits::e1, ImageType::eRTXOutput);
	}
	else
	{
		intermediateImageCreateInfo = vkinit::image_create_info(_swapchainImageFormat,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, _windowExtent3D, 1);
	}
	AllocatedImage intermediateImage = create_image(intermediateImageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
	_intermediateImage = intermediateImage._image;

	vk::ImageViewCreateInfo intermediateViewCreateInfo;
	if (_renderMode == RenderMode::ePathTracing)
	{
		intermediateViewCreateInfo = vkinit::image_view_create_info(vk::Format::eR16G16B16A16Sfloat,
			intermediateImage._image, vk::ImageAspectFlagBits::eColor, 1);
	}
	else
	{
		intermediateViewCreateInfo = vkinit::image_view_create_info(_swapchainImageFormat,
			intermediateImage._image, vk::ImageAspectFlagBits::eColor, 1);
	}

	_intermediateImageView = _device.createImageView(intermediateViewCreateInfo);

	AllocatedImage prevFrameImage;

	if (_renderMode == RenderMode::ePathTracing)
	{
		auto prevFrameImageCreateInfo = vkinit::image_create_info(vk::Format::eR16G16B16A16Sfloat,
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
			_windowExtent3D, 1, vk::SampleCountFlagBits::e1, ImageType::eRTXOutput);
		prevFrameImage = create_image(prevFrameImageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
		_prevFrameImage = prevFrameImage._image;
		vk::ImageViewCreateInfo prevFrameViewCreateInfo = vkinit::image_view_create_info(vk::Format::eR16G16B16A16Sfloat,
			prevFrameImage._image, vk::ImageAspectFlagBits::eColor, 1);
		_prevFrameImageView = _device.createImageView(prevFrameViewCreateInfo);
	}

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(_allocator, intermediateImage._image, intermediateImage._allocation);
		_device.destroyImageView(_intermediateImageView);
		if (_renderMode == RenderMode::ePathTracing) {
			vmaDestroyImage(_allocator, prevFrameImage._image, prevFrameImage._allocation);
			_device.destroyImageView(_prevFrameImageView);
		}
	});

	_mainDeletionQueue.push_function([=]() {
		_device.destroySwapchainKHR(_swapchain);
	});

	_depthFormat = vk::Format::eD32Sfloat;
}

void VulkanEngine::image_layout_transition(vk::CommandBuffer cmd, vk::AccessFlags srcAccessMask,
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

void VulkanEngine::switch_intermediate_image_layout(vk::CommandBuffer cmd, bool beforeRendering)
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

void VulkanEngine::switch_swapchain_image_layout(vk::CommandBuffer cmd, uint32_t swapchainImageIndex, bool beforeRendering)
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

void VulkanEngine::switch_frame_image_layout(vk::Image image, vk::CommandBuffer cmd)
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

FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::init_commands()
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

void VulkanEngine::init_gbuffer_attachments()
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

	_lightingPassPipelineInfo.setColorAttachmentFormats(_swapchainImageFormat);
	_lightingPassPipelineInfo.setDepthAttachmentFormat(_depthFormat);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(_lightingDepthImageView);
		_device.destroyImageView(_depthImageView);
	});
}

void VulkanEngine::create_attachment(vk::Format format, vk::ImageUsageFlagBits usage, vk::RenderingAttachmentInfo& attachmentInfo,
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

void VulkanEngine::init_prepass_attachments()
{
	create_attachment(_motionVectorFormat, vk::ImageUsageFlagBits::eColorAttachment, _motionVectorAttachment,
		&_motionVectorImage, &_motionVectorImageView);
}

void VulkanEngine::init_raytracing()
{
	_gpuProperties.pNext = &_rtProperties;
	_chosenGPU.getProperties2(&_gpuProperties);
}

void VulkanEngine::init_sync_structures()
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

void VulkanEngine::init_descriptors()
{
	std::vector<vk::DescriptorPoolSize> poolSizes = { 
		{ vk::DescriptorType::eUniformBuffer, 10 },
		{ vk::DescriptorType::eUniformBufferDynamic, 10 },
		{ vk::DescriptorType::eStorageBuffer, 10 },
		{ vk::DescriptorType::eCombinedImageSampler, 500 },
		{ vk::DescriptorType::eAccelerationStructureKHR, 10 }
	};
	
	vk::DescriptorPoolCreateInfo poolInfo = {};
	poolInfo.maxSets = 100;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	_descriptorPool = _device.createDescriptorPool(poolInfo);

	vk::DescriptorSetLayoutBinding tlasBinding =
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eAccelerationStructureKHR, vk::ShaderStageFlagBits::eFragment, 0);

	vk::DescriptorSetLayoutCreateInfo tlasLayoutInfo;
	tlasLayoutInfo.setBindings(tlasBinding);

	_tlasSetLayout = _device.createDescriptorSetLayout(tlasLayoutInfo);

	vk::DescriptorSetAllocateInfo tlasSetAllocInfo;
	tlasSetAllocInfo.descriptorPool = _descriptorPool;
	tlasSetAllocInfo.setSetLayouts(_tlasSetLayout);
	tlasSetAllocInfo.descriptorSetCount = 1;

	_tlasDescriptorSet = _device.allocateDescriptorSets(tlasSetAllocInfo)[0];

	vk::DescriptorSetLayoutBinding camSceneBufferBinding = 
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eUniformBufferDynamic, vk::ShaderStageFlagBits::eVertex |
			vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR, 0);

	vk::DescriptorSetLayoutBinding bindings[] = { camSceneBufferBinding };

	vk::DescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.bindingCount = 1;
	setInfo.pBindings = bindings;

	_globalSetLayout = _device.createDescriptorSetLayout(setInfo);

	vk::DescriptorSetLayoutBinding objectBufferBinding = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR, 0);
	
	vk::DescriptorSetLayoutCreateInfo objSetInfo = {};
	objSetInfo.bindingCount = 1;
	objSetInfo.pBindings = &objectBufferBinding;

	_objectSetLayout = _device.createDescriptorSetLayout(objSetInfo);

	const size_t camSceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUCameraData) + sizeof(GPUSceneData));

	_camSceneBuffer = create_buffer(camSceneParamBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);

	vk::DescriptorSetAllocateInfo allocInfo = {};
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_globalSetLayout;

	_globalDescriptor = _device.allocateDescriptorSets(allocInfo)[0];

	Model* scene = get_model("scene");

	vk::DescriptorSetLayoutBinding textureBind = vkinit::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler,
		vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR, 0,
		static_cast<uint32_t>(_scene._diffuseTexNames.size()));

	vk::StructureChain<vk::DescriptorSetLayoutCreateInfo, vk::DescriptorSetLayoutBindingFlagsCreateInfo> c;

	auto& texSetInfo = c.get<vk::DescriptorSetLayoutCreateInfo>();
	texSetInfo.setBindings(textureBind);

	// Allow for usage of unwritten descriptor sets and variable size arrays of textures
	vk::DescriptorBindingFlags bindFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
		vk::DescriptorBindingFlagBits::eVariableDescriptorCount;

	auto& bindFlagsInfo = c.get<vk::DescriptorSetLayoutBindingFlagsCreateInfo>();
	bindFlagsInfo.setBindingFlags(bindFlags);

	_textureSetLayout = _device.createDescriptorSetLayout(texSetInfo);

	// cubemap set layout

	vk::DescriptorSetLayoutBinding cubemapBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eMissKHR, 0);

	vk::DescriptorSetLayoutCreateInfo cubemapSetInfo;
	cubemapSetInfo.setBindings(cubemapBind);

	_cubemapSetLayout = _device.createDescriptorSetLayout(cubemapSetInfo);

	// lighting pass GBuffer descriptor set

	vk::DescriptorSetLayoutBinding positionBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, GBUFFER_POSITION_SLOT);
	vk::DescriptorSetLayoutBinding normalBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, GBUFFER_NORMAL_SLOT);
	vk::DescriptorSetLayoutBinding albedoBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, GBUFFER_ALBEDO_SLOT);
	vk::DescriptorSetLayoutBinding metallicRoughnessBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, GBUFFER_METALLIC_ROUGHNESS_SLOT);

	std::array<vk::DescriptorSetLayoutBinding, NUM_GBUFFER_ATTACHMENTS> gBufferBindings = {
		positionBind, normalBind, albedoBind, metallicRoughnessBind
	};

	vk::DescriptorSetLayoutCreateInfo gBufferSetInfo;
	gBufferSetInfo.setBindings(gBufferBindings);

	_gBufferSetLayout = _device.createDescriptorSetLayout(gBufferSetInfo);

	// postprocess pass descriptor set
	
	vk::DescriptorSetLayoutBinding frameTextureBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0);

	vk::DescriptorSetLayoutCreateInfo frameTexSetInfo;
	frameTexSetInfo.setBindings(frameTextureBind);

	_postprocessSetLayout = _device.createDescriptorSetLayout(frameTexSetInfo);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		constexpr int MAX_OBJECTS = 10000;
		_frames[i]._objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, 
			vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);

		vk::DescriptorSetAllocateInfo objSetAllocInfo = {};
		objSetAllocInfo.descriptorPool = _descriptorPool;
		objSetAllocInfo.setSetLayouts(_objectSetLayout);

		_frames[i]._objectDescriptor = _device.allocateDescriptorSets(objSetAllocInfo)[0];

		vk::DescriptorSetAllocateInfo gBufferSetAllocInfo;
		gBufferSetAllocInfo.descriptorPool = _descriptorPool;
		gBufferSetAllocInfo.setSetLayouts(_gBufferSetLayout);

		_frames[i]._gBufferDescriptorSet = _device.allocateDescriptorSets(gBufferSetAllocInfo)[0];

		vk::DescriptorSetAllocateInfo postprocessSetAllocInfo;
		postprocessSetAllocInfo.descriptorPool = _descriptorPool;
		postprocessSetAllocInfo.setSetLayouts(_postprocessSetLayout);

		_frames[i]._postprocessDescriptorSet = _device.allocateDescriptorSets(postprocessSetAllocInfo)[0];

		// fill gbuffer descriptor sets
		vk::SamplerCreateInfo gBufferSamplerInfo = vkinit::sampler_create_info(vk::Filter::eNearest, vk::Filter::eNearest,
			1, 1.0, vk::SamplerAddressMode::eClampToEdge);

		vk::Sampler gBufferSampler = _device.createSampler(gBufferSamplerInfo);

		_mainDeletionQueue.push_function([=]() {
			_device.destroySampler(gBufferSampler);
		});

		vk::DescriptorImageInfo positionInfo;
		positionInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		positionInfo.imageView = _gBufferColorAttachments[GBUFFER_POSITION_SLOT].imageView;
		positionInfo.sampler = gBufferSampler;

		vk::DescriptorImageInfo normalInfo;
		normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		normalInfo.imageView = _gBufferColorAttachments[GBUFFER_NORMAL_SLOT].imageView;
		normalInfo.sampler = gBufferSampler;

		vk::DescriptorImageInfo albedoInfo;
		albedoInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		albedoInfo.imageView = _gBufferColorAttachments[GBUFFER_ALBEDO_SLOT].imageView;
		albedoInfo.sampler = gBufferSampler;

		vk::DescriptorImageInfo metallicRoughnessInfo;
		metallicRoughnessInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		metallicRoughnessInfo.imageView = _gBufferColorAttachments[GBUFFER_METALLIC_ROUGHNESS_SLOT].imageView;
		metallicRoughnessInfo.sampler = gBufferSampler;

		vk::DescriptorBufferInfo camSceneInfo = {};
		camSceneInfo.buffer = _camSceneBuffer._buffer;
		camSceneInfo.offset = 0;
		camSceneInfo.range = sizeof(GPUCameraData) + sizeof(GPUSceneData);

		vk::DescriptorBufferInfo objBufferInfo = {};
		objBufferInfo.buffer = _frames[i]._objectBuffer._buffer;
		objBufferInfo.offset = 0;
		objBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		// use gbuffer sampler for postprocessing pass

		vk::DescriptorImageInfo frameImageInfo;
		frameImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		frameImageInfo.imageView = _intermediateImageView;
		frameImageInfo.sampler = gBufferSampler;

		vk::WriteDescriptorSet positionWrite = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
			_frames[i]._gBufferDescriptorSet, &positionInfo, GBUFFER_POSITION_SLOT);
		vk::WriteDescriptorSet normalWrite = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
			_frames[i]._gBufferDescriptorSet, &normalInfo, GBUFFER_NORMAL_SLOT);
		vk::WriteDescriptorSet albedoWrite = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
			_frames[i]._gBufferDescriptorSet, &albedoInfo, GBUFFER_ALBEDO_SLOT);
		vk::WriteDescriptorSet metallicRoughnessWrite = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
			_frames[i]._gBufferDescriptorSet, &metallicRoughnessInfo, GBUFFER_METALLIC_ROUGHNESS_SLOT);

		vk::WriteDescriptorSet camSceneWrite = vkinit::write_descriptor_buffer(
			vk::DescriptorType::eUniformBufferDynamic, _globalDescriptor, &camSceneInfo, 0);
		
		vk::WriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(
			vk::DescriptorType::eStorageBuffer, _frames[i]._objectDescriptor, &objBufferInfo, 0);

		vk::WriteDescriptorSet postprocessWrite = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
			_frames[i]._postprocessDescriptorSet, &frameImageInfo, 0);
		
		vk::WriteDescriptorSet setWrites[] = { positionWrite, normalWrite, albedoWrite, metallicRoughnessWrite,
			camSceneWrite, objectWrite, postprocessWrite };

		_device.updateDescriptorSets(setWrites, {});

		_mainDeletionQueue.push_function([=]() {
			vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(_frames[i]._objectBuffer._buffer),
				_frames[i]._objectBuffer._allocation);
		});
	}

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _camSceneBuffer._buffer, _camSceneBuffer._allocation);
		_device.destroyDescriptorSetLayout(_tlasSetLayout);
		_device.destroyDescriptorSetLayout(_globalSetLayout);
		_device.destroyDescriptorSetLayout(_objectSetLayout);
		_device.destroyDescriptorSetLayout(_textureSetLayout);
		_device.destroyDescriptorSetLayout(_cubemapSetLayout);
		_device.destroyDescriptorSetLayout(_gBufferSetLayout);
		_device.destroyDescriptorSetLayout(_postprocessSetLayout);
		_device.destroyDescriptorPool(_descriptorPool);
	});
}

void VulkanEngine::init_pipelines()
{
	// init textured forward pipeline

	Model* scene = get_model("scene");

	vk::PipelineLayoutCreateInfo texturedPipelineLayoutInfo;

	vk::DescriptorSetLayout texSetLayouts[] = {
		_globalSetLayout, _objectSetLayout, _textureSetLayout, _textureSetLayout, _textureSetLayout, _textureSetLayout,
		_tlasSetLayout
	};

	texturedPipelineLayoutInfo.setSetLayouts(texSetLayouts);

	vk::PipelineLayout texturedPipelineLayout;
	texturedPipelineLayout = _device.createPipelineLayout(texturedPipelineLayoutInfo);

	vk::PipelineLayoutCreateInfo skyboxPipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	vk::PushConstantRange skyboxPushConstants;
	skyboxPushConstants.offset = 0;
	skyboxPushConstants.size = sizeof(MeshPushConstants);
	skyboxPushConstants.stageFlags = vk::ShaderStageFlagBits::eVertex;

	skyboxPipelineLayoutInfo.setPushConstantRanges(skyboxPushConstants);
	
	vk::DescriptorSetLayout skyboxSetLayouts[] = { _globalSetLayout, _objectSetLayout, _cubemapSetLayout };
	skyboxPipelineLayoutInfo.setSetLayouts(skyboxSetLayouts);

	vk::PipelineLayout skyboxPipelineLayout = _device.createPipelineLayout(skyboxPipelineLayoutInfo);

	// build stage_create_info for vertex and fragment stages
	// this lets pipeline know about shader modules
	PipelineBuilder pipelineBuilder;

	// not using tricky vertex input yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	// draw triangle lists via input assembly
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(vk::PrimitiveTopology::eTriangleList);
	
	// use swapchain extents to build viewport and scissor
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = static_cast<float>(_windowExtent.width);
	pipelineBuilder._viewport.height = static_cast<float>(_windowExtent.height);
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;
	pipelineBuilder._scissor.offset = vk::Offset2D{ 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	vk::PipelineRenderingCreateInfo texRenderingCreateInfo;
	texRenderingCreateInfo.setColorAttachmentFormats(_swapchainImageFormat);
	texRenderingCreateInfo.setDepthAttachmentFormat(_depthFormat);

	pipelineBuilder._renderingCreateInfo = texRenderingCreateInfo;

	// draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(vk::PolygonMode::eFill);
	
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info(vk::SampleCountFlagBits::e1);

	// no blending, write to rgba
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	// default depth testing
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, vk::CompareOp::eLessOrEqual);

	// build mesh pipeline 

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	// use the vertex info with the pipeline builder
	pipelineBuilder._vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);

	pipelineBuilder._vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

	// specialization constants for arrays in shaders

	std::array<uint32_t, 1> specCostants = {
		NUM_LIGHTS
	};

	vk::SpecializationMapEntry specConstantEntries[1];
	specConstantEntries[0].setConstantID(0);
	specConstantEntries[0].setSize(sizeof(specCostants));
	specConstantEntries[0].setOffset(0);

	vk::SpecializationInfo specInfo;
	specInfo.setMapEntries(specConstantEntries);
	specInfo.setDataSize(sizeof(specCostants));
	specInfo.setPData(specCostants.data());

	vk::ShaderModule texturedMeshShader;
	if (!load_shader_module("../../../shaders/textured_lit.frag.spv", &texturedMeshShader))
	{
		std::cout << "Error building textured mesh fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Textured mesh fragment shader loaded successfully" << std::endl;
	}

	vk::ShaderModule meshVertShader;
	if (!load_shader_module("../../../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error building triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Mesh Triangle vertex shader loaded successfully" << std::endl;
	}

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		meshVertShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		texturedMeshShader));

	pipelineBuilder._shaderStages[1].setPSpecializationInfo(&specInfo);

	pipelineBuilder._pipelineLayout = texturedPipelineLayout;
	
	// init geometry pass pipeline

	vk::ShaderModule geometryPassVertexShader;

	if (!load_shader_module("../../../shaders/geometry_pass.vert.spv", &geometryPassVertexShader))
	{
		std::cout << "Error building geometry pass vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Geometry pass vertex shader loaded successfully" << std::endl;
	}

	vk::ShaderModule geometryPassFragmentShader;

	if (!load_shader_module("../../../shaders/geometry_pass.frag.spv", &geometryPassFragmentShader))
	{
		std::cout << "Error building geometry pass fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Geometry pass fragment shader loaded successfully" << std::endl;
	}

	pipelineBuilder._renderingCreateInfo = _geometryPassPipelineInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		geometryPassVertexShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		geometryPassFragmentShader));

	vk::Pipeline geometryPassPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(geometryPassPipeline, texturedPipelineLayout, "geometrypass");

	// init lighting pass pipeline

	vk::ShaderModule lightingPassVertexShader;

	if (!load_shader_module("../../../shaders/lighting_pass.vert.spv", &lightingPassVertexShader))
	{
		std::cout << "Error building lighting pass vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Lighting pass vertex shader loaded successfully" << std::endl;
	}

	vk::ShaderModule lightingPassFragmentShader;

	if (!load_shader_module("../../../shaders/lighting_pass.frag.spv", &lightingPassFragmentShader))
	{
		std::cout << "Error building lighting pass fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Lighting pass fragment shader loaded successfully" << std::endl;
	}

	vk::PipelineLayoutCreateInfo lightingPassPipelineLayoutInfo;

	vk::DescriptorSetLayout lightingPassSetLayouts[] = {
		_globalSetLayout, _gBufferSetLayout, _tlasSetLayout
	};

	lightingPassPipelineLayoutInfo.setSetLayouts(lightingPassSetLayouts);

	_lightingPassPipelineLayout = _device.createPipelineLayout(lightingPassPipelineLayoutInfo);

	pipelineBuilder._pipelineLayout = _lightingPassPipelineLayout;

	pipelineBuilder._renderingCreateInfo = _lightingPassPipelineInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		lightingPassVertexShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		lightingPassFragmentShader));

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	_lightingPassPipeline = pipelineBuilder.buildPipeline(_device);

	// init skybox pipeline

	vk::ShaderModule skyboxVertShader;

	if (!load_shader_module("../../../shaders/skybox.vert.spv", &skyboxVertShader))
	{
		std::cout << "Error building skybox vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Skybox vertex shader loaded successfully" << std::endl;
	}

	vk::ShaderModule skyboxFragShader;

	if (!load_shader_module("../../../shaders/skybox.frag.spv", &skyboxFragShader))
	{
		std::cout << "Error building skybox fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Skybox fragment shader loaded successfully" << std::endl;
	}

	vertexDescription = Vertex::get_vertex_description();

	// use the vertex info with the pipeline builder
	pipelineBuilder._vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);

	pipelineBuilder._vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

	pipelineBuilder._renderingCreateInfo = texRenderingCreateInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		skyboxVertShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		skyboxFragShader));
	pipelineBuilder._pipelineLayout = skyboxPipelineLayout;

	vk::Pipeline skyboxPipeline = pipelineBuilder.buildPipeline(_device);
	create_material_set(skyboxPipeline, skyboxPipelineLayout, "skybox");

	// init postprocessing pass pipeline

	vk::ShaderModule postPassVertexShader;

	if (!load_shader_module("../../../shaders/postprocess.vert.spv", &postPassVertexShader))
	{
		std::cout << "Error building postprocess vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Postprocess vertex shader loaded successfully" << std::endl;
	}

	vk::ShaderModule fxaaFragmentShader;

	if (!load_shader_module("../../../shaders/fxaa.frag.spv", &fxaaFragmentShader))
	{
		std::cout << "Error building FXAA fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "FXAA fragment shader loaded successfully" << std::endl;
	}

	vk::ShaderModule denoiserFragmentShader;

	if (!load_shader_module("../../../shaders/morrone_denoiser.frag.spv", &denoiserFragmentShader))
	{
		std::cout << "Error building denoiser fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Denoiser fragment shader loaded successfully" << std::endl;
	}

	vk::PipelineLayoutCreateInfo postPassPipelineLayoutInfo;

	vk::DescriptorSetLayout postPassSetLayouts[] = {
		_postprocessSetLayout
	};

	postPassPipelineLayoutInfo.setSetLayouts(postPassSetLayouts);

	_postPassPipelineLayout = _device.createPipelineLayout(postPassPipelineLayoutInfo);

	pipelineBuilder._pipelineLayout = _postPassPipelineLayout;

	vk::PipelineRenderingCreateInfo postPassPipelineRenderingInfo;
	postPassPipelineRenderingInfo.setColorAttachmentFormats(_swapchainImageFormat);
	postPassPipelineRenderingInfo.setDepthAttachmentFormat(_depthFormat);

	pipelineBuilder._renderingCreateInfo = postPassPipelineRenderingInfo;

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		postPassVertexShader));
	if (_renderMode == RenderMode::eHybrid)
	{
		pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
			fxaaFragmentShader));
	}
	else
	{
		pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
			denoiserFragmentShader));
	}
	

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	_postPassPipeline = pipelineBuilder.buildPipeline(_device);

	// init motion vector pass pipeline

	vk::ShaderModule motionVectorFragmentShader;

	if (!load_shader_module("../../../shaders/motion_vectors.frag.spv", &motionVectorFragmentShader))
	{
		std::cout << "Error building motion vector fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Motion vector fragment shader loaded successfully" << std::endl;
	}

	vk::PipelineLayoutCreateInfo mvPipelineLayoutInfo;
	mvPipelineLayoutInfo.setSetLayouts(_globalSetLayout);

	_mvPassPipelineLayout = _device.createPipelineLayout(mvPipelineLayoutInfo);
	pipelineBuilder._pipelineLayout = _mvPassPipelineLayout;

	vk::PipelineRenderingCreateInfo mvPipelineRenderingInfo;
	mvPipelineRenderingInfo.setColorAttachmentFormats(_motionVectorFormat);
	mvPipelineRenderingInfo.setDepthAttachmentFormat(_depthFormat);

	pipelineBuilder._renderingCreateInfo = mvPipelineRenderingInfo;
	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		postPassVertexShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		motionVectorFragmentShader));

	_mvPassPipeline = pipelineBuilder.buildPipeline(_device);

	// shader modules are now built into the pipelines, we don't need them anymore

	_device.destroyShaderModule(meshVertShader);
	_device.destroyShaderModule(texturedMeshShader);
	_device.destroyShaderModule(geometryPassVertexShader);
	_device.destroyShaderModule(geometryPassFragmentShader);
	_device.destroyShaderModule(lightingPassVertexShader);
	_device.destroyShaderModule(lightingPassFragmentShader);
	_device.destroyShaderModule(postPassVertexShader);
	_device.destroyShaderModule(fxaaFragmentShader);
	_device.destroyShaderModule(denoiserFragmentShader);
	_device.destroyShaderModule(motionVectorFragmentShader);
	_device.destroyShaderModule(skyboxVertShader);
	_device.destroyShaderModule(skyboxFragShader);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipeline(geometryPassPipeline);
		_device.destroyPipeline(_lightingPassPipeline);
		_device.destroyPipeline(skyboxPipeline);
		_device.destroyPipeline(_postPassPipeline);
		_device.destroyPipeline(_mvPassPipeline);
		
		_device.destroyPipelineLayout(texturedPipelineLayout);
		_device.destroyPipelineLayout(skyboxPipelineLayout);
		_device.destroyPipelineLayout(_lightingPassPipelineLayout);
		_device.destroyPipelineLayout(_postPassPipelineLayout);
		_device.destroyPipelineLayout(_mvPassPipelineLayout);
	});
}

void VulkanEngine::immediate_submit(std::function<void(vk::CommandBuffer cmd)>&& function, vk::CommandBuffer cmd)
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

void VulkanEngine::load_meshes()
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

void VulkanEngine::init_blas()
{
	std::vector<BLASInput> blasInputs;

	for (size_t i = 0; i < _renderables.size() - 1; ++i)
	{
		blasInputs.emplace_back(convert_to_blas_input(*_renderables[i].mesh));
	}

	vkrt::buildBlas(this, blasInputs);
}

void VulkanEngine::init_tlas()
{
	std::vector<vk::AccelerationStructureInstanceKHR> tlas;
	tlas.reserve(_renderables.size() - 1);

	for (uint32_t i = 0; i < _renderables.size() - 1; ++i)
	{
		vk::AccelerationStructureInstanceKHR accelInst;
		accelInst.setTransform(vkrt::convertToTransformKHR(_renderables[i].transformMatrix));
		accelInst.setInstanceCustomIndex(i);
		accelInst.setAccelerationStructureReference(vkrt::getBlasDeviceAddress(this, i));
		accelInst.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
		accelInst.setMask(0xFF);

		tlas.emplace_back(accelInst);
	}

	vkrt::buildTlas(this, tlas);

	vk::WriteDescriptorSetAccelerationStructureKHR descTlasWrite;
	descTlasWrite.setAccelerationStructures(_topLevelAS._structure);

	vk::WriteDescriptorSet descWrite;
	descWrite.dstBinding = 0;
	descWrite.dstSet = _tlasDescriptorSet;
	descWrite.descriptorCount = 1;
	descWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
	descWrite.pNext = &descTlasWrite;

	_device.updateDescriptorSets(descWrite, {});
}

void VulkanEngine::init_rt_descriptors()
{
	// binding 0 reserved for TLAS
	vk::DescriptorSetLayoutBinding tlasBinding =
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eAccelerationStructureKHR, vk::ShaderStageFlagBits::eRaygenKHR | 
			vk::ShaderStageFlagBits::eClosestHitKHR, 0);

	vk::DescriptorSetLayoutBinding outImageBinding = vkinit::descriptor_set_layout_binding(vk::DescriptorType::eStorageImage,
		vk::ShaderStageFlagBits::eRaygenKHR, 0);

	// binding 1 in per frame set for motion vectors
	vk::DescriptorSetLayoutBinding mvBinding =
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eRaygenKHR, 1);

	// binding 2 in per frame set for previous frame as texture
	vk::DescriptorSetLayoutBinding frameBinding =
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eRaygenKHR, 2);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{ vk::DescriptorType::eStorageImage, 10 },
		{ vk::DescriptorType::eCombinedImageSampler, 10 },
		{ vk::DescriptorType::eAccelerationStructureKHR, 5 }
	};

	vk::DescriptorPoolCreateInfo rtDescPoolInfo;
	rtDescPoolInfo.setMaxSets(50);
	rtDescPoolInfo.setPoolSizes(poolSizes);

	_rtDescriptorPool = _device.createDescriptorPool(rtDescPoolInfo);

	std::vector<vk::DescriptorSetLayoutBinding> rtSetBindings =
	{
		tlasBinding
	};

	std::vector<vk::DescriptorSetLayoutBinding> rtPerFrameSetBindings =
	{
		outImageBinding, mvBinding, frameBinding
	};

	vk::DescriptorSetLayoutCreateInfo rtLayoutInfo;
	rtLayoutInfo.setBindings(rtSetBindings);

	vk::DescriptorSetLayoutCreateInfo rtPerFrameLayoutInfo;
	rtPerFrameLayoutInfo.setBindings(rtPerFrameSetBindings);

	_rtSetLayout = _device.createDescriptorSetLayout(rtLayoutInfo);
	_rtPerFrameSetLayout = _device.createDescriptorSetLayout(rtPerFrameLayoutInfo);

	std::vector<vk::DescriptorSetLayout> rtSetLayouts = {
		_rtSetLayout
	};

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		rtSetLayouts.push_back(_rtPerFrameSetLayout);
	}

	vk::DescriptorSetAllocateInfo rtSetAllocInfo;
	rtSetAllocInfo.setDescriptorPool(_rtDescriptorPool);
	rtSetAllocInfo.setSetLayouts(rtSetLayouts);

	auto allocatedSets = _device.allocateDescriptorSets(rtSetAllocInfo);

	_rtDescriptorSet = allocatedSets[0];

	for (uint32_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		_frames[i]._perFrameSetRTX = allocatedSets[1 + i];
	}

	vk::WriteDescriptorSetAccelerationStructureKHR tlasWriteDescInfo;
	tlasWriteDescInfo.setAccelerationStructures(_topLevelAS._structure);

	vk::WriteDescriptorSet tlasWrite;
	tlasWrite.dstBinding = RTXBindings::eTlas;
	tlasWrite.dstSet = _rtDescriptorSet;
	tlasWrite.descriptorCount = 1;
	tlasWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
	tlasWrite.pNext = &tlasWriteDescInfo;

	// motion vector sampler
	vk::SamplerCreateInfo mvSamplerInfo = vkinit::sampler_create_info(vk::Filter::eNearest, vk::Filter::eNearest,
		1, 1.0, vk::SamplerAddressMode::eClampToEdge);
	vk::Sampler mvSampler = _device.createSampler(mvSamplerInfo);

	// previous frame texture sampler
	vk::SamplerCreateInfo frameSamplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear,
		1, 1.0, vk::SamplerAddressMode::eClampToEdge);
	vk::Sampler frameSampler = _device.createSampler(frameSamplerInfo);

	_device.updateDescriptorSets(tlasWrite, {});

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		vk::DescriptorImageInfo outImageDescInfo;
		outImageDescInfo.imageLayout = vk::ImageLayout::eGeneral;
		outImageDescInfo.imageView = _intermediateImageView;

		vk::WriteDescriptorSet outImageWrite;
		outImageWrite.dstBinding = 0;
		outImageWrite.dstSet = _frames[i]._perFrameSetRTX;
		outImageWrite.descriptorCount = 1;
		outImageWrite.descriptorType = vk::DescriptorType::eStorageImage;
		outImageWrite.setImageInfo(outImageDescInfo);


		vk::DescriptorImageInfo motionVectorsDescInfo;
		motionVectorsDescInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		motionVectorsDescInfo.imageView = _motionVectorImageView;
		motionVectorsDescInfo.sampler = mvSampler;

		vk::WriteDescriptorSet mvImageWrite;
		mvImageWrite.dstBinding = 1;
		mvImageWrite.dstSet = _frames[i]._perFrameSetRTX;
		mvImageWrite.descriptorCount = 1;
		mvImageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		mvImageWrite.setImageInfo(motionVectorsDescInfo);


		vk::DescriptorImageInfo frameImageDescInfo;
		frameImageDescInfo.imageLayout = vk::ImageLayout::eGeneral;
		frameImageDescInfo.imageView = _prevFrameImageView;
		frameImageDescInfo.sampler = frameSampler;

		vk::WriteDescriptorSet frameImageWrite;
		frameImageWrite.dstBinding = 2;
		frameImageWrite.dstSet = _frames[i]._perFrameSetRTX;
		frameImageWrite.descriptorCount = 1;
		frameImageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		frameImageWrite.setImageInfo(frameImageDescInfo);


		std::vector<vk::WriteDescriptorSet> perFrameWrites = {
			outImageWrite, mvImageWrite, frameImageWrite
		};

		_device.updateDescriptorSets(perFrameWrites, {});
	}
	_mainDeletionQueue.push_function([=]() {
		_device.destroyDescriptorSetLayout(_rtSetLayout);
		_device.destroyDescriptorSetLayout(_rtPerFrameSetLayout);
		_device.destroyDescriptorPool(_rtDescriptorPool);
		_device.destroySampler(mvSampler);
		_device.destroySampler(frameSampler);
	});
}

void VulkanEngine::init_rt_pipeline()
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

	vk::PipelineShaderStageCreateInfo stage;
	stage.pName = "main";

	// Raygen
	vk::ShaderModule pathTracingRayGen;
	if (!load_shader_module("../../../shaders/path_tracing.rgen.spv", &pathTracingRayGen))
	{
		std::cout << "Error building path tracing raygen shader module" << std::endl;
	}
	else
	{
		std::cout << "Path tracing raygen shader loaded successfully" << std::endl;
	}
	stage.module = pathTracingRayGen;
	stage.stage = vk::ShaderStageFlagBits::eRaygenKHR;
	stages[StageIndices::eRaygen] = stage;

	// Miss
	vk::ShaderModule pathTracingMiss;
	if (!load_shader_module("../../../shaders/path_tracing.rmiss.spv", &pathTracingMiss))
	{
		std::cout << "Error building path tracing miss shader module" << std::endl;
	}
	else
	{
		std::cout << "Path tracing miss shader loaded successfully" << std::endl;
	}
	stage.module = pathTracingMiss;
	stage.stage = vk::ShaderStageFlagBits::eMissKHR;
	stages[StageIndices::eMiss] = stage;

	// Shadow miss
	vk::ShaderModule shadowMiss;
	if (!load_shader_module("../../../shaders/trace_shadow.rmiss.spv", &shadowMiss))
	{
		std::cout << "Error building shadow miss shader module" << std::endl;
	}
	else
	{
		std::cout << "Shadow miss shader loaded successfully" << std::endl;
	}
	stage.module = shadowMiss;
	stage.stage = vk::ShaderStageFlagBits::eMissKHR;
	stages[StageIndices::eShadowMiss] = stage;

	// Closest hit
	vk::ShaderModule pathTracingClosestHit;
	if (!load_shader_module("../../../shaders/path_tracing.rchit.spv", &pathTracingClosestHit))
	{
		std::cout << "Error building path tracing closest hit shader module" << std::endl;
	}
	else
	{
		std::cout << "Path tracing closest hit shader loaded successfully" << std::endl;
	}
	stage.module = pathTracingClosestHit;
	stage.stage = vk::ShaderStageFlagBits::eClosestHitKHR;
	stages[StageIndices::eClosestHit] = stage;

	// Any hit
	vk::ShaderModule pathTracingAnyHit;
	if (!load_shader_module("../../../shaders/path_tracing.rahit.spv", &pathTracingAnyHit))
	{
		std::cout << "Error building path tracing any hit shader module" << std::endl;
	}
	else
	{
		std::cout << "Path tracing any hit shader loaded successfully" << std::endl;
	}
	stage.module = pathTracingAnyHit;
	stage.stage = vk::ShaderStageFlagBits::eAnyHitKHR;
	stages[StageIndices::eAnyHit] = stage;

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

	std::vector<vk::DescriptorSetLayout> rtPipelineSetLayouts = {
		_rtSetLayout, _rtPerFrameSetLayout, _globalSetLayout, _objectSetLayout, _textureSetLayout,
		_textureSetLayout, _textureSetLayout, _textureSetLayout, _cubemapSetLayout
	};
	rtPipelineLayoutInfo.setSetLayouts(rtPipelineSetLayouts);
	_rtPipelineLayout = _device.createPipelineLayout(rtPipelineLayoutInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipelineLayout(_rtPipelineLayout);
	});

	vk::RayTracingPipelineCreateInfoKHR rtPipelineInfo;
	rtPipelineInfo.setStages(stages);
	rtPipelineInfo.setGroups(_rtShaderGroups);
	rtPipelineInfo.maxPipelineRayRecursionDepth = 2;
	rtPipelineInfo.layout = _rtPipelineLayout;

	if (_rtProperties.maxRayRecursionDepth <= 1) {
		std::cout << "Device doesn't support ray recursion (maxRayRecursionDepth <= 1)" << std::endl;
		abort();
	}

	auto rtPipelineResVal = _device.createRayTracingPipelineKHR({}, {}, rtPipelineInfo);
	VK_CHECK(rtPipelineResVal.result);
	_rtPipeline = rtPipelineResVal.value;

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipeline(_rtPipeline);
	});

	for (auto& stage : stages)
	{
		_device.destroyShaderModule(stage.module);
	}
}

void VulkanEngine::init_shader_binding_table()
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

	// get shader group handles
	uint32_t dataSize = handleCount * handleSize;
	std::vector<uint8_t> handles(dataSize);
	VK_CHECK(_device.getRayTracingShaderGroupHandlesKHR(_rtPipeline, 0, handleCount, dataSize, handles.data()));

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

void VulkanEngine::load_material_texture(Texture& tex, const std::string& texName, const std::string& matName,
	uint32_t texSlot, bool generateMipmaps/* = true */, vk::Format format/* = vk::Format::eR8G8B8A8Srgb */)
{
	vkutil::load_image_from_file(this, texName.data(), tex.image, generateMipmaps, format);

	vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(format,
		tex.image._image, vk::ImageAspectFlagBits::eColor, tex.image._mipLevels);
	tex.imageView = _device.createImageView(imageViewInfo);

	_loadedTextures[matName][texSlot] = tex;

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(tex.imageView);
	});
}

void VulkanEngine::load_skybox(Texture& skybox, const std::string& directory)
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

	vkutil::load_cubemap_from_files(this, files, skybox.image);
}

void VulkanEngine::load_images()
{
	Model* curModel = get_model("scene");

	Texture defaultTex = {};

	vkutil::load_image_from_file(this, "../../../assets/null-texture.png", defaultTex.image);

	vk::ImageViewCreateInfo defaultTexViewInfo = vkinit::image_view_create_info(vk::Format::eR8G8B8A8Srgb,
		defaultTex.image._image, vk::ImageAspectFlagBits::eColor, defaultTex.image._mipLevels);
	defaultTex.imageView = _device.createImageView(defaultTexViewInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(defaultTex.imageView);
	});

	for (size_t i = 0; i < _scene._diffuseTexNames.size(); ++i)
	{
		Texture diffuse = {};

		if (!_scene._diffuseTexNames[i].empty())
		{
			load_material_texture(diffuse, _scene._diffuseTexNames[i], _scene._matNames[i], DIFFUSE_TEX_SLOT);
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
		}
	}

	for (size_t i = 0; i < _scene._metallicTexNames.size(); ++i)
	{
		Texture specular = {};

		if (!_scene._metallicTexNames[i].empty())
		{
			load_material_texture(specular, _scene._metallicTexNames[i], _scene._matNames[i], METALLIC_TEX_SLOT);
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
		}
	}

	for (size_t i = 0; i < _scene._roughnessTexNames.size(); ++i)
	{
		Texture specular = {};

		if (!_scene._roughnessTexNames[i].empty())
		{
			load_material_texture(specular, _scene._roughnessTexNames[i], _scene._matNames[i], ROUGHNESS_TEX_SLOT);
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
		}
	}

	for (size_t i = 0; i < _scene._normalMapNames.size(); ++i)
	{
		Texture normalMap = {};
		Texture defaultNormal = {};

		if (!_scene._normalMapNames[i].empty())
		{
			load_material_texture(normalMap, _scene._normalMapNames[i], _scene._matNames[i], NORMAL_MAP_SLOT,
				true, vk::Format::eR32G32B32A32Sfloat);
		}
		else
		{
			vkutil::load_image_from_file(this, "../../../assets/null-normal.png", defaultNormal.image, true,
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

	load_skybox(_skybox, "../../../assets/skybox/");

	vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(vk::Format::eR8G8B8A8Srgb,
		_skybox.image._image, vk::ImageAspectFlagBits::eColor, _skybox.image._mipLevels, ImageType::eCubemap);
	_skybox.imageView = _device.createImageView(imageViewInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(_skybox.imageView);
	});
}

void VulkanEngine::reset_frame()
{
	_rayConstants.frame = -1;
}

void VulkanEngine::update_frame()
{
	static glm::mat4 refCamMatrix = {};
	static float refFov = _camera._zoom;
	static glm::vec3 refPosition{ 0.0f };

	const glm::mat4 m = _camera.get_view_matrix();
	const float fov = _camera._zoom;
	const glm::vec3 position = _camera._position;

	if (std::memcmp(&refCamMatrix[0][0], &m[0][0], sizeof(glm::mat4)) != 0 || refFov != fov ||
		std::memcmp(&refPosition[0], &position[0], sizeof(glm::vec3)) != 0)
	{
		refCamMatrix = m;
		refFov = fov;
		refPosition = position;
	}
	++_rayConstants.frame;
}

void VulkanEngine::fill_tex_descriptor_sets(std::vector<vk::DescriptorImageInfo>& texBufferInfos,
	const std::vector<std::string>& texNames, Model* model, uint32_t texSlot)
{
	texBufferInfos.resize(texNames.size());

	for (size_t i = 0; i < texNames.size(); ++i)
	{
		vk::SamplerCreateInfo samplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear,
			_loadedTextures[_scene._matNames[i]][texSlot].image._mipLevels, VK_LOD_CLAMP_NONE);

		vk::Sampler smoothSampler = _device.createSampler(samplerInfo);

		_mainDeletionQueue.push_function([=]() {
			_device.destroySampler(smoothSampler);
			});

		texBufferInfos[i].sampler = smoothSampler;
		texBufferInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		texBufferInfos[i].imageView = _loadedTextures[_scene._matNames[i]][texSlot].imageView;
	}
}

void VulkanEngine::init_scene()
{
	RenderObject suzanne = {};
	suzanne.model = get_model("suzanne");
	suzanne.materialSet = get_material_set("geometrypass");
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

	MaterialSet* pSceneMatSet = get_material_set("geometrypass");

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
	_skyboxObject.materialSet = get_material_set("skybox");
	_skyboxObject.mesh = &(_skyboxObject.model->_meshes.front());
	glm::mat4 meshScale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(600.0f, 600.0f, 600.0f));
	//glm::mat4 meshTranslate = glm::translate(glm::mat4{ 1.0f }, glm::vec3(2.8f, -8.0f, 0));
	//glm::mat4 meshRotate = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	_skyboxObject.transformMatrix = meshScale;

	_sceneParameters.dirLight.direction = glm::vec4(glm::normalize(glm::vec3(0.0f, -30.0f, -10.0f)), 1.0f);
	_sceneParameters.dirLight.color = glm::vec4{ 253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f, 1.0f };
	_sceneParameters.ambientLight = { 1.0f, 1.0f, 1.0f, 0.1f };

	PointLight centralLight = {};
	centralLight.position = glm::vec4{ _centralLightPos, 0.0f };
	centralLight.color = glm::vec4{ 253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f, 300.0f };
	_sceneParameters.pointLights[0] = centralLight;

	// turning front and back lights off (w = 0.0) to better highlight central and directional light,
	// but keeping the possibility to turn it on if needed
	PointLight frontLight = {};
	frontLight.position = glm::vec4{ 2.8f, 3.0f, -5.0f, 0.0f };
	frontLight.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 0.0f };

	PointLight backLight = {};
	backLight.position = glm::vec4{ 2.8f, 3.0f, 28.0f, 0.0f };
	backLight.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 0.0f };
	_sceneParameters.pointLights[1] = frontLight;
	_sceneParameters.pointLights[2] = backLight;
	
	// create descriptor set for texture(s)

	MaterialSet* texturedMatSet = get_material_set("geometrypass");

	std::array<uint32_t, NUM_TEXTURE_TYPES> textureVariableDescCounts = {
		static_cast<uint32_t>(_scene._diffuseTexNames.size()),
		static_cast<uint32_t>(_scene._metallicTexNames.size()),
		static_cast<uint32_t>(_scene._roughnessTexNames.size()),
		static_cast<uint32_t>(_scene._normalMapNames.size())
	};

	std::array<vk::DescriptorSetLayout, NUM_TEXTURE_TYPES> textureSetLayouts = {
		_textureSetLayout,
		_textureSetLayout,
		_textureSetLayout,
		_textureSetLayout
	};

	vk::StructureChain<vk::DescriptorSetAllocateInfo, vk::DescriptorSetVariableDescriptorCountAllocateInfo> descCntC;

	auto& variableDescriptorCountAllocInfo =
		descCntC.get<vk::DescriptorSetVariableDescriptorCountAllocateInfo>();
	variableDescriptorCountAllocInfo.setDescriptorCounts(textureVariableDescCounts);

	auto& allocInfo = descCntC.get<vk::DescriptorSetAllocateInfo>();
	allocInfo.setDescriptorPool(_descriptorPool);
	allocInfo.setSetLayouts(textureSetLayouts);

	_texDescriptorSets = _device.allocateDescriptorSets(allocInfo);

	texturedMatSet->diffuseTextureSet = _texDescriptorSets[DIFFUSE_TEX_SLOT];
	texturedMatSet->metallicTextureSet = _texDescriptorSets[METALLIC_TEX_SLOT];
	texturedMatSet->roughnessTextureSet = _texDescriptorSets[ROUGHNESS_TEX_SLOT];
	texturedMatSet->normalMapSet = _texDescriptorSets[NORMAL_MAP_SLOT];

	vk::DescriptorSetAllocateInfo skyboxAllocInfo = {};
	skyboxAllocInfo.setDescriptorPool(_descriptorPool);
	skyboxAllocInfo.setSetLayouts(_cubemapSetLayout);

	MaterialSet* skyboxMatSet = get_material_set("skybox");
	skyboxMatSet->skyboxSet = _device.allocateDescriptorSets(skyboxAllocInfo)[0];

	// fill diffuse descriptor set

	std::vector<vk::DescriptorImageInfo> diffuseImageBufferInfos;
	fill_tex_descriptor_sets(diffuseImageBufferInfos, _scene._diffuseTexNames, pSceneModel, DIFFUSE_TEX_SLOT);

	// fill metallic descriptor set

	std::vector<vk::DescriptorImageInfo> metallicImageBufferInfos;
	fill_tex_descriptor_sets(metallicImageBufferInfos, _scene._metallicTexNames, pSceneModel, METALLIC_TEX_SLOT);

	// fill roughness descriptor set

	std::vector<vk::DescriptorImageInfo> roughnessImageBufferInfos;
	fill_tex_descriptor_sets(roughnessImageBufferInfos, _scene._roughnessTexNames, pSceneModel, ROUGHNESS_TEX_SLOT);

	// fill normal map descriptor set

	std::vector<vk::DescriptorImageInfo> normalMapImageBufferInfos;
	fill_tex_descriptor_sets(normalMapImageBufferInfos, _scene._normalMapNames, pSceneModel, NORMAL_MAP_SLOT);

	// fill cubemap descriptor set

	vk::DescriptorImageInfo skyboxDescImage;
	vk::SamplerCreateInfo skyboxSamplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear);
	vk::Sampler skyboxSampler = _device.createSampler(skyboxSamplerInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroySampler(skyboxSampler);
		});

	skyboxDescImage.sampler = skyboxSampler;
	skyboxDescImage.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	skyboxDescImage.imageView = _skybox.imageView;

	// write to descriptor sets

	std::array<vk::WriteDescriptorSet, NUM_TEXTURE_TYPES> textureSetWrites;

	textureSetWrites[DIFFUSE_TEX_SLOT] = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		texturedMatSet->diffuseTextureSet, diffuseImageBufferInfos.data(), 0, static_cast<uint32_t>(diffuseImageBufferInfos.size()));

	textureSetWrites[METALLIC_TEX_SLOT] = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		texturedMatSet->metallicTextureSet, metallicImageBufferInfos.data(), 0, static_cast<uint32_t>(metallicImageBufferInfos.size()));

	textureSetWrites[ROUGHNESS_TEX_SLOT] = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		texturedMatSet->roughnessTextureSet, roughnessImageBufferInfos.data(), 0, static_cast<uint32_t>(roughnessImageBufferInfos.size()));

	textureSetWrites[NORMAL_MAP_SLOT] = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		texturedMatSet->normalMapSet, normalMapImageBufferInfos.data(), 0, static_cast<uint32_t>(normalMapImageBufferInfos.size()));

	_device.updateDescriptorSets(textureSetWrites, {});

	vk::WriteDescriptorSet skyboxSetWrite;

	skyboxSetWrite = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		skyboxMatSet->skyboxSet, &skyboxDescImage, 0);

	_device.updateDescriptorSets(skyboxSetWrite, {});
}

void VulkanEngine::upload_mesh(Mesh& mesh)
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

void VulkanEngine::cleanup()
{	
	if (_isInitialized) {
		--_frameNumber;
		VK_CHECK(_device.waitForFences(get_current_frame()._renderFence, true, 1000000000));
		++_frameNumber;

		_mainDeletionQueue.flush();

		vmaDestroyAllocator(_allocator);
		_device.destroy();
		_instance.destroySurfaceKHR(_surface);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		_instance.destroy();
		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
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

	switch_frame_image_layout(_prevFrameImage, cmd);
	
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

		draw_lighting_pass(cmd, _lightingPassPipelineLayout, _lightingPassPipeline);

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
		postPassSets.push_back(get_current_frame()._postprocessDescriptorSet);

		draw_screen_quad(cmd, _postPassPipelineLayout, _postPassPipeline, postPassSets);

		cmd.endRendering();
	}
	else if (_renderMode == RenderMode::ePathTracing)
	{
		cmd.beginRendering(mvPassRenderInfo);

		std::vector<vk::DescriptorSet> mvPassSets = {
			_globalDescriptor
		};

		draw_screen_quad(cmd, _mvPassPipelineLayout, _mvPassPipeline, mvPassSets);

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

		copy_image(cmd, vk::ImageAspectFlagBits::eColor, _intermediateImage, vk::ImageLayout::eTransferSrcOptimal,
			_prevFrameImage, vk::ImageLayout::eTransferDstOptimal, _windowExtent3D);

		image_layout_transition(cmd, vk::AccessFlagBits::eColorAttachmentWrite, {},
			vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, _intermediateImage,
			vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eVertexShader);

		switch_intermediate_image_layout(cmd, false);

		switch_swapchain_image_layout(cmd, swapchainImageIndex, true);

		cmd.beginRendering(postPassRenderInfo);

		std::vector<vk::DescriptorSet> postPassSets;
		postPassSets.push_back(get_current_frame()._postprocessDescriptorSet);

		draw_screen_quad(cmd, _postPassPipelineLayout, _postPassPipeline, postPassSets);

		cmd.endRendering();
	}

	// ======================================== END RENDERING ========================================

	// transfer swapchain image to presentable layout

	switch_swapchain_image_layout(cmd, swapchainImageIndex, false);

	// stop recording to command buffer (we can no longer add commands, but it can now be submitted and executed)
	cmd.end();

	_prevCamera = _camera;

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

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
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

vk::DeviceSize VulkanEngine::align_up(vk::DeviceSize originalSize, vk::DeviceSize alignment)
{
	vk::DeviceSize alignedSize = originalSize;
	if (alignment > 0)
	{
		alignedSize = (alignedSize + alignment - 1) & ~(alignment - 1);
	}
	return alignedSize;
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, vk::BufferUsageFlags usage, VmaMemoryUsage memUsage)
{
	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memUsage;

	AllocatedBuffer newBuffer;

	VkBuffer cBuffer;
	VK_CHECK( vmaCreateBuffer(_allocator, &(static_cast<VkBufferCreateInfo>(bufferInfo)), &vmaAllocInfo,
		&cBuffer, &newBuffer._allocation, nullptr) );

	newBuffer._buffer = static_cast<vk::Buffer>(cBuffer);
	return newBuffer;
}

AllocatedImage VulkanEngine::create_image(const vk::ImageCreateInfo& createInfo, VmaMemoryUsage memUsage)
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

void VulkanEngine::copy_image(vk::CommandBuffer cmd, vk::ImageAspectFlags aspectMask, vk::Image srcImage,
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

vk::DeviceAddress VulkanEngine::get_buffer_device_address(vk::Buffer buffer)
{
	vk::BufferDeviceAddressInfo bufferAddressInfo;
	bufferAddressInfo.setBuffer(buffer);
	return _device.getBufferAddress(bufferAddressInfo);
}

BLASInput VulkanEngine::convert_to_blas_input(Mesh& mesh)
{
	vk::DeviceAddress vertexAddress = get_buffer_device_address(mesh._vertexBuffer._buffer);
	vk::DeviceAddress indexAddress = get_buffer_device_address(mesh._indexBuffer._buffer);

	uint32_t maxVertices = static_cast<uint32_t>(mesh._indices.size());
	uint32_t maxPrimCount = static_cast<uint32_t>(mesh._indices.size()) / 3;

	BLASInput blasInput = {};

	blasInput._triangles.vertexFormat = vk::Format::eR32G32B32A32Sfloat;
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

MaterialSet* VulkanEngine::create_material_set(vk::Pipeline pipeline, vk::PipelineLayout layout, const std::string& name)
{
	MaterialSet matSet;
	matSet.pipeline = pipeline;
	matSet.pipelineLayout = layout;
	_materialSets[name] = matSet;
	return &_materialSets[name];
}

MaterialSet* VulkanEngine::get_material_set(const std::string& name)
{
	auto it = _materialSets.find(name);
	if (it == _materialSets.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}

Model* VulkanEngine::get_model(const std::string& name)
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

void VulkanEngine::upload_cam_scene_data(vk::CommandBuffer cmd, RenderObject* first, size_t count)
{
	glm::mat4 view = _camera.get_view_matrix();

	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(_camera._zoom),
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	GPUCameraData camData = {};
	camData.view = view;
	camData.invView = glm::inverse(view);
	camData.proj = projection;
	camData.viewproj = projection * view;
	camData.invProj = glm::inverse(projection);
	camData.invViewProj = glm::inverse(camData.viewproj);
	
	glm::mat4 prevView = _prevCamera.get_view_matrix();
	glm::mat4 prevProjection = glm::perspectiveRH_ZO(glm::radians(_prevCamera._zoom),
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	prevProjection[1][1] *= -1;
	
	camData.prevViewProj = prevProjection * prevView;

	_sceneParameters.pointLights[0].position = glm::vec4{ _centralLightPos, 0.0f };

	char* data;

	vmaMapMemory(_allocator, _camSceneBuffer._allocation, (void**)&data);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	data += pad_uniform_buffer_size(sizeof(GPUCameraData) + sizeof(GPUSceneData)) * frameIndex;
	memcpy(data, &camData, sizeof(GPUCameraData));
	data += sizeof(GPUCameraData);
	memcpy(data, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _camSceneBuffer._allocation);


	void* objectData;

	vmaMapMemory(_allocator, get_current_frame()._objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = reinterpret_cast<GPUObjectData*>(objectData);

	for (int i = 0; i < count; ++i)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;
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

void VulkanEngine::draw_objects(vk::CommandBuffer cmd, RenderObject* first, size_t count)
{
	glm::mat4 view = _camera.get_view_matrix();

	glm::mat4 projection = glm::perspective(glm::radians(_camera._zoom), 
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
			uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(GPUCameraData) +
				sizeof(GPUSceneData)) * frameIndex);

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 0,
				_globalDescriptor, uniformOffset);

			// object descriptor
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 1,
				get_current_frame()._objectDescriptor, {});

			// diffuse texture descriptor
			if (object.materialSet->diffuseTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + DIFFUSE_TEX_SLOT, object.materialSet->diffuseTextureSet, {});
			}

			// metallic texture descriptor
			if (object.materialSet->metallicTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + METALLIC_TEX_SLOT, object.materialSet->metallicTextureSet, {});
			}

			// roughness texture descriptor
			if (object.materialSet->roughnessTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + ROUGHNESS_TEX_SLOT, object.materialSet->roughnessTextureSet, {});
			}

			// normal map descriptor
			if (object.materialSet->normalMapSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + NORMAL_MAP_SLOT, object.materialSet->normalMapSet, {});
			}

			// skybox texture descriptor
			if (object.materialSet->skyboxSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2, object.materialSet->skyboxSet, {});
			}
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

void VulkanEngine::draw_lighting_pass(vk::CommandBuffer cmd, vk::PipelineLayout pipelineLayout, vk::Pipeline pipeline)
{
	glm::mat4 view = _camera.get_view_matrix();

	glm::mat4 projection = glm::perspective(glm::radians(_camera._zoom),
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	GPUCameraData camData = {};
	camData.view = view;
	camData.invView = glm::inverse(view);
	camData.proj = projection;
	camData.viewproj = projection * view;

	_sceneParameters.pointLights[0].position = glm::vec4{ _centralLightPos, 0.0f };

	char* data;

	vmaMapMemory(_allocator, _camSceneBuffer._allocation, (void**)&data);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	data += pad_uniform_buffer_size(sizeof(GPUCameraData) + sizeof(GPUSceneData)) * frameIndex;
	memcpy(data, &camData, sizeof(GPUCameraData));
	data += sizeof(GPUCameraData);
	memcpy(data, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _camSceneBuffer._allocation);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

	// scene & camera descriptor set
	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(GPUCameraData) +
		sizeof(GPUSceneData)) * frameIndex);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, _globalDescriptor, uniformOffset);

	// gbuffer descriptor set
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 1, get_current_frame()._gBufferDescriptorSet, {});

	// TLAS descriptor set
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 2, _tlasDescriptorSet, {});

	cmd.draw(3, 1, 0, 0);
}

void VulkanEngine::draw_screen_quad(vk::CommandBuffer cmd, vk::PipelineLayout pipelineLayout, vk::Pipeline pipeline,
	const std::vector<vk::DescriptorSet>& descriptorSets)
{
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

	int frameIndex = _frameNumber % FRAME_OVERLAP;
	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(GPUCameraData) +
		sizeof(GPUSceneData)) * frameIndex);

	for (uint32_t i = 0; i < descriptorSets.size(); ++i)
	{
		if (descriptorSets[i] == _globalDescriptor)
		{
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, i, descriptorSets[i], uniformOffset);
		}
		else
		{
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, i, descriptorSets[i], {});
		}
	}

	cmd.draw(3, 1, 0, 0);
}

void VulkanEngine::draw_skybox(vk::CommandBuffer cmd, RenderObject& object)
{
	glm::mat4 view = _camera.get_view_matrix();

	glm::mat4 projection = glm::perspective(glm::radians(_camera._zoom),
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	GPUCameraData camData = {};
	camData.view = view;
	camData.invView = glm::inverse(view);
	camData.proj = projection;
	camData.viewproj = projection * view;

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	if (!object.model || !object.materialSet)
	{
		return;
	}

	// don't bind already bound pipeline
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, object.materialSet->pipeline);

	// scene & camera descriptor
	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(GPUCameraData) +
		sizeof(GPUSceneData)) * frameIndex);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 0,
		_globalDescriptor, uniformOffset);

	// object descriptor
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 1,
		get_current_frame()._objectDescriptor, {});

	// diffuse texture descriptor
	if (object.materialSet->diffuseTextureSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
			2 + DIFFUSE_TEX_SLOT, object.materialSet->diffuseTextureSet, {});
	}

	// specular texture descriptor
	if (object.materialSet->metallicTextureSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
			2 + METALLIC_TEX_SLOT, object.materialSet->metallicTextureSet, {});
	}

	// specular texture descriptor
	if (object.materialSet->roughnessTextureSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
			2 + ROUGHNESS_TEX_SLOT, object.materialSet->roughnessTextureSet, {});
	}

	// normal map descriptor
	if (object.materialSet->normalMapSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
			2 + NORMAL_MAP_SLOT, object.materialSet->normalMapSet, {});
	}

	// skybox texture descriptor
	if (object.materialSet->skyboxSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
			2, object.materialSet->skyboxSet, {});
	}

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

void VulkanEngine::trace_rays(vk::CommandBuffer cmd, uint32_t swapchainImageIndex)
{
	update_frame();

	if (_rayConstants.frame >= _maxAccumFrames)
	{
		return;
	}

	FrameData& currentFrame = _frames[swapchainImageIndex];

	std::vector<vk::DescriptorSet> rtSets = {
		_rtDescriptorSet, currentFrame._perFrameSetRTX, _globalDescriptor
	};

	for (auto& descSet : _texDescriptorSets)
	{
		rtSets.push_back(descSet);
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, _rtPipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout, RTXSets::eGeneralRTX, _rtDescriptorSet, {});

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout, RTXSets::ePerFrame,
		currentFrame._perFrameSetRTX, {});

	uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(GPUCameraData) +
		sizeof(GPUSceneData)) * frameIndex);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout, RTXSets::eGlobal, _globalDescriptor, uniformOffset);

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout, RTXSets::eObjectData, currentFrame._objectDescriptor, {});

	MaterialSet* materialSet = get_material_set("geometrypass");

	MaterialSet* skyboxMatSet = get_material_set("skybox");

	// diffuse texture descriptor
	if (materialSet->diffuseTextureSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout,
			RTXSets::eDiffuseTex, materialSet->diffuseTextureSet, {});
	}

	// metallic texture descriptor
	if (materialSet->metallicTextureSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout,
			RTXSets::eMetallicTex, materialSet->metallicTextureSet, {});
	}

	// roughness texture descriptor
	if (materialSet->roughnessTextureSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout,
			RTXSets::eRoughnessTex, materialSet->roughnessTextureSet, {});
	}

	// normal map descriptor
	if (materialSet->normalMapSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout,
			RTXSets::eNormalMap, materialSet->normalMapSet, {});
	}

	// skybox descriptor
	if (skyboxMatSet->skyboxSet)
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout,
			RTXSets::eSkybox, skyboxMatSet->skyboxSet, {});
	}

	// upload to GPU
	cmd.pushConstants(_rtPipelineLayout, vk::ShaderStageFlagBits::eRaygenKHR, 0,
		sizeof(RayPushConstants), &_rayConstants);

	cmd.traceRaysKHR(_rgenRegion, _rmissRegion, _rchitRegion, _rcallRegion, _windowExtent.width, _windowExtent.height, 1);
}

void VulkanEngine::on_mouse_motion_callback()
{
	float outRelX = 0;
	float outRelY = 0;

	SDL_GetRelativeMouseState(&outRelX, &outRelY);

	float xOffset = outRelX;
	float yOffset = -outRelY;

	_camera.process_camera_movement(xOffset, yOffset);
}

void VulkanEngine::on_mouse_scroll_callback(float yOffset)
{
	_camera.process_mouse_scroll(yOffset);
}

void VulkanEngine::on_keyboard_event_callback(SDL_Keycode sym)
{
	switch (sym)
	{
	case SDLK_LSHIFT:
		_camera.process_keyboard(CameraMovement::UP, _deltaTime);
		break;
	case SDLK_LCTRL:
		_camera.process_keyboard(CameraMovement::DOWN, _deltaTime);
		break;
	case SDLK_w:
		_camera.process_keyboard(CameraMovement::FORWARD, _deltaTime);
		break;
	case SDLK_s:
		_camera.process_keyboard(CameraMovement::BACKWARD, _deltaTime);
		break;
	case SDLK_a:
		_camera.process_keyboard(CameraMovement::LEFT, _deltaTime);
		break;
	case SDLK_d:
		_camera.process_keyboard(CameraMovement::RIGHT, _deltaTime);
		break;
	case SDLK_RSHIFT:
		_centralLightPos.y += _camSpeed;
		break;
	case SDLK_RCTRL:
		_centralLightPos.y -= _camSpeed;
		break;
	case SDLK_UP:
		_centralLightPos.z -= _camSpeed;
		break;
	case SDLK_DOWN:
		_centralLightPos.z += _camSpeed;
		break;
	case SDLK_LEFT:
		_centralLightPos.x -= _camSpeed;
		break;
	case SDLK_RIGHT:
		_centralLightPos.x += _camSpeed;
		break;
	default:
		break;
	}
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;
	bool keyDown = false;
	bool mouseMotion = false;
	bool mouseWheel = false;
	float scrollY = 0.0f;
	SDL_Keycode sym = 0;
	// main loop
	SDL_SetRelativeMouseMode(SDL_TRUE);
	while (!bQuit)
	{
		float curFrameTime = static_cast<float>(SDL_GetTicks() / 1000.0f);
		_deltaTime = curFrameTime - _lastFrameTime;
		_lastFrameTime = curFrameTime;

		// Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			// SDL window callback processing
			switch (e.type)
			{
			case SDL_EVENT_QUIT:
				bQuit = true;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				mouseMotion = true;
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				mouseWheel = true;
				scrollY = e.wheel.y;
				break;
			case SDL_EVENT_KEY_DOWN:
				sym = e.key.keysym.sym;
				keyDown = true;
				break;
			case SDL_EVENT_KEY_UP:
				keyDown = false;
				break;
			default:
				break;
			}
		}
		if (mouseMotion)
		{
			on_mouse_motion_callback();
		}
		if (mouseWheel)
		{
			on_mouse_scroll_callback(scrollY);
			scrollY = 0.0f;
		}
		if (keyDown)
		{
			on_keyboard_event_callback(sym);
		}

		draw();
	}
}

bool VulkanEngine::load_shader_module(const char* filePath, vk::ShaderModule* outShaderModule)
{
	// open the file, put cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	// get size of file by looking at the cursor location (gives size in bytes)
	size_t fileSize = static_cast<size_t>(file.tellg());

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	// put cursor at the beginning of the file
	file.seekg(0);

	// load file into buffer
	file.read((char*)buffer.data(), fileSize);

	file.close();

	// create a shader module using the buffer
	vk::ShaderModuleCreateInfo smCreateInfo = vkinit::sm_create_info(buffer);
	
	vk::ShaderModule shaderModule = _device.createShaderModule(smCreateInfo);
	if (!shaderModule)
	{
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
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