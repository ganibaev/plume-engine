#pragma once

#include "vk_types.h"
#include <vector>
#include <deque>
#include <tuple>
#include <functional>
#include <string>
#include <iostream>

#include "vk_camera.h"
#include "vk_mesh.h"

#include <glm/glm.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#define VK_CHECK(x)																\
	do																			\
	{																			\
		VkResult err = static_cast<VkResult>(x);								\
		if (err)																\
		{																		\
			std::cout << "Detected Vulkan error: " << err << std::endl;			\
			abort();															\
		}																		\
	} while (0)

constexpr int NUM_TEXTURE_TYPES = 4;

constexpr int NUM_GBUFFER_ATTACHMENTS = 4;

constexpr uint32_t DIFFUSE_TEX_SLOT = 0;
constexpr uint32_t METALLIC_TEX_SLOT = 1;
constexpr uint32_t ROUGHNESS_TEX_SLOT = 2;
constexpr uint32_t NORMAL_MAP_SLOT = 3;
constexpr uint32_t TLAS_SLOT = 4;

constexpr uint32_t GBUFFER_POSITION_SLOT = 0;
constexpr uint32_t GBUFFER_NORMAL_SLOT = 1;
constexpr uint32_t GBUFFER_ALBEDO_SLOT = 2;
constexpr uint32_t GBUFFER_METALLIC_ROUGHNESS_SLOT = 3;

class PipelineBuilder {
public:
	std::vector<vk::PipelineShaderStageCreateInfo> _shaderStages;
	vk::PipelineVertexInputStateCreateInfo _vertexInputInfo;
	vk::PipelineInputAssemblyStateCreateInfo _inputAssembly;
	vk::Viewport _viewport;
	vk::Rect2D _scissor;
	vk::PipelineRasterizationStateCreateInfo _rasterizer;
	vk::PipelineColorBlendAttachmentState _colorBlendAttachment;
	vk::PipelineMultisampleStateCreateInfo _multisampling;
	vk::PipelineDepthStencilStateCreateInfo _depthStencil;
	vk::PipelineLayout _pipelineLayout;

	vk::PipelineRenderingCreateInfo _renderingCreateInfo;

	vk::Pipeline buildPipeline(vk::Device device);
};

struct MaterialSet
{
	vk::DescriptorSet diffuseTextureSet;
	vk::DescriptorSet metallicTextureSet;
	vk::DescriptorSet roughnessTextureSet;
	vk::DescriptorSet normalMapSet;
	vk::DescriptorSet skyboxSet;
	vk::Pipeline pipeline;
	vk::PipelineLayout pipelineLayout;
};

struct UploadContext
{
	vk::Fence _uploadFence;
	vk::CommandPool _commandPool;
	vk::CommandBuffer _commandBuffer;
};

struct RenderObject
{
	Mesh* mesh;
	Model* model;
	MaterialSet* materialSet;
	glm::mat4 transformMatrix;

	bool operator<(const RenderObject& other) const
	{
		return std::tie(materialSet, mesh) < std::tie(other.materialSet, other.mesh);
	}
};

struct MeshPushConstants
{
	glm::mat4 render_matrix;
};

constexpr uint32_t FRAME_OVERLAP = 3;
constexpr uint32_t NUM_LIGHTS = 3;

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 invView;
	glm::mat4 proj;
	glm::mat4 viewproj;
};

struct DirectionalLight
{
	glm::vec4 direction; // w for sun power
	glm::vec4 color;
};

struct PointLight
{
	glm::vec4 position;
	glm::vec4 color; // w for light intensity
};

struct GPUSceneData
{
	glm::vec4 fogColor; // w for exponent
	glm::vec4 fogDistances; // x -- min, y -- max
	glm::vec4 ambientLight; // a for ambient light intensity
	DirectionalLight dirLight;
	std::array<PointLight, NUM_LIGHTS> pointLights;
};

struct GPUObjectData
{
	glm::mat4 modelMatrix;
};

struct Texture
{
	AllocatedImage image;
	vk::ImageView imageView;
};

struct FrameData
{
	vk::Semaphore _presentSemaphore, _renderSemaphore;
	vk::Fence _renderFence;

	vk::CommandPool _commandPool;
	vk::CommandBuffer _mainCommandBuffer;

