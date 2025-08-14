#include "render_core.h"
#include "render_mesh.h"
#include "render_shader.h"
#include "VkBootstrap.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


#ifdef _DEBUG
	constexpr static bool ENABLE_VALIDATION_LAYERS = true;
#else
	constexpr static bool ENABLE_VALIDATION_LAYERS = false;
#endif // _DEBUG


void Render::Image::LayoutTransition(vk::CommandBuffer cmd, const TransitionInfo& info) const
{
	vk::ImageMemoryBarrier transition;
	transition.dstAccessMask = info.dstAccessMask;
	transition.srcAccessMask = info.srcAccessMask;
	transition.oldLayout = info.oldLayout;
	transition.newLayout = info.newLayout;
	transition.image = _handle;

	transition.subresourceRange.aspectMask = _aspectMask;
	transition.subresourceRange.levelCount = _levelCount;
	transition.subresourceRange.layerCount = _layerCount;

	cmd.pipelineBarrier(info.srcStageMask, info.dstStageMask, {}, {}, {}, transition);
}


void Render::Image::LayoutTransition(const TransitionInfo& info) const
{
	auto* backend = Render::Backend::AcquireInstance();

	LayoutTransition(backend->GetCurrentCommandBuffer(), info);
}


void Render::Image::GenerateMipmaps(vk::CommandBuffer cmd) const
{
	vk::ImageMemoryBarrier barrier = {};

	barrier.image = _handle;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = _extent.width;
	int32_t mipHeight = _extent.height;

	for (uint32_t i = 1; i < _levelCount; ++i)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

		vk::ImageBlit blit = {};

		blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
		blit.srcOffsets[1] = vk::Offset3D{ mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
		blit.dstOffsets[1] = vk::Offset3D{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		cmd.blitImage(_handle, vk::ImageLayout::eTransferSrcOptimal, _handle, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);

		if (mipWidth > 1)
		{
			mipWidth /= 2;
		}
		if (mipHeight > 1)
		{
			mipHeight /= 2;
		}
	}

	barrier.subresourceRange.baseMipLevel = _levelCount - 1;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		{}, nullptr, nullptr, barrier);
}


vk::DeviceAddress Render::Buffer::GetDeviceAddress() const
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::BufferDeviceAddressInfo bufferAddressInfo;
	bufferAddressInfo.setBuffer(_handle);
	return backend->GetPDevice()->getBufferAddress(bufferAddressInfo);
}


void Render::Buffer::MemoryBarrier(vk::CommandBuffer cmd, const MemoryBarrierInfo& barrierInfo) const
{
	vk::BufferMemoryBarrier2 barrier;
	barrier.srcAccessMask = barrierInfo.srcAccess;
	barrier.dstAccessMask = barrierInfo.dstAccess;
	barrier.srcStageMask = barrierInfo.srcStage;
	barrier.dstStageMask = barrierInfo.dstStage;
	barrier.buffer = _handle;
	barrier.size = VK_WHOLE_SIZE;

	vk::DependencyInfo depInfo;
	depInfo.setBufferMemoryBarriers(barrier);

	cmd.pipelineBarrier2(depInfo);
}


void Render::Buffer::DestroyManually()
{
	auto* backend = Render::Backend::AcquireInstance();

	vmaDestroyBuffer(backend->_allocator, _handle, _allocation);
}


std::unique_ptr<Render::Backend> Render::Backend::_pInstance = nullptr;
bool Render::Backend::_isInitialized = false;


Render::Backend* Render::Backend::AcquireInstance()
{
	if (!_pInstance)
	{
		_pInstance = std::make_unique<Backend>();
	}
	else
	{
		ASSERT(_isInitialized, "Backend was cleaned up, can't use it anymore.");
	}

	return _pInstance.get();
}


void Render::Backend::Init()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Plume Start")
		.request_validation_layers(ENABLE_VALIDATION_LAYERS)
		.require_api_version(1, 3, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkb_inst.fp_vkGetInstanceProcAddr);
	_libInstance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	VkSurfaceKHR surfaceC;
	SDL_Vulkan_CreateSurface(_pWindow, _libInstance, nullptr, &surfaceC);
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

	_renderCfg.SHADER_EXECUTION_REORDERING = physicalDevice.enable_extension_if_present("VK_NV_ray_tracing_invocation_reorder");

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

	_chosenGPU = physicalDevice.physical_device;
	_device = vkbDevice.device;

	_gpuProperties = vk::PhysicalDeviceProperties2(physicalDevice.properties);

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _libInstance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	VULKAN_HPP_DEFAULT_DISPATCHER.init(_libInstance);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(_device);

	InitSwapchain();
	InitCommands();

	_descMng.init(&_device, &_mainDeletionQueue);

	InitSyncStructures();
	InitRaytracingProperties();
	InitSamplers();
	InitImGui();

	_isInitialized = true;
}


void Render::Backend::Terminate()
{
	--_frameId;
	ASSERT_VK(_device.waitForFences(GetCurrentFrameData()._renderFence, true, 1000000000), "Render fence timeout");
	++_frameId;

	_mainDeletionQueue.flush();

	vmaDestroyAllocator(_allocator);
	_device.destroy();
	_libInstance.destroySurfaceKHR(_surface);
	vkb::destroy_debug_utils_messenger(_libInstance, _debug_messenger);
	_libInstance.destroy();

	_isInitialized = false;
}


Render::Backend::FrameData& Render::Backend::GetCurrentFrameData()
{
	return _frames[_frameId % FRAME_OVERLAP];
}


vk::Sampler Render::Backend::GetSampler(const SamplerType& type) const
{
	auto samplerId = static_cast<size_t>(type);

	return _samplers[samplerId];
}


