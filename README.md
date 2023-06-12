# Plume Engine

![plume-v0_4](https://github.com/ganibaev/plume-engine/assets/55918604/7526272d-9496-4bb6-8781-528fd907f2ad)

This is the repository for Plume, my toy rendering engine written in Vulkan and C++.

Currently it supports Blinn-Phong lighting, fairly complex scene rendering and texturing with an arbitrary number of textures using variable descriptor count and nonuniform descriptor indexing. It also uses multiple buffers in the swapchain, one dynamic descriptor and one uniform buffer for all frames for both camera and scene data, SSBO for object transform data, and it generates mipmaps.

The engine supports smooth mouse and WASD camera movement (with LShift to move up and LCtrl to move down), you can zoom in and out via mouse scroll wheel and move the light source in world space with arrow keys (RShift to move up and RCtrl to move down).

## Work in progress (loosely arranged in order of priority)

* Alpha blending
* MSAA
* More advanced lighting (shadows, deferred shading, PBR, etc.)
* Basic raytracing
* Asset system
* GPU driven rendering
* ...etc.

## Running the code

As the original architecture of the engine is based on the brilliant Vulkan Guide by Victor Blanco, to run it and play around with it on your own, you can follow the instructions here: https://vkguide.dev/docs/chapter-0/building_project/.

Note that Vulkan validation layers are enabled by default. To turn them off, change `request_validation_layers(true)` to `request_validation_layers(false)` on line 83 in the `src/vk_engine.cpp` file.

## Acknowledgements

This project is based on the Vulkan Guide by Victor Blanco (https://vkguide.dev/), Vulkan Tutorial by Alexander Overvoorde (https://vulkan-tutorial.com/), Vulkan samples by Sascha Willems (https://github.com/SaschaWillems/Vulkan), Vulkan Game Engine Tutorial by Brendan Galea (https://github.com/blurrypiano/littleVulkanEngine) and Learn OpenGL by Joey de Vries (https://learnopengl.com/).
