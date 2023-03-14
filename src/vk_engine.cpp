#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vk_types.h"
#include "vk_initializers.h"

#include "vk_textures.h"

// This will make initialization much less of a pain
#include "VkBootstrap.h"

#include <iostream>
#include <fstream>

#include <unordered_map>

#include <glm/gtx/transform.hpp>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define VK_CHECK(x)														\
	do																	\
	{																	\
		VkResult err = x;												\
		if (err)														\
		{																\
			std::cout << "Detected Vulkan error: " << err << std::endl;	\
			abort();													\
		}																\
	} while (0)

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	
	_window = SDL_CreateWindow(
		"Plume",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
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

	init_sync_structures();

	init_descriptors();

	init_pipelines();

	load_images();

	load_meshes();

	init_scene();

	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Plume Start")
		.request_validation_layers(true)
		.require_api_version(1, 2, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	// store instance
	_instance = vkb_inst.instance;
	// store debug messenger
	_debug_messenger = vkb_inst.debug_messenger;

	// get surface of the window we opened with SDL
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	// use VkBootstrap to select a GPU
	// the GPU should be able to write to SDL surface and support Vulkan 1.2
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 2)
		.set_surface(_surface)
		.select()
		.value();

	// create final Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {};
	shaderDrawParametersFeatures.pNext = nullptr;
	shaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	shaderDrawParametersFeatures.shaderDrawParameters = VK_TRUE;

	vkb::Device vkbDevice = deviceBuilder.add_pNext(&shaderDrawParametersFeatures).build().value();

	// get VkDevice handle for use in the rest of the application
	_chosenGPU = physicalDevice.physical_device;
	_device = vkbDevice.device;

	_gpuProperties = physicalDevice.properties;

	// get Graphics-capable queue using VkBootstrap
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initialize Vulkan memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);
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
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = vkbSwapchain.image_format;

	_mainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		});

	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo dImageInfo = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo dimage_allocinfo = {};
	dimage_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimage_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &dImageInfo, &dimage_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::image_view_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK( vkCreateImageView(_device, &dview_info, nullptr, &_depthImageView) );

	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
	});
}

FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);

	VK_CHECK( vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool) );

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
		});

	// default instant commands buffer

	VkCommandBufferAllocateInfo upCmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);

	VK_CHECK( vkAllocateCommandBuffers(_device, &upCmdAllocInfo, &_uploadContext._commandBuffer) );

	// create a command pool for rendering

	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		VK_CHECK( vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool) );

		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK( vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer) );
		_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
		});
	}
}

void VulkanEngine::init_default_renderpass()
{
	// the render pass will use this color attachment
	VkAttachmentDescription color_attachment = {};

	// use format needed by the swapchain 
	color_attachment.format = _swapchainImageFormat;

	// number of samples (for MSAA, for instance)
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;

	// clear when we load this attachment
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	// keep the stored attachment when render pass ends
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// at the start we don't care about image layout format
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// when the render pass ends (the image is rendered), we need to be able to present it optimally
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	// index into pAttachments of the parent render pass
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// create only 1 subpass, which is the minimum
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkSubpassDependency color_dependency = {};
	color_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	color_dependency.dstSubpass = 0;
	color_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	color_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	color_dependency.srcAccessMask = 0;
	color_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depth_dependency = {};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.srcAccessMask = 0;
	depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { color_dependency, depth_dependency };

	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	// connect color attachment to render pass
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	// connect dependencies
	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];
	// connect subpass
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VK_CHECK( vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass) );
	
	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, _renderPass, nullptr);
		});
}

void VulkanEngine::init_framebuffers()
{
	// create framebuffers for the swapchain images, connecting render pass to images
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = _renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;
	
	// grab number of images from swapchain
	const uint32_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	// create a framebuffer for each swapchain image view
	for (size_t i = 0; i < swapchain_imagecount; ++i) {
		VkImageView attachments[2] = { _swapchainImageViews[i], _depthImageView };

		fb_info.pAttachments = &attachments[0];
		fb_info.attachmentCount = 2;

		VK_CHECK( vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]) );

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();

	VK_CHECK( vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext._uploadFence) );

	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
		});

	// create in the signaled state, so that the first wait call returns immediately
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		VK_CHECK( vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence) );

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
		});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));

		_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			});
	}
}