Render::Image Render::Backend::CreateImage(const Image::CreateInfo& createInfo)
{
	Render::Image resImage;

	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = createInfo.memUsage;

	vk::ImageCreateInfo imageVkCreateInfo;
	imageVkCreateInfo.imageType = vk::ImageType::e2D;

	imageVkCreateInfo.format = createInfo.format;
	resImage._format = createInfo.format;

	if (createInfo.extent == vk::Extent3D(0, 0, 0))
	{
		imageVkCreateInfo.extent = _windowExtent3D;
		resImage._extent = _windowExtent3D;
	}
	else
	{
		imageVkCreateInfo.extent = createInfo.extent;
		resImage._extent = createInfo.extent;
	}

	resImage._layerCount = (createInfo.type == Image::Type::eCubemap) ? 6 : 1;
	resImage._levelCount = createInfo.mipLevels;

	imageVkCreateInfo.mipLevels = createInfo.mipLevels;
	imageVkCreateInfo.arrayLayers = resImage._layerCount;
	imageVkCreateInfo.samples = createInfo.numSamples;
	imageVkCreateInfo.tiling = vk::ImageTiling::eOptimal;
	imageVkCreateInfo.usage = createInfo.usageFlags;
	imageVkCreateInfo.flags = (createInfo.type == Image::Type::eCubemap) ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlagBits(0);

	VkImageCreateInfo imageInfoC = static_cast<VkImageCreateInfo>(imageVkCreateInfo);

	VkImage imageC = {};

	VmaAllocation allocation = {};
	ASSERT_VK(vmaCreateImage(_allocator, &imageInfoC, &imgAllocInfo, &imageC, &allocation, nullptr), "Image creation failed");

	resImage._handle = imageC;


	vk::ImageViewType viewType = {};

	switch (createInfo.type)
	{
	case Image::Type::eRTXOutput:
		[[fallthrough]];
	case Image::Type::eTexture:
		viewType = vk::ImageViewType::e2D;
		break;
	case Image::Type::eCubemap:
		viewType = vk::ImageViewType::eCube;
		break;
	default:
		ASSERT(false, "Invalid image type");
		break;
	}

	vk::ImageViewCreateInfo viewCreateInfo = {};

	viewCreateInfo.viewType = viewType;
	viewCreateInfo.image = resImage._handle;
	viewCreateInfo.format = createInfo.format;
	viewCreateInfo.subresourceRange.baseMipLevel = 0;
	viewCreateInfo.subresourceRange.levelCount = createInfo.mipLevels;
	viewCreateInfo.subresourceRange.baseArrayLayer = 0;
	viewCreateInfo.subresourceRange.layerCount = resImage._layerCount;
	viewCreateInfo.subresourceRange.aspectMask = createInfo.aspectMask;
	resImage._aspectMask = createInfo.aspectMask;

	resImage._view = _device.createImageView(viewCreateInfo);

	_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(_allocator, resImage._handle, allocation);
		_device.destroyImageView(resImage.GetView());
	});

	return resImage;
}


Render::Buffer Render::Backend::CreateBuffer(const Buffer::CreateInfo& createInfo)
{
	Render::Buffer resBuffer;

	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.size = createInfo.allocSize;
	bufferInfo.usage = createInfo.usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = createInfo.memUsage;
	vmaAllocInfo.flags = createInfo.flags;
	vmaAllocInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(createInfo.reqFlags);

	VkBuffer cBuffer;

	auto cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
	ASSERT_VK(vmaCreateBuffer(_allocator, &cBufferInfo, &vmaAllocInfo,
		&cBuffer, &resBuffer._allocation, &resBuffer._allocationInfo), "Buffer creation failed");

	resBuffer._handle = static_cast<vk::Buffer>(cBuffer);

	if (createInfo.isLifetimeManaged)
	{
		_mainDeletionQueue.push_function([=]() {
			vmaDestroyBuffer(_allocator, resBuffer._handle, resBuffer._allocation);
		});
	}

	return resBuffer;
}


void Render::Backend::CopyImage(const Render::Image& srcImage, const Render::Image& dstImage)
{
	vk::ImageCopy copyInfo;
	copyInfo.srcSubresource.aspectMask = srcImage._aspectMask;
	copyInfo.srcSubresource.layerCount = 1;
	copyInfo.srcSubresource.mipLevel = 0;
	copyInfo.srcSubresource.baseArrayLayer = 0;
	copyInfo.dstSubresource = copyInfo.srcSubresource;
	copyInfo.extent = srcImage._extent;
	copyInfo.srcOffset = { { 0, 0, 0 } };
	copyInfo.dstOffset = { { 0, 0, 0 } };

	GetCurrentCommandBuffer().copyImage(srcImage.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, dstImage.GetHandle(), vk::ImageLayout::eTransferDstOptimal, copyInfo);
}


void Render::Backend::CopyDataToBuffer(const void* data, size_t dataSize, Render::Buffer& targetBuffer, uint32_t offset /* = 0 */)
{
	void* mappedData;
	vmaMapMemory(_allocator, targetBuffer._allocation, &mappedData);

	auto* mappedDataBytes = reinterpret_cast<uint8_t*>(mappedData);
	mappedDataBytes += offset;

	std::memcpy(mappedDataBytes, data, dataSize);

	vmaUnmapMemory(_allocator, targetBuffer._allocation);
}


void Render::Backend::CopyBufferToImage(vk::CommandBuffer cmd, const Render::Buffer& srcBuffer, Render::Image& dstImage)
{
	vk::BufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;

	copyRegion.imageSubresource.aspectMask = dstImage._aspectMask;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = dstImage._layerCount;
	copyRegion.imageExtent = dstImage._extent;

	cmd.copyBufferToImage(srcBuffer.GetHandle(), dstImage.GetHandle(), vk::ImageLayout::eTransferDstOptimal, copyRegion);
}


void Render::Backend::CopyBufferToImage(const Render::Buffer& srcBuffer, Render::Image& dstImage)
{
	CopyBufferToImage(GetCurrentCommandBuffer(), srcBuffer, dstImage);
}


void Render::Backend::CopyBufferRegionsToImage(vk::CommandBuffer cmd, const Render::Buffer& srcBuffer, Render::Image& dstImage, const std::vector<vk::BufferImageCopy>& bufferCopyRegions)
{
	cmd.copyBufferToImage(srcBuffer.GetHandle(), dstImage.GetHandle(), vk::ImageLayout::eTransferDstOptimal, bufferCopyRegions);
}


