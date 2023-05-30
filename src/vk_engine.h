#pragma once

#include <vk_types.h>
#include <vector>
#include <deque>
#include <tuple>
#include <functional>
#include <string>

#include "vk_mesh.h"

#include <glm/glm.hpp>

class PipelineBuilder {
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	VkPipelineColorBlendAttachmentState _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;
	VkPipelineLayout _pipelineLayout;
	
	VkPipeline buildPipeline(VkDevice device, VkRenderPass pass);
};

struct Material
{
	VkDescriptorSet textureSet{	VK_NULL_HANDLE };
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct UploadContext
{
	VkFence _uploadFence;
	VkCommandPool _commandPool;
	VkCommandBuffer _commandBuffer;
};

struct RenderObject
{
	Mesh* mesh;
	Material* material;
	glm::mat4 transformMatrix;

	bool operator<(const RenderObject& other) const
	{
		return std::tie(material, mesh) < std::tie(other.material, other.mesh);
	}
};

struct MeshPushConstants
{
	glm::vec4 data;
	glm::mat4 render_matrix;
	glm::uint num_materials;
};

constexpr unsigned int FRAME_OVERLAP = 2;

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 invView;
	glm::mat4 proj;
	glm::mat4 viewproj;
};

struct GPUSceneData
{
	glm::vec4 fogColor; // w for exponent
	glm::vec4 fogDistances; // x -- min, y -- max
	glm::vec4 ambientLight; // a for ambient light intensity
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
	// point light
	glm::vec4 pointLightPosition;
	glm::vec4 pointLightColor; // w for light intensity
};

struct GPUObjectData
{
	glm::mat4 modelMatrix;
};

struct Texture
{
	AllocatedImage image;
	VkImageView imageView;
};

struct FrameData
{
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	AllocatedBuffer _objectBuffer;
	VkDescriptorSet _objectDescriptor;
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
	int _frameNumber {0};
	
	constexpr static float _camSpeed = 0.2f;

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	VmaAllocator _allocator;

	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; 
	VkPhysicalDevice _chosenGPU; // default GPU
	VkDevice _device; // commands will be executed on this 
	VkSurfaceKHR _surface; // window surface

	VkPhysicalDeviceProperties _gpuProperties;

	UploadContext _uploadContext;

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	glm::vec3 _camPos = { -2.8f, -6.0f, -40.0f };
	glm::vec3 _lightPos = { 2.8f, 12.0f, -5.0f };

	GPUSceneData _sceneParameters;
	AllocatedBuffer _camSceneBuffer;

	size_t pad_uniform_buffer_size(size_t originalSize);

	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorPool _descriptorPool;

	VkDescriptorSet _globalDescriptor;

	VkDescriptorSetLayout _textureSetLayout;

	VkSwapchainKHR _swapchain;

	std::vector<VkImage> _swapchainImages;
	VkFormat _swapchainImageFormat;

	std::vector<VkImageView> _swapchainImageViews;

	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame();

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	// load shader module from .spirv
	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);

	VkPipelineLayout _meshPipelineLayout;

	AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memUsage);
	
	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Mesh> _meshes;
	std::unordered_map<std::string, Material> _materials;

	std::unordered_map<std::string, Texture> _loadedTextures;

	// create material and add to the map
	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

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

	void init_scene();

	void load_meshes();

	void load_images();

	void upload_mesh(Mesh& mesh);
};
