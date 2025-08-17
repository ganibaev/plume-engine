#pragma once

#include "render_types.h"
#include "render_initializers.h"
#include "render_descriptors.h"
#include "render_shader.h"
#include "render_cfg.h"
#include "../engine/plm_scene.h"
#include <thread>
#include <memory>

#include <SDL.h>
#include <SDL_vulkan.h>

class Vertex;

namespace Render
{

class Image
{
	friend class Backend;

public:
	enum class Type
	{
		eTexture = 0,
		eCubemap = 1,
		eRTXOutput = 2
	};

	struct CreateInfo
	{
		vk::Format format;
		vk::ImageUsageFlags usageFlags;
		vk::Extent3D extent;
		uint32_t mipLevels = 1;
		vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1;
		Type type = Type::eTexture;
		vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
		VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_GPU_ONLY;
	};

	vk::Image GetHandle() const { return _handle; }
	vk::ImageView GetView() const { return _view; }
	vk::Format GetFormat() const { return _format; }

	struct TransitionInfo
	{
		vk::AccessFlags srcAccessMask = vk::AccessFlagBits::eNone;
		vk::AccessFlags dstAccessMask = vk::AccessFlagBits::eNone;
		vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined;
		vk::ImageLayout newLayout = vk::ImageLayout::eUndefined;
		vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eAllGraphics;
		vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eAllGraphics;
	};

	void LayoutTransition(vk::CommandBuffer cmd, const TransitionInfo& info) const;
	void LayoutTransition(const TransitionInfo& info) const;

	void GenerateMipmaps(vk::CommandBuffer cmd) const;

private:
	uint32_t _levelCount = 1;
	uint32_t _layerCount = 1;

	vk::Image _handle;
	vk::Format _format;
	vk::ImageView _view;
	vk::ImageAspectFlags _aspectMask;

	vk::Extent3D _extent;
};


enum class SamplerType
{
	eLinearClamp = 0,
	eLinearRepeatAnisotropic,
};


class Buffer
{
	friend class Backend;

public:
	struct CreateInfo
	{
		size_t allocSize;
		vk::BufferUsageFlags usage;
		VmaMemoryUsage memUsage;
		VmaAllocationCreateFlags flags = 0;
		vk::MemoryPropertyFlags reqFlags = {};
		bool isLifetimeManaged = true;
	};

	vk::Buffer GetHandle() const { return _handle; }
	VmaAllocation GetAllocation() const { return _allocation; }
	vk::DeviceAddress GetDeviceAddress() const;

	struct MemoryBarrierInfo
	{
		vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eNone;
		vk::AccessFlags2 dstAccess = vk::AccessFlagBits2::eNone;
		vk::PipelineStageFlags2 srcStage = vk::PipelineStageFlagBits2::eAllCommands;
		vk::PipelineStageFlags2 dstStage = vk::PipelineStageFlagBits2::eAllCommands;
	};

	void MemoryBarrier(vk::CommandBuffer cmd, const MemoryBarrierInfo& barrierInfo) const;

	void DestroyManually();

private:
	vk::Buffer _handle;

	VmaAllocation _allocation = {};
	VmaAllocationInfo _allocationInfo = {};
	VkMemoryPropertyFlags _memPropFlags;
};


struct Mesh
{
	const Plume::Mesh* pEngineMesh = nullptr;

	Render::Buffer vertexBuffer;
	Render::Buffer indexBuffer;

	size_t numOfIndices = 0;
};


class Pass;
class System;
class Object;

class Backend
{
	friend System;
public:
	Backend() {}
	~Backend() = default;

	Backend(Backend& other) = delete;
	void operator=(const Backend& other) = delete;

	static Backend* AcquireInstance();
	void Init();
	void Terminate();

	vk::Device* GetPDevice() { return &_device; }
	const Render::DescriptorManager* GetPDescriptorManager() const { return &_descMng; }

	vk::FormatProperties GetFormatProperties(vk::Format format) const { return _chosenGPU.getFormatProperties(format); }

	vk::Sampler GetSampler(const SamplerType& type) const;