void Render::Backend::UpdateBufferGPUData(const float* sourceData, size_t bufferSize, Render::Buffer& targetBuffer, vk::CommandBuffer cmd, Render::Buffer* stagingBuffer /* = nullptr */)
{
	if (targetBuffer._memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
	{
		memcpy(targetBuffer._allocationInfo.pMappedData, sourceData, bufferSize);
	}
	else
	{
		ASSERT(stagingBuffer != nullptr, "Invalid staging buffer.");

		memcpy(stagingBuffer->_allocationInfo.pMappedData, sourceData, bufferSize);
		vmaFlushAllocation(_allocator, stagingBuffer->_allocation, 0, VK_WHOLE_SIZE);

		Render::Buffer::MemoryBarrierInfo barrierInfo = {};
		barrierInfo.srcAccess = vk::AccessFlagBits2::eHostWrite;
		barrierInfo.dstAccess = vk::AccessFlagBits2::eTransferRead;
		barrierInfo.srcStage = vk::PipelineStageFlagBits2::eHost;
		barrierInfo.dstStage = vk::PipelineStageFlagBits2::eCopy;

		stagingBuffer->MemoryBarrier(cmd, barrierInfo);
		vk::BufferCopy bufCopy;
		bufCopy.srcOffset = 0;
		bufCopy.dstOffset = 0;
		bufCopy.size = bufferSize;
		cmd.copyBuffer(stagingBuffer->_handle, targetBuffer._handle, bufCopy);
	}
}


void Render::Backend::UploadBufferImmediately(Render::Buffer& targetGpuBuffer, const void* data, size_t size, vk::CommandBuffer* extCmd /* = nullptr */)
{
	Render::Buffer::CreateInfo stagingInfo = {};
	stagingInfo.allocSize = size;
	stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
	stagingInfo.memUsage = VMA_MEMORY_USAGE_CPU_ONLY;
	stagingInfo.isLifetimeManaged = false;

	Render::Buffer stagingBuffer = CreateBuffer(stagingInfo);

	CopyDataToBuffer(data, size, stagingBuffer);

	if (!extCmd)
	{
		SubmitCmdImmediately([=](vk::CommandBuffer cmd) {
			vk::BufferCopy copy;
			copy.dstOffset = 0;
			copy.srcOffset = 0;
			copy.size = size;
			cmd.copyBuffer(stagingBuffer.GetHandle(), targetGpuBuffer.GetHandle(), copy);
		}, _uploadContext._commandBuffer);
	}
	else
	{
		vk::BufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = size;

		extCmd->copyBuffer(stagingBuffer.GetHandle(), targetGpuBuffer.GetHandle(), copy);

		Render::Buffer::MemoryBarrierInfo uploadBarrierInfo = {};
		uploadBarrierInfo.srcAccess = vk::AccessFlagBits2::eTransferWrite;
		uploadBarrierInfo.dstAccess = vk::AccessFlagBits2::eShaderRead;
		uploadBarrierInfo.srcStage = vk::PipelineStageFlagBits2::eTransfer;
		uploadBarrierInfo.dstStage = vk::PipelineStageFlagBits2::eFragmentShader;

		targetGpuBuffer.MemoryBarrier(*extCmd, uploadBarrierInfo);
	}

	stagingBuffer.DestroyManually();
}


size_t Render::Backend::PadUniformBufferSize(size_t originalSize) const
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


void Render::Backend::RegisterImage(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages, const std::vector<Render::DescriptorManager::ImageInfo>& imageInfos,
	uint32_t binding, uint32_t numDescs /* = 1 */, bool isBindless /* = false */, bool isPerFrame /* = false */)
{
	_descMng.register_image(descriptorSetType, shaderStages, imageInfos, binding, numDescs, isBindless, isPerFrame);
}


void Render::Backend::RegisterBuffer(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages, const std::vector<Render::DescriptorManager::BufferInfo>& bufferInfos,
	uint32_t binding, uint32_t numDescs /* = 1 */, bool isPerFrame /* = false */)
{
	_descMng.register_buffer(descriptorSetType, shaderStages, bufferInfos, binding, numDescs, isPerFrame);
}


void Render::Backend::RegisterAccelerationStructure(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
	vk::AccelerationStructureKHR accelStructure, uint32_t binding, bool isPerFrame /* = false */)
{
	_descMng.register_accel_structure(descriptorSetType, shaderStages, accelStructure, binding, isPerFrame);
}


Render::Backend::BLASInput Render::Backend::ConvertMeshToBlasInput(const Mesh& mesh, vk::GeometryFlagBitsKHR rtGeometryFlags)
{
	vk::DeviceAddress vertexAddress = mesh._vertexBuffer.GetDeviceAddress();
	vk::DeviceAddress indexAddress = mesh._indexBuffer.GetDeviceAddress();

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
	blasInput._geometry.flags = rtGeometryFlags;
	blasInput._geometry.setGeometry(blasInput._triangles);

	blasInput._buildRangeInfo.firstVertex = 0;
	blasInput._buildRangeInfo.primitiveCount = maxPrimCount;
	blasInput._buildRangeInfo.primitiveOffset = 0;
	blasInput._buildRangeInfo.transformOffset = 0;

	return blasInput;
}


vk::RenderingAttachmentInfo Render::Backend::CreateAttachment(vk::Format format, vk::ImageUsageFlagBits usage, Render::Image* image)
{
	vk::RenderingAttachmentInfo resAttachmentInfo;
	Render::Image::CreateInfo attachmentImageInfo = {};

	Render::Image attachmentImage = {};

	vk::ImageLayout imageLayout = {};
	vk::ClearValue clearValue;

	if (usage & vk::ImageUsageFlagBits::eColorAttachment)
	{
		attachmentImageInfo.aspectMask = vk::ImageAspectFlagBits::eColor;
		imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	}

	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
	{
		attachmentImageInfo.aspectMask = vk::ImageAspectFlagBits::eDepth;
		if (format >= vk::Format::eD16UnormS8Uint)
		{
			attachmentImageInfo.aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}
		imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		clearValue.depthStencil = vk::ClearDepthStencilValue({ 1.0f, 0 });
	}

	vk::Extent3D extent;
	extent.width = _windowExtent.width;
	extent.height = _windowExtent.height;
	extent.depth = 1;

	attachmentImageInfo.format = format;
	attachmentImageInfo.usageFlags = usage | vk::ImageUsageFlagBits::eSampled;
	attachmentImageInfo.extent = extent;
	attachmentImageInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	*image = CreateImage(attachmentImageInfo);

	resAttachmentInfo.imageView = image->GetView();
	resAttachmentInfo.imageLayout = imageLayout;
	resAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
	resAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
	resAttachmentInfo.clearValue = clearValue;

	return resAttachmentInfo;
}


void Render::Backend::BeginFrameRendering()
{
	const FrameData& currentFrameData = GetCurrentFrameData();

	// wait until the GPU has finished rendering the last frame, with timeout of 1 second
	ASSERT_VK(_device.waitForFences(currentFrameData._renderFence, true, 1000000000), "Render fence timeout.");
	_device.resetFences(currentFrameData._renderFence);

	_swapchainImageIndex = _device.acquireNextImageKHR(_swapchain, 1000000000, currentFrameData._presentSemaphore, {}).value;

	// we know that everything finished rendering, so we safely reset the command buffer and reuse it
	ASSERT_VK(vkResetCommandBuffer(currentFrameData._mainCommandBuffer, 0), "Command buffer reset failed.");

	vk::CommandBuffer cmd = currentFrameData._mainCommandBuffer;

	// begin recording the command buffer, letting Vulkan know that we will submit cmd exactly once per frame
	vk::CommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	cmd.begin(cmdBeginInfo);
}


void Render::Backend::EndFrameRendering()
{
	const FrameData& currentFrameData = GetCurrentFrameData();

	// stop recording to command buffer (we can no longer add commands, but it can now be submitted and executed)
	currentFrameData._mainCommandBuffer.end();

	// prepare the vk::Queue submission
	// wait for the present semaphore to present image
	// signal the render semaphore, showing that rendering is finished

	vk::SubmitInfo submit = {};
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &currentFrameData._presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &currentFrameData._renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &currentFrameData._mainCommandBuffer;

	// submit the buffer
	// render fence blocks until graphics commands are done
	_graphicsQueue.submit(submit, currentFrameData._renderFence);
}


void Render::Backend::Present()
{
	vk::PresentInfoKHR presentInfo = {};
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &GetCurrentFrameData()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	uint32_t uImageIndex = _swapchainImageIndex;

	presentInfo.pImageIndices = &uImageIndex;

	ASSERT_VK(_graphicsQueue.presentKHR(presentInfo), "Present failed");

	++_frameId;
}


void Render::Backend::DrawObjects(const std::vector<RenderObject>& objects, const Render::Pass& pass, PushConstantsInfo* pPushConstantsInfo /* = nullptr */, bool useCamLightingBuffer /* = false */)
{
	if (pass._swapchainTargetId > -1)
	{
		ASSERT(pass._swapchainImageIsSet, "Please set current swapchain image before trying to render to it");
	}

	vk::CommandBuffer cmd = GetCurrentCommandBuffer();

	cmd.beginRendering(pass._renderingInfo);

	if (pPushConstantsInfo)
	{
		const PushConstantsInfo& pushConstantsInfo = *pPushConstantsInfo;

		cmd.pushConstants(pass.GetPipelineLayout(), pushConstantsInfo.shaderStages, 0, pushConstantsInfo.size, pushConstantsInfo.pData);
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pass.GetPipeline());

	int32_t frameInFlightId = _frameId % FRAME_OVERLAP;

	for (int32_t i = 0; i < objects.size(); ++i)
	{
		const RenderObject& object = objects[i];

		if (!object.model)
		{
			continue;
		}

		auto pipelineDescriptorSets = _descMng.get_descriptor_sets(pass._usedDescSets, frameInFlightId);

		if (useCamLightingBuffer)
		{
			// scene & camera dynamic descriptor offset
			auto dynamicDescOffset = static_cast<uint32_t>(PadUniformBufferSize(sizeof(CameraDataGPU) +
				sizeof(LightingData)) * frameInFlightId);

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pass.GetPipelineLayout(), 0,
				pipelineDescriptorSets, dynamicDescOffset);
		}
		else
		{
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pass.GetPipelineLayout(), 0,
				pipelineDescriptorSets, {});
		}

		vk::DeviceSize offset = 0;
		cmd.bindVertexBuffers(0, object.mesh->_vertexBuffer.GetHandle(), offset);
		cmd.bindIndexBuffer(object.mesh->_indexBuffer.GetHandle(), offset, vk::IndexType::eUint32);

		cmd.drawIndexed(static_cast<uint32_t>(object.mesh->_indices.size()), 1, 0, 0, i);
	}

	cmd.endRendering();
}