	AllocatedBuffer _objectBuffer;
	vk::DescriptorSet _objectDescriptor;
	vk::DescriptorSet _gBufferDescriptorSet;
};

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		for (auto& func : deletors)
		{
			func();
		}

		deletors.clear();
	}
};

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber{ 0 };
	
	constexpr static float _camSpeed = 0.2f;

	vk::Extent2D _windowExtent{ 1920, 1080 };

	vk::Extent3D _windowExtent3D{ _windowExtent, 1 };

	struct SDL_Window* _window{ nullptr };

	// initializes everything in the engine
	void init();

	// shuts down the engine
	void cleanup();

	// draw loop
	void draw();

	// run main loop
	void run();

	void on_mouse_motion_callback();
	void on_mouse_scroll_callback(float yOffset);
	void on_keyboard_event_callback(SDL_Keycode sym);

	VmaAllocator _allocator;

	vk::Instance _instance; // Vulkan library handle
	vk::DebugUtilsMessengerEXT _debug_messenger; 
	vk::PhysicalDevice _chosenGPU; // default GPU
	vk::Device _device; // commands will be executed on this 
	vk::SurfaceKHR _surface; // window surface

	vk::PhysicalDeviceProperties2 _gpuProperties;
	vk::PhysicalDeviceRayTracingPipelinePropertiesKHR _rtProperties;

	UploadContext _uploadContext;

	void immediate_submit(std::function<void(vk::CommandBuffer cmd)>&& function, vk::CommandBuffer cmd);

	Scene _scene;

	Camera _camera = Camera(glm::vec3(2.8f, 6.0f, 40.0f));
	float _deltaTime = 0.0f;
	float _lastFrameTime = 0.0f;

	glm::vec3 _centralLightPos = { 2.8f, 10.0f, 17.5f };

	GPUSceneData _sceneParameters;
	AllocatedBuffer _camSceneBuffer;

	size_t pad_uniform_buffer_size(size_t originalSize);

	std::vector<AccelerationStructure> _bottomLevelASVec = {};
	AccelerationStructure _topLevelAS = {};

	vk::DescriptorSetLayout _globalSetLayout;
	vk::DescriptorSetLayout _tlasSetLayout;
	vk::DescriptorSetLayout _objectSetLayout;
	vk::DescriptorPool _descriptorPool;

	vk::DescriptorSet _globalDescriptor;

	vk::DescriptorSet _tlasDescriptorSet;

	vk::DescriptorSetLayout _textureSetLayout;
	vk::DescriptorSetLayout _cubemapSetLayout;

	vk::SwapchainKHR _swapchain;

	std::vector<vk::Image> _swapchainImages;
	vk::Format _swapchainImageFormat;

	std::vector<vk::ImageView> _swapchainImageViews;

	void image_layout_transition(vk::CommandBuffer cmd, vk::AccessFlags srcAccessMask,
		vk::AccessFlags dstAccessMask, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::Image image,
		vk::ImageAspectFlags aspectMask, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask);
	void switch_swapchain_image_layout(vk::CommandBuffer cmd, uint32_t swapchainImageIndex, bool beforeRendering);

	vk::ImageView _depthImageView;
	vk::Image _depthImage;
	vk::Format _depthFormat;

	vk::ImageView _lightingDepthImageView;
	AllocatedImage _lightingDepthImage;

	vk::Queue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame();

	vk::RenderingInfo _geometryPassInfo;
	vk::RenderingInfo _lightingPassInfo;

	vk::PipelineRenderingCreateInfo _geometryPassPipelineInfo;
	vk::PipelineRenderingCreateInfo _lightingPassPipelineInfo;

	vk::PipelineLayout _lightingPassPipelineLayout;
	vk::Pipeline _lightingPassPipeline;

	vk::DescriptorSetLayout _gBufferSetLayout;

	std::array<vk::Image, NUM_GBUFFER_ATTACHMENTS> _gBufferImages;
	std::array<vk::RenderingAttachmentInfo, NUM_GBUFFER_ATTACHMENTS> _gBufferColorAttachments;
	vk::RenderingAttachmentInfo _gBufferDepthAttachment;
	std::array<vk::Format, NUM_GBUFFER_ATTACHMENTS> _colorAttachmentFormats;

	// load shader module from .spirv
	bool load_shader_module(const char* filePath, vk::ShaderModule* outShaderModule);

	AllocatedBuffer create_buffer(size_t allocSize, vk::BufferUsageFlags usage, VmaMemoryUsage memUsage);
	AllocatedImage create_image(const vk::ImageCreateInfo& createInfo, VmaMemoryUsage memUsage);

	vk::DeviceAddress get_buffer_device_address(vk::Buffer buffer);
	BLASInput convert_to_blas_input(Mesh& mesh);
	
	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, MaterialSet> _materialSets;

	std::unordered_map<std::string, std::array<Texture, NUM_TEXTURE_TYPES>> _loadedTextures;

	Texture _skybox;
	RenderObject _skyboxObject;

	// create material set and add to the map
	MaterialSet* create_material_set(vk::Pipeline pipeline, vk::PipelineLayout layout, const std::string& name);

	MaterialSet* get_material_set(const std::string& name);

	Model* get_model(const std::string& name);

	void draw_objects(vk::CommandBuffer cmd, RenderObject* first, size_t count);
	void draw_screen_quad(vk::CommandBuffer cmd, vk::PipelineLayout pipelineLayout, vk::Pipeline pipeline);
	void draw_skybox(vk::CommandBuffer cmd, RenderObject& object);

	DeletionQueue _mainDeletionQueue;
private:

	void init_vulkan();
	void init_swapchain();
	void init_commands();

	void init_gbuffer_attachments();
	void create_attachment(vk::Format format, vk::ImageUsageFlagBits usage, vk::RenderingAttachmentInfo& attachmentInfo,
		vk::Image* image = nullptr);

	void init_raytracing();

	void init_sync_structures();

	void init_descriptors();

	void init_pipelines();

	void fill_tex_descriptor_sets(std::vector<vk::DescriptorImageInfo>& texBufferInfos,
		const std::vector<std::string>& texNames, Model* model, uint32_t texSlot);

	void init_scene();

	void load_meshes();

	void init_blas();

	void init_tlas();

	void load_material_texture(Texture& tex, const std::string& texName, const std::string& matName,
		uint32_t texSlot, bool generateMipmaps = true, vk::Format format = vk::Format::eR8G8B8A8Srgb);

	void load_skybox(Texture& skybox, const std::string& directory);

	void load_images();

	void upload_mesh(Mesh& mesh);
};