	Image CreateImage(const Render::Image::CreateInfo& createInfo);
	Buffer CreateBuffer(const Render::Buffer::CreateInfo& createInfo);

	void CopyImage(const Render::Image& srcImage, const Render::Image& dstImage);

	void CopyDataToBuffer(const void* data, size_t dataSize, Render::Buffer& targetBuffer, uint32_t offset = 0);

	void CopyBufferToImage(vk::CommandBuffer cmd, const Render::Buffer& srcBuffer, Render::Image& dstImage);
	void CopyBufferToImage(const Render::Buffer& srcBuffer, Render::Image& dstImage);

	void CopyBufferRegionsToImage(vk::CommandBuffer cmd, const Render::Buffer& srcBuffer, Render::Image& dstImage, const std::vector<vk::BufferImageCopy>& bufferCopyRegions);

	void UpdateBufferGPUData(const float* sourceData, size_t bufferSize, Render::Buffer& targetBuffer, 
		vk::CommandBuffer cmd, Render::Buffer* stagingBuffer = nullptr);

	void UploadBufferImmediately(Render::Buffer& targetGpuBuffer, const void* data, size_t size, vk::CommandBuffer* extCmd = nullptr);

	template <typename T>
	void UploadBufferImmediately(Render::Buffer& targetGpuBuffer, const std::vector<T>& bufferData, vk::CommandBuffer* extCmd = nullptr)
	{
		const size_t bufferSize = bufferData.size() * sizeof(T);
		const void* data = bufferData.data();

		UploadBufferImmediately(targetGpuBuffer, data, bufferSize, extCmd);
	}

	size_t PadUniformBufferSize(size_t originalSize) const;

	void RegisterImage(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
		const std::vector<Render::DescriptorManager::ImageInfo>& imageInfos, uint32_t binding, uint32_t numDescs = 1, bool isBindless = false,
		bool isPerFrame = false);
	void RegisterBuffer(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
		const std::vector<Render::DescriptorManager::BufferInfo>& bufferInfos, uint32_t binding, uint32_t numDescs = 1, bool isPerFrame = false);
	void RegisterAccelerationStructure(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
		vk::AccelerationStructureKHR accelStructure, uint32_t binding, bool isPerFrame = false);

	Render::Mesh UploadMesh(const Plume::Mesh& engineMesh);

	struct BLASInput
	{
		vk::AccelerationStructureGeometryTrianglesDataKHR _triangles;
		vk::AccelerationStructureGeometryKHR _geometry;
		vk::AccelerationStructureBuildRangeInfoKHR _buildRangeInfo;
	};

	BLASInput ConvertMeshToBlasInput(const Mesh& mesh, vk::GeometryFlagBitsKHR rtGeometryFlags);

	vk::RenderingAttachmentInfo CreateAttachment(vk::Format format, vk::ImageUsageFlagBits usage, Render::Image* image);

	void SubmitCmdImmediately(std::function<void(vk::CommandBuffer cmd)>&& function, vk::CommandBuffer cmd);

	struct UploadContext
	{
		vk::Fence _uploadFence;
		vk::CommandPool _commandPool;
		vk::CommandBuffer _commandBuffer;
	};

	const UploadContext& GetUploadContext() const { return _uploadContext; }

	SDL_Window* _pWindow;
	vk::Extent2D _windowExtent;
	vk::Extent3D _windowExtent3D;

	vk::Format _swapchainImageFormat = {};
	vk::Format _frameBufferFormat = vk::Format::eR16G16B16A16Sfloat;
	vk::Format _depthFormat = vk::Format::eD32Sfloat;

	vk::PhysicalDeviceProperties2 _gpuProperties;
	vk::PhysicalDeviceRayTracingPipelinePropertiesKHR _rtProperties;

	vk::Queue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VmaAllocator _allocator;
	ConfigurationVariables _renderCfg;

	DeletionQueue _mainDeletionQueue;

	Image _intermediateImage;

	vk::SwapchainKHR _swapchain;
	std::vector<Image> _swapchainImages;

	struct FrameData
	{
		vk::Semaphore _presentSemaphore, _renderSemaphore;
		vk::Fence _renderFence;