void Render::Backend::DrawScreenQuad(const Render::Pass& pass, PushConstantsInfo* pPushConstantsInfo /* = nullptr */, bool useCamLightingBuffer /* = false */)
{
	if (pass._swapchainTargetId > -1)
	{
		ASSERT(pass._swapchainImageIsSet, "Please set current swapchain image before trying to render to it");
	}

	vk::CommandBuffer cmd = GetCurrentCommandBuffer();

	cmd.beginRendering(pass._renderingInfo);

	if (pPushConstantsInfo)
	{
		const PushConstantsInfo& pushConstantsInfo = *pPushConstantsInfo;

		cmd.pushConstants(pass.GetPipelineLayout(), pushConstantsInfo.shaderStages, 0, pushConstantsInfo.size, pushConstantsInfo.pData);
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pass.GetPipeline());

	int32_t frameInFlightId = _frameId % FRAME_OVERLAP;

	auto pipelineDescriptorSets = _descMng.get_descriptor_sets(pass._usedDescSets, frameInFlightId);

	if (useCamLightingBuffer)
	{
		// scene & camera dynamic descriptor offset
		auto dynamicDescOffset = static_cast<uint32_t>(PadUniformBufferSize(sizeof(CameraDataGPU) +
			sizeof(LightingData)) * frameInFlightId);

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pass.GetPipelineLayout(), 0,
			pipelineDescriptorSets, dynamicDescOffset);
	}
	else
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pass.GetPipelineLayout(), 0,
			pipelineDescriptorSets, {});
	}

	cmd.draw(3, 1, 0, 0);

	cmd.endRendering();
}


void Render::Backend::TraceRays(const Render::Pass& pass, PushConstantsInfo* pPushConstantsInfo /* = nullptr */, bool useCamLightingBuffer /* = false */)
{
	vk::CommandBuffer cmd = GetCurrentCommandBuffer();

	if (pPushConstantsInfo)
	{
		const PushConstantsInfo& pushConstantsInfo = *pPushConstantsInfo;

		cmd.pushConstants(pass.GetPipelineLayout(), pushConstantsInfo.shaderStages, 0, pushConstantsInfo.size, pushConstantsInfo.pData);
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, pass.GetPipeline());

	int32_t frameInFlightId = _frameId % FRAME_OVERLAP;

	auto pipelineDescriptorSets = _descMng.get_descriptor_sets(pass._usedDescSets, frameInFlightId);

	if (useCamLightingBuffer)
	{
		// scene & camera dynamic descriptor offset
		auto dynamicDescOffset = static_cast<uint32_t>(PadUniformBufferSize(sizeof(CameraDataGPU) +
			sizeof(LightingData)) * frameInFlightId);

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pass.GetPipelineLayout(), 0,
			pipelineDescriptorSets, dynamicDescOffset);
	}
	else
	{
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pass.GetPipelineLayout(), 0,
			pipelineDescriptorSets, {});
	}

	cmd.traceRaysKHR(pass._rtSbt._rgenRegion, pass._rtSbt._rmissRegion, pass._rtSbt._rchitRegion, pass._rtSbt._rcallRegion, _windowExtent.width, _windowExtent.height, 1);
}


