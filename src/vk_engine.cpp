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
constexpr static float DRAW_DISTANCE = 3200.0f;

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

	init_default_renderpass();

	init_framebuffers();

	init_raytracing();

	init_sync_structures();

	load_meshes();

	init_descriptors();

	init_pipelines();

	load_images();

	init_scene();

	init_blas();
	
	init_tlas();

	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Plume Start")
		.request_validation_layers(ENABLE_VALIDATION_LAYERS)
		.require_api_version(1, 2, 0)
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
	// the GPU should be able to write to SDL surface and support Vulkan 1.2
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 2)
		.set_surface(_surface)
		.add_required_extension("VK_KHR_acceleration_structure")
		.add_required_extension("VK_KHR_ray_tracing_pipeline")
		.add_required_extension("VK_KHR_ray_query")
		.add_required_extension("VK_KHR_deferred_host_operations")
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
	
	vkb::Device vkbDevice = deviceBuilder.add_pNext(&descIndexingFeatures).add_pNext(&shaderDrawParametersFeatures)
		.add_pNext(&accelFeatures).add_pNext(&rayQueryFeatures).add_pNext(&rtPipelineFeatures)
		.add_pNext(&addressFeatures).build().value();

	// get vk::Device handle for use in the rest of the application
	_chosenGPU = physicalDevice.physical_device;
	_device = vkbDevice.device;

	_gpuProperties = vk::PhysicalDeviceProperties2(physicalDevice.properties);

	// use 8 samples for MSAA by default
	// if the physical device doesn't support MSAA x8, fall back to max supported number of samples

	vk::SampleCountFlags supportedSampleCounts = _gpuProperties.properties.limits.framebufferColorSampleCounts &
		_gpuProperties.properties.limits.framebufferDepthSampleCounts;

	if (!(supportedSampleCounts & vk::SampleCountFlagBits::e8))
	{
		if (supportedSampleCounts & vk::SampleCountFlagBits::e4)
		{
			_msaaSamples = vk::SampleCountFlagBits::e4;
		}
		else if (supportedSampleCounts & vk::SampleCountFlagBits::e2)
		{
			_msaaSamples = vk::SampleCountFlagBits::e2;
		}
		else
		{
			_msaaSamples = vk::SampleCountFlagBits::e1;
		}
	}
	
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
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // set vsync mode
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;

	std::vector<VkImage> swchImages = vkbSwapchain.get_images().value();
	_swapchainImages = std::vector<vk::Image>(swchImages.begin(), swchImages.end());
	std::vector<VkImageView> swchImageViews = vkbSwapchain.get_image_views().value();
	_swapchainImageViews = std::vector<vk::ImageView>(swchImageViews.begin(),swchImageViews.end());

	_swapchainImageFormat = static_cast<vk::Format>(vkbSwapchain.image_format);

	_mainDeletionQueue.push_function([=]() {
		_device.destroySwapchainKHR(_swapchain);
	});

	vk::Extent3D colorImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_colorFormat = _swapchainImageFormat;

	vk::ImageCreateInfo imageInfo = vkinit::image_create_info(_colorFormat, vk::ImageUsageFlagBits::eTransientAttachment
		| vk::ImageUsageFlagBits::eColorAttachment, colorImageExtent, 1, _msaaSamples);
	
	VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);

	VmaAllocationCreateInfo cImageAllocInfo = {};
	cImageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	cImageAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkImage cImage;
	vmaCreateImage(_allocator, &cImageInfo, &cImageAllocInfo, &cImage, &_colorImage._allocation, nullptr);

	_colorImage._image = static_cast<vk::Image>(cImage);
	
	vk::ImageViewCreateInfo cViewInfo = vkinit::image_view_create_info(_colorFormat, _colorImage._image, vk::ImageAspectFlagBits::eColor, 1);
	
	_colorImageView = _device.createImageView(cViewInfo);
	
	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(_colorImageView);
		vmaDestroyImage(_allocator, static_cast<VkImage>(_colorImage._image), _colorImage._allocation);
	});

	vk::Extent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_depthFormat = vk::Format::eD32Sfloat;
	
	vk::ImageCreateInfo dImageInfo = vkinit::image_create_info(_depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
		depthImageExtent, 1, _msaaSamples);

	VmaAllocationCreateInfo dImageAllocInfo = {};
	dImageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dImageAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	VkImage cDepthImage;
	vmaCreateImage(_allocator, &(static_cast<VkImageCreateInfo>(dImageInfo)), &dImageAllocInfo, &cDepthImage, &_depthImage._allocation, nullptr);

	_depthImage._image = static_cast<vk::Image>(cDepthImage);

	vk::ImageViewCreateInfo dViewInfo = vkinit::image_view_create_info(_depthFormat, _depthImage._image, vk::ImageAspectFlagBits::eDepth, 1);
	
	_depthImageView = _device.createImageView(dViewInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyImageView(_depthImageView);
		vmaDestroyImage(_allocator, static_cast<VkImage>(_depthImage._image), _depthImage._allocation);
	});
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

