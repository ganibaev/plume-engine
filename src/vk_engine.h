// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <deque>
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
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject
{
	Mesh* mesh;
	Material* material;
	glm::mat4 transformMatrix;
};

struct MeshPushConstants
{
	glm::vec4 data;
	glm::mat4 render_matrix;
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

	glm::vec3 _camPos = { 0.0f, -6.0f, -10.0f };

	VkSwapchainKHR _swapchain;

	std::vector<VkImage> _swapchainImages;
	VkFormat _swapchainImageFormat;

	std::vector<VkImageView> _swapchainImageViews;

	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	// load shader module from .spirv
	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);

	VkPipelineLayout _meshPipelineLayout;

	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Mesh> _meshes;
	std::unordered_map<std::string, Material> _materials;

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

	void init_pipelines();

	void init_scene();

	void load_meshes();

	void upload_mesh(Mesh& mesh);
};