vk::CommandPool Render::Backend::CreateCommandPool(uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags flags /* = {} */)
{
	vk::CommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
	commandPoolInfo.flags = flags;

	vk::CommandPool resCommandPool;
	resCommandPool = _device.createCommandPool(commandPoolInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroyCommandPool(resCommandPool);
	});

	return resCommandPool;
}


vk::CommandBuffer Render::Backend::CreateCommandBuffer(vk::CommandPool pool, uint32_t count /* = 1 */,
	vk::CommandBufferLevel level /* = vk::CommandBufferLevel::ePrimary */)
{
	vk::CommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.commandPool = pool;
	cmdAllocInfo.commandBufferCount = count;
	cmdAllocInfo.level = level;

	return _device.allocateCommandBuffers(cmdAllocInfo)[0];
}


void Render::Backend::SubmitCmdImmediately(std::function<void(vk::CommandBuffer cmd)>&& function, vk::CommandBuffer cmd)
{
	vk::CommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	cmd.begin(cmdBeginInfo);

	function(cmd);

	cmd.end();

	vk::SubmitInfo submitInfo = vkinit::submit_info(&cmd);

	_graphicsQueue.submit(submitInfo, _uploadContext._uploadFence);

	ASSERT_VK(_device.waitForFences(_uploadContext._uploadFence, true, 9999999999), "Timeout on uploadFence");
	_device.resetFences(_uploadContext._uploadFence);

	_device.resetCommandPool(_uploadContext._commandPool);
}


void Render::Backend::InitSwapchain()
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
	std::vector<VkImageView> swchImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = static_cast<vk::Format>(vkbSwapchain.image_format);

	_swapchainImages.resize(swchImages.size());

	for (int32_t i = 0; i < swchImages.size(); ++i)
	{
		_swapchainImages[i]._handle = swchImages[i];
		_swapchainImages[i]._view = swchImageViews[i];
		_swapchainImages[i]._format = _swapchainImageFormat;
		_swapchainImages[i]._aspectMask = vk::ImageAspectFlagBits::eColor;
	}

	_mainDeletionQueue.push_function([=]() {
		for (auto& image : _swapchainImages)
		{
			_device.destroyImageView(image.GetView());
		}
	});

	Image::CreateInfo intermediateImageInfo;
	intermediateImageInfo.aspectMask = vk::ImageAspectFlagBits::eColor;
	intermediateImageInfo.extent = _windowExtent3D;
	intermediateImageInfo.format = _frameBufferFormat;
	intermediateImageInfo.usageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
		vk::ImageUsageFlagBits::eTransferSrc;

	_intermediateImage = CreateImage(intermediateImageInfo);

	_mainDeletionQueue.push_function([=]() {
		_device.destroySwapchainKHR(_swapchain);
	});
}


void Render::Backend::InitCommands()
{
	_uploadContext._commandPool = CreateCommandPool(_graphicsQueueFamily);
	_uploadContext._commandBuffer = CreateCommandBuffer(_uploadContext._commandPool);

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		_frames[i]._commandPool = CreateCommandPool(_graphicsQueueFamily, vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
		_frames[i]._mainCommandBuffer = CreateCommandBuffer(_frames[i]._commandPool);
	}
}


void Render::Backend::InitSyncStructures()
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


void Render::Backend::InitRaytracingProperties()
{
	_gpuProperties.pNext = &_rtProperties;
	_chosenGPU.getProperties2(&_gpuProperties);
}


void Render::Backend::InitSamplers()
{
	vk::SamplerCreateInfo linearClampSamplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear,
		1.0, vk::SamplerAddressMode::eClampToEdge);
	
	auto linearClampSamplerId = static_cast<size_t>(Render::SamplerType::eLinearClamp);
	_samplers[linearClampSamplerId] = _device.createSampler(linearClampSamplerInfo);


	vk::SamplerCreateInfo linearRepeatSamplerInfo = vkinit::sampler_create_info(vk::Filter::eLinear, vk::Filter::eLinear,
		VK_LOD_CLAMP_NONE, vk::SamplerAddressMode::eRepeat);

	auto linearRepeatSamplerId = static_cast<size_t>(Render::SamplerType::eLinearRepeat);
	_samplers[linearRepeatSamplerId] = _device.createSampler(linearRepeatSamplerInfo);

	_mainDeletionQueue.push_function([=]() {
		for (auto& sampler : _samplers) {
			_device.destroySampler(sampler);
		}
	});
}


void Render::Backend::InitImGui()
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

	const vk::Device& device = *GetPDevice();

	VkDescriptorPool imguiPool = device.createDescriptorPool(poolInfo);

	ImGui::CreateContext();

	ImGui_ImplSDL3_InitForVulkan(_pWindow);

	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = _libInstance;
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