void VulkanEngine::init_default_renderpass()
{
	// the render pass will use this color attachment
	vk::AttachmentDescription color_attachment = {};

	// use format needed by the swapchain 
	color_attachment.format = _swapchainImageFormat;

	// number of samples (for MSAA, for instance)
	color_attachment.samples = _msaaSamples;

	// clear when we load this attachment
	color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
	// keep the stored attachment when render pass ends
	color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
	
	color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	
	// at the start we don't care about image layout format
	color_attachment.initialLayout = vk::ImageLayout::eUndefined;
	
	// when the render pass ends (the image is rendered), it needs to be ready to get resolved
	color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
	
	// create a color attachment to resolve MSAA
	vk::AttachmentDescription color_attachment_resolve = {};

	color_attachment_resolve.format = _swapchainImageFormat;
	color_attachment_resolve.samples = vk::SampleCountFlagBits::e1;
	
	color_attachment_resolve.loadOp = vk::AttachmentLoadOp::eDontCare;
	color_attachment_resolve.storeOp = vk::AttachmentStoreOp::eStore;
	
	color_attachment_resolve.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	color_attachment_resolve.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	
	color_attachment_resolve.initialLayout = vk::ImageLayout::eUndefined;
	color_attachment_resolve.finalLayout = vk::ImageLayout::ePresentSrcKHR;
	
	vk::AttachmentReference color_attachment_resolve_ref = {};
	color_attachment_resolve_ref.attachment = 2;
	color_attachment_resolve_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;
	
	vk::AttachmentReference color_attachment_ref = {};
	// index into pAttachments of the parent render pass
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::AttachmentDescription depth_attachment = {};
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = _msaaSamples;
	depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
	depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
	depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
	depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
	depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	
	vk::AttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	// create only 1 subpass, which is the minimum
	vk::SubpassDescription subpass = {};
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = 1;
	subpass.pResolveAttachments = &color_attachment_resolve_ref;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;
	
	vk::SubpassDependency color_dependency = {};
	color_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	color_dependency.dstSubpass = 0;
	color_dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	color_dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	color_dependency.srcAccessMask = vk::AccessFlagBits::eNone;
	color_dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	
	vk::SubpassDependency depth_dependency = {};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
	depth_dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
	depth_dependency.srcAccessMask = vk::AccessFlagBits::eNone;
	depth_dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	
	vk::SubpassDependency dependencies[2] = { color_dependency, depth_dependency };

	vk::AttachmentDescription attachments[3] = { color_attachment, depth_attachment, color_attachment_resolve };

	vk::RenderPassCreateInfo render_pass_info = {};
	// connect color attachment to render pass
	render_pass_info.attachmentCount = 3;
	render_pass_info.pAttachments = &attachments[0];
	// connect dependencies
	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];
	// connect subpass
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	
	_renderPass = _device.createRenderPass(render_pass_info);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyRenderPass(_renderPass);
	});
}