		vk::CommandPool _commandPool;
		vk::CommandBuffer _mainCommandBuffer;

		Render::Buffer _objectBuffer;
	};

	FrameData _frames[FRAME_OVERLAP];
	FrameData& GetCurrentFrameData();

	Render::Image& GetCurrentSwapchainImage() { return _swapchainImages[_swapchainImageIndex]; }

	vk::CommandBuffer GetCurrentCommandBuffer() { return GetCurrentFrameData()._mainCommandBuffer; }

	void BeginFrameRendering();
	void EndFrameRendering();

	void Present();

	struct PushConstantsInfo
	{
		void* pData = nullptr;
		size_t size = 0;
		vk::ShaderStageFlags shaderStages = {};
	};

	void DrawObjects(const std::vector<Object>& objects, const Render::Pass& pass, PushConstantsInfo* pPushConstantsInfo = nullptr, bool useCamLightingBuffer = false);
	void DrawScreenQuad(const Render::Pass& pass, PushConstantsInfo* pPushConstantsInfo = nullptr, bool useCamLightingBuffer = false);

	void TraceRays(const Render::Pass& pass, PushConstantsInfo* pPushConstantsInfo = nullptr, bool useCamLightingBuffer = false);

private:
	static std::unique_ptr<Backend> _pInstance;

	UploadContext _uploadContext;

	vk::Instance _libInstance; // Vulkan library handle
	vk::DebugUtilsMessengerEXT _debug_messenger;
	vk::PhysicalDevice _chosenGPU; // default GPU
	vk::Device _device; // commands will be executed on this
	vk::SurfaceKHR _surface; // window surface

	Render::DescriptorManager _descMng;

	uint64_t _frameId = 0;
	int32_t _swapchainImageIndex = -1;

	static bool _isInitialized;

	static constexpr size_t MAX_NUM_OF_SAMPLERS = 8;
	std::array<vk::Sampler, MAX_NUM_OF_SAMPLERS> _samplers;

	vk::CommandPool CreateCommandPool(uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags flags = {});
	vk::CommandBuffer CreateCommandBuffer(vk::CommandPool pool, uint32_t count = 1, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);

	void InitSwapchain();
	void InitCommands();
	void InitSyncStructures();
	void InitRaytracingProperties();
	void InitSamplers();
	void InitImGui();

	void AllocateDescriptorSets() { _descMng.AllocateSets(); }
	void UpdateDescriptorSets() { _descMng.UpdateSets(); }
};


struct RTShaderBindingTable
{
	Render::Buffer _buffer;
	vk::StridedDeviceAddressRegionKHR _rgenRegion;
	vk::StridedDeviceAddressRegionKHR _rmissRegion;
	vk::StridedDeviceAddressRegionKHR _rchitRegion;
	vk::StridedDeviceAddressRegionKHR _rcallRegion;
};


class PipelineState
{
	friend class Pass;
public:
	bool IsBuilt() const { return _isBuilt; }

private:
	vk::Pipeline _pipeline;
	vk::PipelineLayout _pipelineLayout;

	bool _isBuilt = false;

	Render::DescriptorSetFlags _usedDescriptorSets;
};


struct VertexInputDescription
{
	std::vector<vk::VertexInputBindingDescription> bindings;
	std::vector<vk::VertexInputAttributeDescription> attributes;

	vk::PipelineVertexInputStateCreateFlags flags;

	void ConstructFromVertex();
};


class Pass
{
	friend Backend;

public:
	enum class Type
	{
		eNone,
		eGeometryPass = 0,
		eLightingPass,
		ePostprocess,
		eSky,
		ePathTracing,

		eMaxValue
	};

	struct AttachmentStateInfo
	{
		bool blendEnable = false;
		Render::Image* pImage = nullptr;
		vk::ColorComponentFlags colorWriteFlags = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear;
		vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;

		bool isSwapchainImage = false;
	};

	struct PushConstantsInitInfo
	{
		uint32_t pcBufferSize = 0;
		vk::ShaderStageFlagBits stageFlags = {};
	};

