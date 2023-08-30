#pragma once

#include "vk_types.h"
#include <vector>
#include <deque>
#include <tuple>
#include <functional>
#include <string>

#include "vk_camera.h"
#include "vk_mesh.h"

#include <glm/glm.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

constexpr int NUM_TEXTURE_TYPES = 3;

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
	
	vk::Pipeline buildPipeline(vk::Device device, vk::RenderPass pass);
};

struct MaterialSet
{
	vk::DescriptorSet diffuseTextureSet;
	vk::DescriptorSet ambientTextureSet;
	vk::DescriptorSet specularTextureSet;
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
	glm::vec4 data;
	glm::mat4 render_matrix;
	glm::uint num_materials;
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

	vk::Extent2D _windowExtent{ 1700 , 900 };

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

	vk::PhysicalDeviceProperties _gpuProperties;

	UploadContext _uploadContext;

	void immediate_submit(std::function<void(vk::CommandBuffer cmd)>&& function);

	vk::SampleCountFlagBits _msaaSamples = vk::SampleCountFlagBits::e8;
	
	Camera _camera = Camera(glm::vec3(2.8f, 6.0f, 40.0f));
	float _deltaTime = 0.0f;
	float _lastFrameTime = 0.0f;

	glm::vec3 _centralLightPos = { 2.8f, 10.0f, 17.5f };

	GPUSceneData _sceneParameters;
	AllocatedBuffer _camSceneBuffer;

	size_t pad_uniform_buffer_size(size_t originalSize);

	vk::DescriptorSetLayout _globalSetLayout;
	vk::DescriptorSetLayout _objectSetLayout;
	vk::DescriptorPool _descriptorPool;

	vk::DescriptorSet _globalDescriptor;

	vk::DescriptorSetLayout _textureSetLayout;
	vk::DescriptorSetLayout _cubemapSetLayout;

	vk::SwapchainKHR _swapchain;

	std::vector<vk::Image> _swapchainImages;
	vk::Format _swapchainImageFormat;

	std::vector<vk::ImageView> _swapchainImageViews;

	vk::ImageView _colorImageView;
	AllocatedImage _colorImage;
	vk::Format _colorFormat;

	vk::ImageView _depthImageView;
	AllocatedImage _depthImage;
	vk::Format _depthFormat;

	vk::Queue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame();

	vk::RenderPass _renderPass;
	std::vector<vk::Framebuffer> _framebuffers;

	// load shader module from .spirv
	bool load_shader_module(const char* filePath, vk::ShaderModule* outShaderModule);

	vk::PipelineLayout _meshPipelineLayout;

	AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, vk::BufferUsageFlags usage, VmaMemoryUsage memUsage);
	
	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Model> _models;
	std::unordered_map<std::string, MaterialSet> _materialSets;

	std::unordered_map<std::string, std::array<Texture, NUM_TEXTURE_TYPES>> _loadedTextures;

	Texture _skybox;

	// create material set and add to the map
	MaterialSet* create_material_set(vk::Pipeline pipeline, vk::PipelineLayout layout, const std::string& name);

	MaterialSet* get_material_set(const std::string& name);

	Model* get_model(const std::string& name);

	void draw_objects(vk::CommandBuffer cmd, RenderObject* first, int count);

	DeletionQueue _mainDeletionQueue;
private:

	void init_vulkan();
	void init_swapchain();
	void init_commands();

	void init_default_renderpass();
	void init_framebuffers();

	void init_sync_structures();

	void init_descriptors();

	void init_pipelines();

	void fill_tex_descriptor_sets(std::vector<vk::DescriptorImageInfo>& texBufferInfos,
		const std::vector<std::string>& texNames, Model* model, uint32_t texSlot);

	void init_scene();

	void load_meshes();

	void load_material_texture(Texture& tex, const std::string& texName, const std::string& matName, uint32_t texSlot);

	void load_skybox(Texture& skybox, const std::string& directory);

	void load_images();

	void upload_mesh(Mesh& mesh);
};