void VulkanEngine::init_descriptors()
{
	std::vector<VkDescriptorPoolSize> poolSizes = { 
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pNext = nullptr;
	poolInfo.flags = 0;
	poolInfo.maxSets = 10;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);

	VkDescriptorSetLayoutBinding camSceneBufferBinding = vkinit::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutBinding bindings[] = { camSceneBufferBinding };

	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.pNext = nullptr;

	setInfo.bindingCount = 1;
	setInfo.flags = 0;
	setInfo.pBindings = bindings;

	vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);


	VkDescriptorSetLayoutBinding objectBufferBinding = vkinit::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	
	VkDescriptorSetLayoutCreateInfo objSetInfo = {};
	objSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	objSetInfo.pNext = nullptr;

	objSetInfo.bindingCount = 1;
	objSetInfo.flags = 0;
	objSetInfo.pBindings = &objectBufferBinding;

	vkCreateDescriptorSetLayout(_device, &objSetInfo, nullptr, &_objectSetLayout);


	const size_t camSceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUCameraData) + sizeof(GPUSceneData));

	_camSceneBuffer = create_buffer(camSceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_globalSetLayout;

	vkAllocateDescriptorSets(_device, &allocInfo, &_globalDescriptor);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		constexpr int MAX_OBJECTS = 10000;
		_frames[i]._objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorSetAllocateInfo objSetAllocInfo = {};
		objSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objSetAllocInfo.pNext = nullptr;
		objSetAllocInfo.descriptorPool = _descriptorPool;
		objSetAllocInfo.descriptorSetCount = 1;
		objSetAllocInfo.pSetLayouts = &_objectSetLayout;

		vkAllocateDescriptorSets(_device, &objSetAllocInfo, &_frames[i]._objectDescriptor);

		VkDescriptorBufferInfo camSceneInfo = {};
		camSceneInfo.buffer = _camSceneBuffer._buffer;
		camSceneInfo.offset = 0;
		camSceneInfo.range = sizeof(GPUCameraData) + sizeof(GPUSceneData);

		VkDescriptorBufferInfo objBufferInfo = {};
		objBufferInfo.buffer = _frames[i]._objectBuffer._buffer;
		objBufferInfo.offset = 0;
		objBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		VkWriteDescriptorSet camSceneWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, _globalDescriptor, &camSceneInfo, 0);
		
		VkWriteDescriptorSet objectWrite = vkinit::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _frames[i]._objectDescriptor, &objBufferInfo, 0);

		VkWriteDescriptorSet setWrites[] = { camSceneWrite, objectWrite };

		vkUpdateDescriptorSets(_device, 2, setWrites, 0, nullptr);
		
		_mainDeletionQueue.push_function([=]() {
			vmaDestroyBuffer(_allocator, _frames[i]._objectBuffer._buffer, _frames[i]._objectBuffer._allocation);
		});
	}

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, _camSceneBuffer._buffer, _camSceneBuffer._allocation);
		vkDestroyDescriptorSetLayout(_device, _globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _objectSetLayout, nullptr);
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		});
}

void VulkanEngine::init_pipelines()
{
	VkShaderModule triangleFragShader;
	if (!load_shader_module("../../shaders/default_lit.frag.spv", &triangleFragShader))
	{
		std::cout << "Error building triangle fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Triangle fragment shader loaded successfully" << std::endl;
	}


	// build pipeline layout with push constants
	VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	VkPushConstantRange pushConstant;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(MeshPushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = { _globalSetLayout, _objectSetLayout };

	meshPipelineLayoutInfo.setLayoutCount = 2;
	meshPipelineLayoutInfo.pSetLayouts = setLayouts;

	VK_CHECK( vkCreatePipelineLayout(_device, &meshPipelineLayoutInfo, nullptr, &_meshPipelineLayout) );


	// build stage_create_info for vertex and fragment stages
	// this lets pipeline know about shader modules
	PipelineBuilder pipelineBuilder;

	// not using tricky vertex input yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	// draw triangle lists via input assembly
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	// use swapchain extents to build viewport and scissor
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = static_cast<float>(_windowExtent.width);
	pipelineBuilder._viewport.height = static_cast<float>(_windowExtent.height);
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;
	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	// draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	// no multisampling
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	// no blending, write to rgba
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	// default depth testing
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);


	// build mesh pipeline 

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	// use the vertex info with the pipeline builder
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size(); 

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	pipelineBuilder._shaderStages.clear(); // clean up shader stages used previously

	VkShaderModule meshVertShader;
	if (!load_shader_module("../../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error building triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Mesh Triangle vertex shader loaded successfully" << std::endl;
	}

	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));
	pipelineBuilder._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));
	pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	VkPipeline meshPipeline = pipelineBuilder.buildPipeline(_device, _renderPass);

	create_material(meshPipeline, _meshPipelineLayout, "defaultmesh");

	// shader modules are now built into the pipelines, we don't need them anymore

	vkDestroyShaderModule(_device, meshVertShader, nullptr);
	vkDestroyShaderModule(_device, triangleFragShader, nullptr);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyPipeline(_device, meshPipeline, nullptr);

		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		});


}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBuffer cmd = _uploadContext._commandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK( vkBeginCommandBuffer(cmd, &cmdBeginInfo) );

	function(cmd);

	VK_CHECK( vkEndCommandBuffer(cmd) );

	VkSubmitInfo submitInfo = vkinit::submit_info(&cmd);


	VK_CHECK( vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _uploadContext._uploadFence) );

	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext._uploadFence);

	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
}