	struct InitInfo
	{
		Render::DescriptorSetFlags usedDescSets;
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
		int32_t viewportWidth = -1;
		int32_t viewportHeight = -1;
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
		const std::vector<AttachmentStateInfo>* pColorAttachmentInfos = nullptr;
		AttachmentStateInfo* pDepthAttachment = nullptr;
		const std::vector<std::string>* pShaderNames = nullptr;
		vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1;
		bool bDepthTest = true;
		bool bDepthWrite = true;
		bool useVertexAttributes = false;
		vk::CompareOp compareOp = vk::CompareOp::eLessOrEqual;
		PushConstantsInitInfo pcInitInfo = {};
	};

	void Init(const InitInfo& initInfo);

	// Call from within the render loop
	void SetSwapchainImage(Image& curSwapchainImage);

	// Ray tracing specific structures and functions

	static constexpr int32_t RAY_TRACING_SHADER_GROUP_COUNT = static_cast<int32_t>(Render::Shader::RTStageIndices::eShaderGroupCount);
	static std::array<vk::RayTracingShaderGroupCreateInfoKHR, RAY_TRACING_SHADER_GROUP_COUNT> MakeRTShaderGroups();
	std::array<vk::PipelineShaderStageCreateInfo, RAY_TRACING_SHADER_GROUP_COUNT> MakeRTShaderStages(const std::vector<std::string>* pShaderNames = nullptr);

	struct RTInitInfo
	{
		Render::DescriptorSetFlags usedDescSets;
		const std::vector<std::string>* pShaderNames = nullptr;
		PushConstantsInitInfo pcInitInfo = {};
	};

	void InitRT(const RTInitInfo& initInfo);

	vk::Pipeline GetPipeline() const { return _pso._pipeline; }
	vk::PipelineLayout GetPipelineLayout() const { return _pso._pipelineLayout; }

private:
	// State manager functions. Not all states have corresponding Make...() functions for now, as their defaults have
	// been sufficient so far

	void MakeInputAssembly(vk::PrimitiveTopology topology);
	void MakeViewportAndScissor(int32_t width, int32_t height);
	void MakeRasterizationState(vk::PolygonMode polygonMode, vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone);
	void MakeColorBlendAttachmentState(int32_t attachmentId, bool blendEnable, vk::ColorComponentFlags colorWriteFlags);
	void MakeMultisamplingState(vk::SampleCountFlagBits numSamples);
	void MakeDepthStencilState(bool bDepthTest, bool bDepthWrite, vk::CompareOp compareOp);

	VertexInputDescription _vertexInputDesc = {};

	vk::PipelineVertexInputStateCreateInfo _vertexInputInfo;
	vk::PipelineInputAssemblyStateCreateInfo _inputAssembly;
	vk::Viewport _viewport;
	vk::Rect2D _scissor;
	vk::PipelineRasterizationStateCreateInfo _rasterizer;
	std::vector<vk::PipelineColorBlendAttachmentState> _colorBlendAttachments;
	vk::PipelineMultisampleStateCreateInfo _multisampling;
	vk::PipelineDepthStencilStateCreateInfo _depthStencil;

	Render::DescriptorSetFlags _usedDescSets;

	vk::RenderingInfo _renderingInfo;
	vk::PipelineRenderingCreateInfo _pipelineRenderingCreateInfo;
	vk::PushConstantRange _pushConstantRange;
	std::vector<Render::Shader> _shaders;
	std::vector<vk::PipelineShaderStageCreateInfo> _shaderStages;

	std::vector<vk::RenderingAttachmentInfo> _colorRenderingAttachmentInfos;
	vk::RenderingAttachmentInfo _depthAttachmentInfo;

	Render::RTShaderBindingTable _rtSbt;

	int32_t _swapchainTargetId = -1;
	bool _swapchainImageIsSet = false;

	void BuildShaderBindingTable();

	void BuildPipeline();
	void BuildRTPipeline(const std::array<vk::PipelineShaderStageCreateInfo, Render::Pass::RAY_TRACING_SHADER_GROUP_COUNT>& shaderStages);

	PipelineState _pso;
};


struct Object
{
	Mesh mesh;
	const Plume::Model* model;
	glm::mat4 transformMatrix;
};


} // namespace Render