void VulkanEngine::init_framebuffers()
{
	// create framebuffers for the swapchain images, connecting render pass to images
	vk::FramebufferCreateInfo fb_info = {};
	fb_info.renderPass = _renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;
	
	// grab number of images from swapchain
	const size_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<vk::Framebuffer>(swapchain_imagecount);

	// create a framebuffer for each swapchain image view
	for (size_t i = 0; i < swapchain_imagecount; ++i) {
		vk::ImageView attachments[3] = { _colorImageView, _depthImageView, _swapchainImageViews[i] };

		fb_info.pAttachments = &attachments[0];
		fb_info.attachmentCount = 3;

		_framebuffers[i] = _device.createFramebuffer(fb_info);

		_mainDeletionQueue.push_function([=]() {
			_device.destroyFramebuffer(_framebuffers[i]);
			_device.destroyImageView(_swapchainImageViews[i]);
		});
	}
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
		{ vk::DescriptorType::eCombinedImageSampler, 20 },
		{ vk::DescriptorType::eAccelerationStructureKHR, 1 }
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
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eUniformBufferDynamic,
			vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0);

	vk::DescriptorSetLayoutBinding bindings[] = { camSceneBufferBinding };

	vk::DescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.bindingCount = 1;
	setInfo.pBindings = bindings;

	_globalSetLayout = _device.createDescriptorSetLayout(setInfo);

	vk::DescriptorSetLayoutBinding objectBufferBinding = 
		vkinit::descriptor_set_layout_binding(vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 0);
	
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
		vk::ShaderStageFlagBits::eFragment, 0, static_cast<uint32_t>(_scene._diffuseTexNames.size()));

	vk::StructureChain<vk::DescriptorSetLayoutCreateInfo, vk::DescriptorSetLayoutBindingFlagsCreateInfo> c;

	vk::DescriptorSetLayoutCreateInfo texSetInfo = c.get<vk::DescriptorSetLayoutCreateInfo>();
	texSetInfo.setBindings(textureBind);

	// Allow for usage of unwritten descriptor sets and variable size arrays of textures
	vk::DescriptorBindingFlags bindFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
		vk::DescriptorBindingFlagBits::eVariableDescriptorCount;

	vk::DescriptorSetLayoutBindingFlagsCreateInfo bindFlagsInfo = c.get<vk::DescriptorSetLayoutBindingFlagsCreateInfo>();
	bindFlagsInfo.setBindingFlags(bindFlags);

	_textureSetLayout = _device.createDescriptorSetLayout(texSetInfo);

	// cubemap set layout

	vk::DescriptorSetLayoutBinding cubemapBind = vkinit::descriptor_set_layout_binding(
		vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0);

	vk::DescriptorSetLayoutCreateInfo cubemapSetInfo;
	cubemapSetInfo.setBindings(cubemapBind);

	_cubemapSetLayout = _device.createDescriptorSetLayout(cubemapSetInfo);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		constexpr int MAX_OBJECTS = 10000;
		_frames[i]._objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, 
			vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);

		vk::DescriptorSetAllocateInfo objSetAllocInfo = {};
		objSetAllocInfo.descriptorPool = _descriptorPool;
		objSetAllocInfo.descriptorSetCount = 1;
		objSetAllocInfo.pSetLayouts = &_objectSetLayout;

		_frames[i]._objectDescriptor = _device.allocateDescriptorSets(objSetAllocInfo)[0];

		vk::DescriptorBufferInfo camSceneInfo = {};
		camSceneInfo.buffer = _camSceneBuffer._buffer;
		camSceneInfo.offset = 0;
		camSceneInfo.range = sizeof(GPUCameraData) + sizeof(GPUSceneData);

		vk::DescriptorBufferInfo objBufferInfo = {};
		objBufferInfo.buffer = _frames[i]._objectBuffer._buffer;
		objBufferInfo.offset = 0;
		objBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		vk::WriteDescriptorSet camSceneWrite = vkinit::write_descriptor_buffer(
			vk::DescriptorType::eUniformBufferDynamic, _globalDescriptor, &camSceneInfo, 0);
		
		vk::WriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(
			vk::DescriptorType::eStorageBuffer, _frames[i]._objectDescriptor, &objBufferInfo, 0);
		
		vk::WriteDescriptorSet setWrites[] = { camSceneWrite, objectWrite };

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
		_device.destroyDescriptorPool(_descriptorPool);
	});
}