void VulkanEngine::load_meshes()
{
	Mesh triangleMesh;
	Mesh monkeyMesh;

	// set number of vertices
	triangleMesh._vertices.resize(3);

	// set positions
	triangleMesh._vertices[0].position = { 1.0f, 1.0f, 0.0f };
	triangleMesh._vertices[1].position = { -1.0f, 1.0f, 0.0f };
	triangleMesh._vertices[2].position = { 0.0f, -1.0f, 0.0f };

	triangleMesh._vertices[0].color = { 0.5f, 0.0f, 0.5f };
	triangleMesh._vertices[1].color = { 0.5f, 0.0f, 0.5f };
	triangleMesh._vertices[2].color = { 0.5f, 0.0f, 0.5f };

	monkeyMesh.load_from_obj("../../assets/monkey_smooth.obj");

	upload_mesh(triangleMesh);
	upload_mesh(monkeyMesh);

	_meshes["monkey"] = monkeyMesh;
	_meshes["triangle"] = triangleMesh;
}

void VulkanEngine::load_images()
{
	Texture lostEmpire;

	vkutil::load_image_from_file(*this, "../../assets/lost_empire-RGBA.png", lostEmpire.image);

	VkImageViewCreateInfo imageViewInfo = vkinit::image_view_create_info(VK_FORMAT_R8G8B8A8_SRGB, lostEmpire.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(_device, &imageViewInfo, nullptr, &lostEmpire.imageView);

	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, lostEmpire.imageView, nullptr);
		});

	_loadedTextures["empire_diffuse"] = lostEmpire;
}

void VulkanEngine::init_scene()
{
	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::mat4{ 1.0f };

	_renderables.push_back(monkey);

	for (int x = -30; x <= 30; ++x)
	{
		for (int y = -30; y <= 30; ++y)
		{
			RenderObject triangle;
			triangle.mesh = get_mesh("triangle");
			triangle.material = get_material("defaultmesh");
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0f }, glm::vec3(x, 0, y));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(0.2f, 0.2f, 0.2f));
			triangle.transformMatrix = translation * scale;

			_renderables.push_back(triangle);
		}
	}
}

void VulkanEngine::upload_mesh(Mesh& mesh)
{
	const size_t bufferSize = mesh._vertices.size() * sizeof(Vertex);

	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	// write on CPU, read on GPU
	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK( vmaCreateBuffer(_allocator, &stagingBufferInfo, &vmaAllocInfo, &stagingBuffer._buffer, &stagingBuffer._allocation, nullptr) );

	// copy vertex data
	void* data;
	vmaMapMemory(_allocator, stagingBuffer._allocation, &data);

	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));

	vmaUnmapMemory(_allocator, stagingBuffer._allocation);

	// allocate Vertex Buffer
	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK( vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaAllocInfo, &mesh._vertexBuffer._buffer, &mesh._vertexBuffer._allocation, nullptr) );

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
	});

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

void VulkanEngine::cleanup()
{	
	if (_isInitialized) {
		--_frameNumber;
		vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000);
		++_frameNumber;

		_mainDeletionQueue.flush();

		vmaDestroyAllocator(_allocator);
		vkDestroyDevice(_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{
	// wait until the GPU has finished rendering the last frame, with timeout of 1 second
	VK_CHECK( vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000) );
	VK_CHECK( vkResetFences(_device, 1, &get_current_frame()._renderFence) );

	// request image to draw to, 1 second timeout
	uint32_t swapchainImageIndex;
	VK_CHECK( vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex) );

	// we know that everything finished rendering, so we safely reset the command buffer and reuse it
	VK_CHECK( vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0) );

	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
	
	// begin recording the command buffer, letting Vulkan know that we will submit cmd exactly once per frame
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;

	cmdBeginInfo.pInheritanceInfo = nullptr; // no secondary command buffers
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK( vkBeginCommandBuffer(cmd, &cmdBeginInfo) );

	// make clear color dependent on frame number
	VkClearValue clearValue;
	// float flash = abs(sin(_frameNumber / 120.0f));
	clearValue.color = { { 2.0f / 255.0f, 150.0f / 255.0f, 254.0f / 255.0f, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	// begin main render pass
	// use clear color from above and the image under acquired index
	VkRenderPassBeginInfo rpInfo = vkinit::render_pass_begin_info(_renderPass, _windowExtent, _framebuffers[swapchainImageIndex]);

	// connect clear values
	rpInfo.clearValueCount = 2;

	VkClearValue clearValues[2] = { clearValue, depthClear };

	rpInfo.pClearValues = &clearValues[0];

	std::sort(_renderables.begin(), _renderables.end());

	// ======================================== RENDER PASS ========================================

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_objects(cmd, _renderables.data(), _renderables.size());

	// finish render pass
	vkCmdEndRenderPass(cmd);

	// ======================================== END RENDER PASS ========================================


	// stop recording to command buffer (we can no longer add commands, but it can now be submitted and executed)
	VK_CHECK( vkEndCommandBuffer(cmd) );

	// prepare the VkQueue submission
	// wait for the present semaphore to present image
	// signal the render semaphore, showing that rendering is finished

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &get_current_frame()._presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &get_current_frame()._renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	// submit the buffer
	// render fence blocks until graphics commands are done
	VK_CHECK( vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence) );

	// display image
	// wait on render semaphore so that rendered image is complete 

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK( vkQueuePresentKHR(_graphicsQueue, &presentInfo) );

	++_frameNumber;
}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
{
	// calculate alignment based on min device offset alignment
	size_t minUBOAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUBOAlignment > 0)
	{
		alignedSize = (alignedSize + minUBOAlignment - 1) & ~(minUBOAlignment - 1);
	}
	return alignedSize;
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memUsage)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memUsage;

	AllocatedBuffer newBuffer;

	VK_CHECK( vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer._buffer, &newBuffer._allocation, nullptr) );

	return newBuffer;
}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	_materials[name] = mat;
	return &_materials[name];
}

