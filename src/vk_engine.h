// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber {0};

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

	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; 
	VkPhysicalDevice _chosenGPU; // default GPU
	VkDevice _device; // commands will be executed on this 
	VkSurfaceKHR _surface; // window surface

	VkSwapchainKHR _swapchain;

	std::vector<VkImage> _swapchainImages;
	VkFormat _swapchainImageFormat;

	std::vector<VkImageView> _swapchainImageViews;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

private:

	void init_vulkan();
	void init_swapchain();
	void init_commands();

	void init_default_renderpass();
	void init_framebuffers();

	void init_sync_structures();
};