void Render::Pass::BuildShaderBindingTable()
{
	// TODO: make SBT more flexible -- currently it only supports one (very specific) set of shader regions.
	//       Tie it to RT pipeline initialization.

	auto* backend = Render::Backend::AcquireInstance();

	uint32_t rmissCount = 2;
	uint32_t rchitCount = 1;
	uint32_t handleCount = 1 + rmissCount + rchitCount;
	uint32_t handleSize = backend->_rtProperties.shaderGroupHandleSize;

	vk::DeviceSize alignedHandleSize = AlignUp(handleSize, backend->_rtProperties.shaderGroupHandleAlignment);

	_rtSbt._rgenRegion.stride = AlignUp(alignedHandleSize, backend->_rtProperties.shaderGroupBaseAlignment);
	_rtSbt._rgenRegion.size = _rtSbt._rgenRegion.stride; // for raygen size == stride
	_rtSbt._rmissRegion.stride = alignedHandleSize;
	_rtSbt._rmissRegion.size = AlignUp(rmissCount * alignedHandleSize, backend->_rtProperties.shaderGroupBaseAlignment);
	_rtSbt._rchitRegion.stride = alignedHandleSize;
	_rtSbt._rchitRegion.size = AlignUp(rchitCount * alignedHandleSize, backend->_rtProperties.shaderGroupBaseAlignment);

	const auto& device = *backend->GetPDevice();

	// get shader group handles
	uint32_t dataSize = handleCount * handleSize;
	std::vector<uint8_t> handles(dataSize);
	ASSERT_VK(device.getRayTracingShaderGroupHandlesKHR(_pso._pipeline, 0, handleCount, dataSize, handles.data()), "Failed to get shader group handles");

	vk::DeviceSize sbtSize = _rtSbt._rgenRegion.size + _rtSbt._rmissRegion.size + _rtSbt._rchitRegion.size + _rtSbt._rcallRegion.size;

	Render::Buffer::CreateInfo sbtBufferInfo = {};
	sbtBufferInfo.allocSize = sbtSize;
	sbtBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eShaderBindingTableKHR;
	sbtBufferInfo.memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	_rtSbt._buffer = backend->CreateBuffer(sbtBufferInfo);

	// find shader group device addresses
	vk::BufferDeviceAddressInfo addressInfo;
	addressInfo.buffer = _rtSbt._buffer.GetHandle();
	vk::DeviceAddress sbtAddress = device.getBufferAddress(addressInfo);
	_rtSbt._rgenRegion.deviceAddress = sbtAddress;
	_rtSbt._rmissRegion.deviceAddress = sbtAddress + _rtSbt._rgenRegion.size;
	_rtSbt._rchitRegion.deviceAddress = sbtAddress + _rtSbt._rgenRegion.size + _rtSbt._rmissRegion.size;

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
	pData = sbtBufferRaw.data() + _rtSbt._rgenRegion.size;
	for (uint32_t c = 0; c < rmissCount; ++c)
	{
		std::memcpy(pData, getHandle(handleIndex++), handleSize);
		pData += _rtSbt._rmissRegion.stride;
	}

	// copy hit region
	pData = sbtBufferRaw.data() + _rtSbt._rgenRegion.size + _rtSbt._rmissRegion.size;
	for (uint32_t c = 0; c < rchitCount; ++c)
	{
		std::memcpy(pData, getHandle(handleIndex++), handleSize);
		pData += _rtSbt._rchitRegion.stride;
	}

	backend->UploadBufferImmediately(_rtSbt._buffer, sbtBufferRaw);
}


void Render::Pass::BuildPipeline()
{
	auto* backend = Render::Backend::AcquireInstance();

	auto setLayouts = backend->GetPDescriptorManager()->get_layouts(_usedDescSets);

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
	pipelineLayoutInfo.setSetLayouts(setLayouts);

	if (_pushConstantRange.size > 0)
	{
		pipelineLayoutInfo.setPushConstantRanges(_pushConstantRange);
	}

	vk::Device& device = *backend->GetPDevice();
	vk::PipelineLayout pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);
	_pso._pipelineLayout = pipelineLayout;

	backend->_mainDeletionQueue.push_function([=](){
		device.destroyPipelineLayout(pipelineLayout);
	});

	vk::PipelineViewportStateCreateInfo viewportState = {};
	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	vk::PipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = vk::LogicOp::eCopy;
	colorBlending.setAttachments(_colorBlendAttachments);

	vk::GraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.setStages(_shaderStages);
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.layout = _pso._pipelineLayout;
	pipelineInfo.pNext = &_pipelineRenderingCreateInfo;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	auto pipelineResVal = backend->GetPDevice()->createGraphicsPipelines({}, pipelineInfo);
	ASSERT(pipelineResVal.result == vk::Result::eSuccess, "Failed to create pipeline");

	_pso._pipeline = pipelineResVal.value[0];

	// shader modules are now built into the pipelines, we don't need them anymore
	for (auto& shader : _shaders)
	{
		shader.destroy();
	}

	backend->_mainDeletionQueue.push_function([=]() {
		device.destroyPipeline(pipelineResVal.value[0]);
	});
}


void Render::Pass::BuildRTPipeline(const std::array<vk::PipelineShaderStageCreateInfo, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT>& shaderStages)
{
	auto* backend = Render::Backend::AcquireInstance();

	auto setLayouts = backend->GetPDescriptorManager()->get_layouts(_usedDescSets);

	vk::PipelineLayoutCreateInfo rtPipelineLayoutInfo = vkinit::pipeline_layout_create_info();
	rtPipelineLayoutInfo.setSetLayouts(setLayouts);

	if (_pushConstantRange.size > 0)
	{
		rtPipelineLayoutInfo.setPushConstantRanges(_pushConstantRange);
	}

	vk::Device& device = *backend->GetPDevice();
	vk::PipelineLayout pipelineLayout = device.createPipelineLayout(rtPipelineLayoutInfo);

	_pso._pipelineLayout = pipelineLayout;

	backend->_mainDeletionQueue.push_function([=]() {
		device.destroyPipelineLayout(pipelineLayout);
	});

	auto shaderGroups = MakeRTShaderGroups();

	vk::RayTracingPipelineCreateInfoKHR rtPipelineInfo;
	rtPipelineInfo.setStages(shaderStages);
	rtPipelineInfo.setGroups(shaderGroups);
	rtPipelineInfo.maxPipelineRayRecursionDepth = 1;
	rtPipelineInfo.layout = pipelineLayout;

	auto rtPipelineResVal = backend->GetPDevice()->createRayTracingPipelineKHR({}, {}, rtPipelineInfo);

	ASSERT(rtPipelineResVal.result == vk::Result::eSuccess, "Failed to build RT pipeline");
	_pso._pipeline = rtPipelineResVal.value;

	// shader modules are now built into the pipelines, we don't need them anymore
	for (auto& shader : _shaders)
	{
		shader.destroy();
	}

	backend->_mainDeletionQueue.push_function([=]() {
		device.destroyPipeline(rtPipelineResVal.value);
	});

	BuildShaderBindingTable();
}