Material* VulkanEngine::get_material(const std::string& name)
{
	auto it = _materials.find(name);
	if (it == _materials.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}

Mesh* VulkanEngine::get_mesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject* first, int count)
{
	glm::mat4 view = glm::translate(glm::mat4(1.0f), _camPos);

	glm::mat4 projection = glm::perspective(glm::radians(70.0f), _windowExtent.width / static_cast<float>(_windowExtent.height), 0.1f, 200.0f);
	projection[1][1] *= -1;

	GPUCameraData camData;
	camData.view = view;
	camData.proj = projection;
	camData.viewproj = projection * view;

	float framed = _frameNumber / 120.0f;

	_sceneParameters.ambientColor = { sin(framed), 0.0f, cos(framed), 1.0f };
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

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; ++i)
	{
		RenderObject& object = first[i];

		// don't bind already bound pipeline
		if (object.material != lastMaterial)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;
			
			// scene & camera descriptor
			uint32_t uniformOffset = pad_uniform_buffer_size(sizeof(GPUCameraData) + sizeof(GPUSceneData)) * frameIndex;
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &_globalDescriptor, 1, &uniformOffset);

			// object descriptor
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &get_current_frame()._objectDescriptor, 0, nullptr);
		}

		glm::mat4 model = object.transformMatrix;
		// final render matrix
		glm::mat4 mesh_matrix = projection * view * model;

		MeshPushConstants constants;
		constants.render_matrix = object.transformMatrix;

		// upload to GPU
		vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		if (object.mesh != lastMesh)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
		}

		vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, i);
	}
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;
	bool keyDown = false;
	SDL_Keycode sym = 0;
	//main loop
	while (!bQuit)
	{
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			switch (e.type)
			{
			case SDL_QUIT:
				bQuit = true;
				break;
			case SDL_KEYDOWN:
				sym = e.key.keysym.sym;
				keyDown = true;
				break;
			case SDL_KEYUP:
				keyDown = false;
				break;
			default:
				break;
			}
		}
		if (keyDown)
		{
			switch (sym)
			{
			case SDLK_LSHIFT:
				_camPos.y -= _camSpeed;
				break;
			case SDLK_LCTRL:
				_camPos.y += _camSpeed;
				break;
			case SDLK_w:
				_camPos.z += _camSpeed;
				break;
			case SDLK_s:
				_camPos.z -= _camSpeed;
				break;
			case SDLK_a:
				_camPos.x += _camSpeed;
				break;
			case SDLK_d:
				_camPos.x -= _camSpeed;
				break;
			default:
				break;
			}
		}

		draw();
	}
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule)
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
	VkShaderModuleCreateInfo smCreateInfo = vkinit::sm_create_info(buffer);
	
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &smCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}

VkPipeline PipelineBuilder::buildPipeline(VkDevice device, VkRenderPass pass)
{
	// make viewport state from viewpoint + scissor (only one of each for now)
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	// dummy color blending (no blending), just writing to color attachment for now
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	// finally assemble the pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = _shaderStages.size();
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

	VkPipeline newPipeline;

	// check pipeline build errors
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		std::cout << "failed to create pipeline" << std::endl;
		return VK_NULL_HANDLE;
	}
	else
	{
		return newPipeline;
	}
}