void VulkanEngine::init_pipelines()
{
	Model* scene = get_model("scene");

	vk::ShaderModule triangleFragShader;
	if (!load_shader_module("../../../shaders/default_lit.frag.spv", &triangleFragShader))
	{
		std::cout << "Error building triangle fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Triangle fragment shader loaded successfully" << std::endl;
	}

	// build pipeline layout with push constants
	vk::PipelineLayoutCreateInfo meshPipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	vk::PushConstantRange pushConstant;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(MeshPushConstants);
	pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex;
	
	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	vk::DescriptorSetLayout setLayouts[] = { _globalSetLayout, _objectSetLayout };

	meshPipelineLayoutInfo.setLayoutCount = 2;
	meshPipelineLayoutInfo.pSetLayouts = setLayouts;

	_meshPipelineLayout = _device.createPipelineLayout(meshPipelineLayoutInfo);

	vk::PipelineLayoutCreateInfo texturedPipelineLayoutInfo = meshPipelineLayoutInfo;

	vk::DescriptorSetLayout texSetLayouts[] = {
		_globalSetLayout, _objectSetLayout, _textureSetLayout, _textureSetLayout, _textureSetLayout,
		_textureSetLayout, _tlasSetLayout
	};

	texturedPipelineLayoutInfo.setSetLayouts(texSetLayouts);

	vk::PipelineLayout texturedPipelineLayout;
	texturedPipelineLayout = _device.createPipelineLayout(texturedPipelineLayoutInfo);

	vk::PipelineLayoutCreateInfo skyboxPipelineLayoutInfo = vkinit::pipeline_layout_create_info();
	
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

	// draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(vk::PolygonMode::eFill);
	
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info(_msaaSamples);

	// no blending, write to rgba
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	// default depth testing
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, vk::CompareOp::eLessOrEqual);

	// build mesh pipeline 

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	// use the vertex info with the pipeline builder
	pipelineBuilder._vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);

	pipelineBuilder._vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

	pipelineBuilder._shaderStages.clear(); // clean up shader stages used previously

	vk::ShaderModule meshVertShader;
	if (!load_shader_module("../../../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error building triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Mesh Triangle vertex shader loaded successfully" << std::endl;
	}

	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		meshVertShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		triangleFragShader));

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

	pipelineBuilder._shaderStages[1].setPSpecializationInfo(&specInfo);

	// build pipeline
	pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	vk::Pipeline meshPipeline = pipelineBuilder.buildPipeline(_device, _renderPass);

	create_material_set(meshPipeline, _meshPipelineLayout, "defaultmesh");

	vk::ShaderModule texturedMeshShader;
	if (!load_shader_module("../../../shaders/textured_lit.frag.spv", &texturedMeshShader))
	{
		std::cout << "Error building textured mesh fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Textured mesh fragment shader loaded successfully" << std::endl;
	}

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		meshVertShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		texturedMeshShader));

	pipelineBuilder._shaderStages[1].setPSpecializationInfo(&specInfo);

	pipelineBuilder._pipelineLayout = texturedPipelineLayout;

	vk::Pipeline texPipeline = pipelineBuilder.buildPipeline(_device, _renderPass);
	create_material_set(texPipeline, texturedPipelineLayout, "texturedmesh");
	
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

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eVertex,
		skyboxVertShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(vk::ShaderStageFlagBits::eFragment,
		skyboxFragShader));
	pipelineBuilder._pipelineLayout = skyboxPipelineLayout;

	vk::Pipeline skyboxPipeline = pipelineBuilder.buildPipeline(_device, _renderPass);
	create_material_set(skyboxPipeline, skyboxPipelineLayout, "skybox");

	// shader modules are now built into the pipelines, we don't need them anymore

	_device.destroyShaderModule(meshVertShader);
	_device.destroyShaderModule(triangleFragShader);
	_device.destroyShaderModule(texturedMeshShader);
	_device.destroyShaderModule(skyboxVertShader);
	_device.destroyShaderModule(skyboxFragShader);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyPipeline(meshPipeline);
		_device.destroyPipeline(texPipeline);
		_device.destroyPipeline(skyboxPipeline);
		
		_device.destroyPipelineLayout(_meshPipelineLayout);
		_device.destroyPipelineLayout(texturedPipelineLayout);
		_device.destroyPipelineLayout(skyboxPipelineLayout);
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
		"right.png", "left.png",
		"top.png", "bottom.png",
		"front.png", "back.png"
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

	for (size_t i = 0; i < _scene._diffuseTexNames.size(); ++i)
	{
		Texture diffuse = {};
		Texture defaultTex = {};

		if (!_scene._diffuseTexNames[i].empty())
		{
			load_material_texture(diffuse, _scene._diffuseTexNames[i], _scene._matNames[i], DIFFUSE_TEX_SLOT);
		}
		else
		{
			vkutil::load_image_from_file(this, "../../../assets/null-texture.png", defaultTex.image);

			vk::ImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(vk::Format::eR8G8B8A8Srgb,
				defaultTex.image._image, vk::ImageAspectFlagBits::eColor, defaultTex.image._mipLevels);
			defaultTex.imageView = _device.createImageView(imageViewInfo);

			_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;

			_mainDeletionQueue.push_function([=]() {
				_device.destroyImageView(defaultTex.imageView);
			});
		}
	}

	for (size_t i = 0; i < _scene._ambientTexNames.size(); ++i)
	{
		Texture ambient = {};

		if (!_scene._ambientTexNames[i].empty())
		{
			load_material_texture(ambient, _scene._ambientTexNames[i], _scene._matNames[i], AMBIENT_TEX_SLOT);
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][AMBIENT_TEX_SLOT] =
				_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT];
		}
	}

	for (size_t i = 0; i < _scene._specularTexNames.size(); ++i)
	{
		Texture specular = {};

		if (!_scene._specularTexNames[i].empty())
		{
			load_material_texture(specular, _scene._specularTexNames[i], _scene._matNames[i], SPECULAR_TEX_SLOT);
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][SPECULAR_TEX_SLOT] =
				_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT];
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
	suzanne.materialSet = get_material_set("texturedmesh");
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

	glm::mat4 sceneTransform = glm::translate(glm::vec3{ 5, -10, 0 })* glm::rotate(glm::radians(90.0f),
		glm::vec3(0.0f, 1.0f, 0.0f))* glm::scale(glm::mat4{ 1.0f }, glm::vec3(0.05f, 0.05f, 0.05f));

	MaterialSet* pSceneMatSet = get_material_set("texturedmesh");

	for (Mesh& mesh : pSceneModel->_meshes)
	{
		RenderObject renderSceneMesh = {};
		renderSceneMesh.model = pSceneModel;
		renderSceneMesh.mesh = &mesh;
		renderSceneMesh.materialSet = pSceneMatSet;
		renderSceneMesh.transformMatrix = sceneTransform;

		_renderables.push_back(std::move(renderSceneMesh));
	}

	RenderObject cube = {};
	cube.model = get_model("cube");
	cube.materialSet = get_material_set("skybox");
	cube.mesh = &(cube.model->_meshes.front());
	glm::mat4 meshScale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(200.0f, 200.0f, 200.0f));
	//glm::mat4 meshTranslate = glm::translate(glm::mat4{ 1.0f }, glm::vec3(2.8f, -8.0f, 0));
	//glm::mat4 meshRotate = glm::rotate(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	cube.transformMatrix = meshScale;

	_renderables.push_back(cube);

	// turning directional light off (w = 0.0) to better highlight point lights,
	// but keeping the possibility to turn it on if needed
	_sceneParameters.dirLight.direction = glm::vec4(glm::normalize(glm::vec3(-14.0f, -3.0f, -1.0f)), 0.0f);
	_sceneParameters.dirLight.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
	_sceneParameters.ambientLight = { 1.0f, 1.0f, 1.0f, 0.05f };

	PointLight centralLight = {};
	centralLight.position = glm::vec4{ _centralLightPos, 0.0f };
	centralLight.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 200.0f };
	_sceneParameters.pointLights[0] = centralLight;

	// turning front light off (w = 0.0) to better highlight central light,
	// but keeping the possibility to turn it on if needed
	PointLight frontLight = {};
	frontLight.position = glm::vec4{ 2.8f, 3.0f, -5.0f, 0.0f };
	frontLight.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 0.0f };

	PointLight backLight = {};
	backLight.position = glm::vec4{ 2.8f, 3.0f, 28.0f, 0.0f };
	backLight.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 150.0f };
	_sceneParameters.pointLights[1] = frontLight;
	_sceneParameters.pointLights[2] = backLight;
	
	// create descriptor set for texture(s)

	MaterialSet* texturedMatSet = get_material_set("texturedmesh");

	std::array<uint32_t, NUM_TEXTURE_TYPES> textureVariableDescCounts = {
		static_cast<uint32_t>(_scene._diffuseTexNames.size()),
		static_cast<uint32_t>(_scene._ambientTexNames.size()),
		static_cast<uint32_t>(_scene._specularTexNames.size()),
		static_cast<uint32_t>(_scene._normalMapNames.size())
	};

	std::array<vk::DescriptorSetLayout, NUM_TEXTURE_TYPES> textureSetLayouts = {
		_textureSetLayout,
		_textureSetLayout,
		_textureSetLayout,
		_textureSetLayout
	};

	vk::StructureChain<vk::DescriptorSetAllocateInfo, vk::DescriptorSetVariableDescriptorCountAllocateInfo> descCntC;

	vk::DescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountAllocInfo =
		descCntC.get<vk::DescriptorSetVariableDescriptorCountAllocateInfo>();
	variableDescriptorCountAllocInfo.setDescriptorCounts(textureVariableDescCounts);

	vk::DescriptorSetAllocateInfo allocInfo = descCntC.get<vk::DescriptorSetAllocateInfo>();
	allocInfo.setDescriptorPool(_descriptorPool);
	allocInfo.setSetLayouts(textureSetLayouts);

	std::vector<vk::DescriptorSet> texDescriptorSets = _device.allocateDescriptorSets(allocInfo);

	texturedMatSet->diffuseTextureSet = texDescriptorSets[DIFFUSE_TEX_SLOT];
	texturedMatSet->ambientTextureSet = texDescriptorSets[AMBIENT_TEX_SLOT];
	texturedMatSet->specularTextureSet = texDescriptorSets[SPECULAR_TEX_SLOT];
	texturedMatSet->normalMapSet = texDescriptorSets[NORMAL_MAP_SLOT];

	vk::DescriptorSetAllocateInfo skyboxAllocInfo = {};
	skyboxAllocInfo.setDescriptorPool(_descriptorPool);
	skyboxAllocInfo.setSetLayouts(_cubemapSetLayout);

	MaterialSet* skyboxMatSet = get_material_set("skybox");
	skyboxMatSet->skyboxSet = _device.allocateDescriptorSets(skyboxAllocInfo)[0];

	// fill diffuse descriptor set

	std::vector<vk::DescriptorImageInfo> diffuseImageBufferInfos;
	fill_tex_descriptor_sets(diffuseImageBufferInfos, _scene._diffuseTexNames, pSceneModel, DIFFUSE_TEX_SLOT);

	// fill ambient descriptor set

	std::vector<vk::DescriptorImageInfo> ambientImageBufferInfos;
	fill_tex_descriptor_sets(ambientImageBufferInfos, _scene._ambientTexNames, pSceneModel, AMBIENT_TEX_SLOT);

	// fill specular descriptor set

	std::vector<vk::DescriptorImageInfo> specularImageBufferInfos;
	fill_tex_descriptor_sets(specularImageBufferInfos, _scene._specularTexNames, pSceneModel, SPECULAR_TEX_SLOT);

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

	textureSetWrites[AMBIENT_TEX_SLOT] = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		texturedMatSet->ambientTextureSet, ambientImageBufferInfos.data(), 0, static_cast<uint32_t>(ambientImageBufferInfos.size()));

	textureSetWrites[SPECULAR_TEX_SLOT] = vkinit::write_descriptor_image(vk::DescriptorType::eCombinedImageSampler,
		texturedMatSet->specularTextureSet, specularImageBufferInfos.data(), 0, static_cast<uint32_t>(specularImageBufferInfos.size()));

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
	const size_t bufferSize = mesh._vertices.size() * sizeof(Vertex);

	vk::BufferCreateInfo vertexStagingBufferInfo = {};
	vertexStagingBufferInfo.size = bufferSize;
	vertexStagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
	
	// write on CPU, read on GPU
	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer vertexStagingBuffer;
	
	VkBufferCreateInfo cVertexStagingBufferInfo = static_cast<VkBufferCreateInfo>(vertexStagingBufferInfo);

	VkBuffer cVertexStagingBuffer;
	VK_CHECK( vmaCreateBuffer(_allocator, &cVertexStagingBufferInfo, &vmaAllocInfo, &cVertexStagingBuffer,
		&vertexStagingBuffer._allocation, nullptr) );
	vertexStagingBuffer._buffer = static_cast<vk::Buffer>(cVertexStagingBuffer);

	// copy vertex data
	void* data;
	vmaMapMemory(_allocator, vertexStagingBuffer._allocation, &data);

	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));

	vmaUnmapMemory(_allocator, vertexStagingBuffer._allocation);

	// allocate Vertex Buffer
	vk::BufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
	
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkBuffer cVertexBuffer;

	VK_CHECK( vmaCreateBuffer(_allocator, &(static_cast<VkBufferCreateInfo>(vertexBufferInfo)), &vmaAllocInfo,
		&cVertexBuffer, &mesh._vertexBuffer._allocation, nullptr) );

	mesh._vertexBuffer._buffer = static_cast<vk::Buffer>(cVertexBuffer);

	immediate_submit([=](vk::CommandBuffer cmd) {
		vk::BufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		cmd.copyBuffer(vertexStagingBuffer._buffer, mesh._vertexBuffer._buffer, copy);
	}, _uploadContext._commandBuffer);

	// now do the same with the Index buffer

	vk::BufferCreateInfo indexStagingBufferInfo = {};
	indexStagingBufferInfo.size = mesh._indices.size() * sizeof(uint32_t);
	indexStagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;

	// write on CPU, read on GPU
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer indexStagingBuffer;

	VkBufferCreateInfo cIndexStagingBufferInfo = static_cast<VkBufferCreateInfo>(indexStagingBufferInfo);

	VkBuffer cIndexStagingBuffer;
	VK_CHECK( vmaCreateBuffer(_allocator, &cIndexStagingBufferInfo, &vmaAllocInfo, &cIndexStagingBuffer,
		&indexStagingBuffer._allocation, nullptr) );
	indexStagingBuffer._buffer = static_cast<vk::Buffer>(cIndexStagingBuffer);

	// copy index data
	void* indData;
	vmaMapMemory(_allocator, indexStagingBuffer._allocation, &indData);

	memcpy(indData, mesh._indices.data(), mesh._indices.size() * sizeof(uint32_t));

	vmaUnmapMemory(_allocator, indexStagingBuffer._allocation);

	vk::BufferCreateInfo indexBufferInfo = {};
	indexBufferInfo.size = mesh._indices.size() * sizeof(uint32_t);
	indexBufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;

	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkBuffer cIndexBuffer;

	VK_CHECK( vmaCreateBuffer(_allocator, &(static_cast<VkBufferCreateInfo>(indexBufferInfo)), &vmaAllocInfo,
		&cIndexBuffer, &mesh._indexBuffer._allocation, nullptr) );

	mesh._indexBuffer._buffer = static_cast<vk::Buffer>(cIndexBuffer);

	immediate_submit([=](vk::CommandBuffer cmd) {
		vk::BufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = mesh._indices.size() * sizeof(uint32_t);
		cmd.copyBuffer(indexStagingBuffer._buffer, mesh._indexBuffer._buffer, copy);
	}, _uploadContext._commandBuffer);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(mesh._vertexBuffer._buffer),
			mesh._vertexBuffer._allocation);
		vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(mesh._indexBuffer._buffer),
			mesh._indexBuffer._allocation);
	});

	vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(vertexStagingBuffer._buffer), vertexStagingBuffer._allocation);
	vmaDestroyBuffer(_allocator, static_cast<VkBuffer>(indexStagingBuffer._buffer), indexStagingBuffer._allocation);
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

	// make clear color dependent on frame number
	vk::ClearValue clearValue;
	// float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = vk::ClearColorValue({ 2.0f / 255.0f, 150.0f / 255.0f, 254.0f / 255.0f, 1.0f });

	vk::ClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	// begin main render pass
	// use clear color from above and the image under acquired index
	vk::RenderPassBeginInfo rpInfo = vkinit::render_pass_begin_info(_renderPass, _windowExtent, _framebuffers[swapchainImageIndex]);

	// connect clear values
	rpInfo.clearValueCount = 2;

	vk::ClearValue clearValues[2] = { clearValue, depthClear };

	rpInfo.pClearValues = &clearValues[0];

	std::sort(_renderables.begin(), _renderables.end());

	// ======================================== RENDER PASS ========================================

	cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

	draw_objects(cmd, _renderables.data(), _renderables.size());

	cmd.endRenderPass();

	// ======================================== END RENDER PASS ========================================


	// stop recording to command buffer (we can no longer add commands, but it can now be submitted and executed)
	VK_CHECK( vkEndCommandBuffer(cmd) );

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

	// The data contains opaque geometry
	blasInput._geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
	blasInput._geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
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