void Render::VertexInputDescription::ConstructFromVertex()
{
	// 1 vertex buffer binding, per-vertex rate
	vk::VertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = vk::VertexInputRate::eVertex;

	this->bindings.push_back(mainBinding);

	// store position at location 0
	vk::VertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = vk::Format::eR32G32B32Sfloat;
	positionAttribute.offset = offsetof(Vertex, position);

	// normals at location 1
	vk::VertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = vk::Format::eR32G32B32Sfloat;
	normalAttribute.offset = offsetof(Vertex, normal);

	// colors at location 2
	vk::VertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = vk::Format::eR32G32B32Sfloat;
	colorAttribute.offset = offsetof(Vertex, color);

	// UV at location 3
	vk::VertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = vk::Format::eR32G32Sfloat;
	uvAttribute.offset = offsetof(Vertex, uv);

	// tangents at location 4
	vk::VertexInputAttributeDescription tangentAttribute = {};
	tangentAttribute.binding = 0;
	tangentAttribute.location = 4;
	tangentAttribute.format = vk::Format::eR32G32B32Sfloat;
	tangentAttribute.offset = offsetof(Vertex, tangent);

	this->attributes.push_back(positionAttribute);
	this->attributes.push_back(normalAttribute);
	this->attributes.push_back(colorAttribute);
	this->attributes.push_back(uvAttribute);
	this->attributes.push_back(tangentAttribute);
}


void Render::Pass::Init(const InitInfo& initInfo)
{
	auto* backend = Render::Backend::AcquireInstance();

	_usedDescSets = initInfo.usedDescSets;

	_vertexInputInfo.vertexBindingDescriptionCount = 0;
	_vertexInputInfo.vertexAttributeDescriptionCount = 0;

	if (initInfo.useVertexAttributes)
	{
		_vertexInputDesc.ConstructFromVertex();

		_vertexInputInfo.setVertexAttributeDescriptions(_vertexInputDesc.attributes);
		_vertexInputInfo.setVertexBindingDescriptions(_vertexInputDesc.bindings);
	}

	_inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
	_inputAssembly.primitiveRestartEnable = VK_FALSE;

	_viewport.width = backend->_windowExtent.width;
	_viewport.height = backend->_windowExtent.height;
	_viewport.minDepth = 0.0f;
	_viewport.maxDepth = 1.0f;

	_scissor.offset = vk::Offset2D{ 0, 0 };
	_scissor.extent = backend->_windowExtent;

	_rasterizer.depthClampEnable = VK_FALSE;
	_rasterizer.rasterizerDiscardEnable = VK_FALSE;

	_rasterizer.polygonMode = vk::PolygonMode::eFill;
	_rasterizer.lineWidth = 1.0f;

	_rasterizer.cullMode = vk::CullModeFlagBits::eNone;
	_rasterizer.frontFace = vk::FrontFace::eClockwise;
	_rasterizer.depthBiasEnable = VK_FALSE;
	_rasterizer.depthBiasClamp = 0.0f;
	_rasterizer.depthBiasConstantFactor = 0.0f;
	_rasterizer.depthBiasSlopeFactor = 0.0f;

	ASSERT(initInfo.pColorAttachmentInfos, "Invalid attachment infos");
	size_t numColorAttachments = initInfo.pColorAttachmentInfos->size();
	_colorBlendAttachments.resize(numColorAttachments);

	for (auto& attachment : _colorBlendAttachments)
	{
		attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		attachment.blendEnable = VK_FALSE;
		attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		attachment.colorBlendOp = vk::BlendOp::eAdd;
		attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
		attachment.alphaBlendOp = vk::BlendOp::eAdd;
	}

	_multisampling.sampleShadingEnable = VK_FALSE;
	_multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
	_multisampling.minSampleShading = 1.0f;
	_multisampling.pSampleMask = nullptr;
	_multisampling.alphaToCoverageEnable = VK_FALSE;
	_multisampling.alphaToOneEnable = VK_FALSE;


	_depthStencil.depthTestEnable = VK_TRUE;
	_depthStencil.depthWriteEnable = VK_TRUE;
	_depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
	_depthStencil.depthBoundsTestEnable = VK_FALSE;
	_depthStencil.stencilTestEnable = VK_FALSE;

	MakeInputAssembly(initInfo.topology);
	MakeRasterizationState(initInfo.polygonMode, initInfo.cullMode);

	if (initInfo.viewportHeight > 0 && initInfo.viewportWidth > 0)
	{
		MakeViewportAndScissor(initInfo.viewportWidth, initInfo.viewportHeight);
	}

	// Color attachment handling

	std::vector<vk::Format> colorAttachmentFormats(numColorAttachments);

	_colorRenderingAttachmentInfos.resize(numColorAttachments);

	for (int32_t i = 0; i < numColorAttachments; ++i)
	{
		const auto& attachmentInfos = *initInfo.pColorAttachmentInfos;
		MakeColorBlendAttachmentState(i, attachmentInfos[i].blendEnable, attachmentInfos[i].colorWriteFlags);

		if (attachmentInfos[i].isSwapchainImage)
		{
			colorAttachmentFormats[i] = backend->_swapchainImageFormat;
			_colorRenderingAttachmentInfos[i].imageView = VK_NULL_HANDLE;
			_swapchainTargetId = i;
			_swapchainImageIsSet = false;
		}
		else
		{
			colorAttachmentFormats[i] = attachmentInfos[i].pImage->GetFormat();
			_colorRenderingAttachmentInfos[i].imageView = attachmentInfos[i].pImage->GetView();
		}

		_colorRenderingAttachmentInfos[i].imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		_colorRenderingAttachmentInfos[i].loadOp = attachmentInfos[i].loadOp;
		_colorRenderingAttachmentInfos[i].storeOp = attachmentInfos[i].storeOp;

		vk::ClearValue clearValue;
		clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		_colorRenderingAttachmentInfos[i].clearValue = clearValue;
	}

	_pipelineRenderingCreateInfo.setColorAttachmentFormats(colorAttachmentFormats);

	// Depth attachment handling

	ASSERT(initInfo.pDepthAttachment->pImage, "Invalid depth image");
	const Render::Image& depthImage = *initInfo.pDepthAttachment->pImage;
	_depthAttachmentInfo.imageView = depthImage.GetView();
	_depthAttachmentInfo.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
	_depthAttachmentInfo.loadOp = initInfo.pDepthAttachment->loadOp;
	_depthAttachmentInfo.storeOp = initInfo.pDepthAttachment->storeOp;

	vk::ClearValue depthClearValue;
	depthClearValue.depthStencil = vk::ClearDepthStencilValue({ 1.0f, 0 });

	_depthAttachmentInfo.clearValue = depthClearValue;

	_pipelineRenderingCreateInfo.setDepthAttachmentFormat(depthImage.GetFormat());

	MakeMultisamplingState(initInfo.numSamples);
	MakeDepthStencilState(initInfo.bDepthTest, initInfo.bDepthWrite, initInfo.compareOp);

	ASSERT(initInfo.pShaderNames, "Invalid shader stage names");
	size_t numShaderStages = initInfo.pShaderNames->size();

	_shaders.resize(numShaderStages);
	_shaderStages.resize(numShaderStages);

	const auto& shaderFileNames = *initInfo.pShaderNames;

	for (int32_t i = 0; i < numShaderStages; ++i)
	{
		_shaders[i].create(backend->GetPDevice(), shaderFileNames[i]);
		_shaderStages[i] = _shaders[i].get_stage_create_info();
	}

	_pushConstantRange.offset = 0;
	_pushConstantRange.size = initInfo.pcInitInfo.pcBufferSize;
	_pushConstantRange.stageFlags = initInfo.pcInitInfo.stageFlags;

	BuildPipeline();

	if (_swapchainTargetId < 0)
	{
		_renderingInfo.setColorAttachments(_colorRenderingAttachmentInfos);
	}
	_renderingInfo.setPDepthAttachment(&_depthAttachmentInfo);
	_renderingInfo.layerCount = 1;
	_renderingInfo.renderArea.extent.width = _viewport.width;
	_renderingInfo.renderArea.extent.height = _viewport.height;
}


void Render::Pass::SetSwapchainImage(Image& curSwapchainImage)
{
	_colorRenderingAttachmentInfos[_swapchainTargetId].imageView = curSwapchainImage.GetView();
	_renderingInfo.setColorAttachments(_colorRenderingAttachmentInfos);

	_swapchainImageIsSet = true;
}


void Render::Pass::InitRT(const RTInitInfo& initInfo)
{
	std::array<vk::PipelineShaderStageCreateInfo, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT> shaderStages = MakeRTShaderStages(initInfo.pShaderNames);

	_usedDescSets = initInfo.usedDescSets;

	_pushConstantRange.offset = 0;
	_pushConstantRange.size = initInfo.pcInitInfo.pcBufferSize;
	_pushConstantRange.stageFlags = initInfo.pcInitInfo.stageFlags;

	BuildRTPipeline(shaderStages);
}


std::array<vk::RayTracingShaderGroupCreateInfoKHR, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT> Render::Pass::MakeRTShaderGroups()
{
	std::array<vk::RayTracingShaderGroupCreateInfoKHR, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT> rtShaderGroups;

	// Define shader groups
	vk::RayTracingShaderGroupCreateInfoKHR shaderGroupInfo;
	shaderGroupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

	// Raygen
	auto raygenShaderIndex = static_cast<uint32_t>(Render::Shader::RTStageIndices::eRaygen);
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
	shaderGroupInfo.generalShader = raygenShaderIndex;
	rtShaderGroups[raygenShaderIndex] = shaderGroupInfo;

	// Miss
	auto missShaderIndex = static_cast<uint32_t>(Render::Shader::RTStageIndices::eMiss);
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
	shaderGroupInfo.generalShader = missShaderIndex;
	rtShaderGroups[missShaderIndex] = shaderGroupInfo;

	// Shadow miss
	auto shadowMissShaderIndex = static_cast<uint32_t>(Render::Shader::RTStageIndices::eShadowMiss);
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
	shaderGroupInfo.generalShader = shadowMissShaderIndex;
	rtShaderGroups[shadowMissShaderIndex] = shaderGroupInfo;

	// Closest hit and any hit
	auto triangleClosestHitShaderIndex = static_cast<uint32_t>(Render::Shader::RTStageIndices::eClosestHit);
	auto triangleAnyHitShaderIndex = static_cast<uint32_t>(Render::Shader::RTStageIndices::eAnyHit);
	shaderGroupInfo.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
	shaderGroupInfo.generalShader = VK_SHADER_UNUSED_KHR;
	shaderGroupInfo.closestHitShader = triangleClosestHitShaderIndex;
	shaderGroupInfo.anyHitShader = triangleAnyHitShaderIndex;
	rtShaderGroups[triangleClosestHitShaderIndex] = shaderGroupInfo;

	return rtShaderGroups;
}


std::array<vk::PipelineShaderStageCreateInfo, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT> Render::Pass::MakeRTShaderStages(const std::vector<std::string>* pShaderNames/* = nullptr*/)
{
	auto* backend = Render::Backend::AcquireInstance();

	ASSERT(pShaderNames, "Invalid shader names");

	const std::vector<std::string>& shaderNames = *pShaderNames;

	std::array<vk::PipelineShaderStageCreateInfo, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT> stages;

	size_t numShaderStages = shaderNames.size();
	_shaders.reserve(numShaderStages);

	for (auto& shaderName : shaderNames)
	{
		Render::Shader shader;
		shader.create(backend->GetPDevice(), shaderName);

		_shaders.push_back(shader);

		auto stageIndex = static_cast<int32_t>(Shader::get_rt_shader_index_from_file_name(shaderName));
		stages[stageIndex] = shader.get_stage_create_info();
	}

	return stages;
}


void Render::Pass::MakeInputAssembly(vk::PrimitiveTopology topology)
{
	_inputAssembly.topology = topology;
}


void Render::Pass::MakeViewportAndScissor(int32_t width, int32_t height)
{
	_viewport.width = width;
	_viewport.height = height;

	_scissor.extent = vk::Extent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}


void Render::Pass::MakeRasterizationState(vk::PolygonMode polygonMode, vk::CullModeFlags cullMode/* = vk::CullModeFlagBits::eNone*/)
{
	_rasterizer.polygonMode = polygonMode;
	_rasterizer.cullMode = cullMode;
}


void Render::Pass::MakeColorBlendAttachmentState(int32_t attachmentId, bool blendEnable, vk::ColorComponentFlags colorWriteFlags)
{
	_colorBlendAttachments[attachmentId].blendEnable = blendEnable;
	_colorBlendAttachments[attachmentId].colorWriteMask = colorWriteFlags;
}


void Render::Pass::MakeMultisamplingState(vk::SampleCountFlagBits numSamples)
{
	_multisampling.rasterizationSamples = numSamples;
}


void Render::Pass::MakeDepthStencilState(bool bDepthTest, bool bDepthWrite, vk::CompareOp compareOp)
{
	_depthStencil.depthTestEnable = bDepthTest;
	_depthStencil.depthWriteEnable = bDepthWrite;
	_depthStencil.depthCompareOp = compareOp;
}