void VulkanEngine::draw_objects(vk::CommandBuffer cmd, RenderObject* first, size_t count)
{
	glm::mat4 view = _camera.get_view_matrix();

	glm::mat4 projection = glm::perspective(glm::radians(_camera._zoom), 
		_windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.view = view;
	camData.invView = glm::inverse(view);
	camData.proj = projection;
	camData.viewproj = projection * view;

	_sceneParameters.pointLights[0].position = glm::vec4{_centralLightPos, 0.0f};

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
	}

	vmaUnmapMemory(_allocator, get_current_frame()._objectBuffer._allocation);

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
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.materialSet->pipeline);
			lastMaterialSet = object.materialSet;

			// scene & camera descriptor
			uint32_t uniformOffset = static_cast<uint32_t>(pad_uniform_buffer_size(sizeof(GPUCameraData) +
				sizeof(GPUSceneData)) * frameIndex);

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 0,
				_globalDescriptor, uniformOffset);

			// object descriptor
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 1,
				get_current_frame()._objectDescriptor, {});

			// TLAS descriptor
			if (object.materialSet->diffuseTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout, 2 + TLAS_SLOT,
					_tlasDescriptorSet, {});
			}

			// diffuse texture descriptor
			if (object.materialSet->diffuseTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + DIFFUSE_TEX_SLOT, object.materialSet->diffuseTextureSet, {});
			}

			// ambient texture descriptor
			if (object.materialSet->ambientTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + AMBIENT_TEX_SLOT, object.materialSet->ambientTextureSet, {});
			}

			// specular texture descriptor
			if (object.materialSet->specularTextureSet)
			{
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.materialSet->pipelineLayout,
					2 + SPECULAR_TEX_SLOT, object.materialSet->specularTextureSet, {});
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

		MeshPushConstants constants;
		constants.render_matrix = object.transformMatrix;
		constants.num_materials = static_cast<glm::uint>(_scene._matNames.size());

		// upload to GPU
		if (!object.materialSet->skyboxSet)
		{
			cmd.pushConstants(object.materialSet->pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
				sizeof(MeshPushConstants), &constants);
		}
		
		if (object.model != lastModel)
		{
			vk::DeviceSize offset = 0;
			cmd.bindVertexBuffers(0, object.mesh->_vertexBuffer._buffer, offset);
			cmd.bindIndexBuffer(object.mesh->_indexBuffer._buffer, offset, vk::IndexType::eUint32);
		}

		cmd.drawIndexed(static_cast<uint32_t>(object.mesh->_indices.size()), 1, 0, 0, i);
	}
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

vk::Pipeline PipelineBuilder::buildPipeline(vk::Device device, vk::RenderPass pass)
{
	// make viewport state from viewpoint + scissor (only one of each for now)
	vk::PipelineViewportStateCreateInfo viewportState = {};
	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	// dummy color blending (no blending), just writing to color attachment for now
	vk::PipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = vk::LogicOp::eCopy;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;
	
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
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
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