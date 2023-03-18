# Plume Engine

![plume-v0 1](https://user-images.githubusercontent.com/55918604/225998504-cab5905c-f49e-477e-a29f-3d611d9a6aa4.png)

This is the repository for Plume, my toy rendering engine written in Vulkan and C++.

Currently it supports basic scene rendering and texturing, using multiple buffers in the swapchain; it uses one dynamic descriptor and one uniform buffer for all frames for both camera, scene data and SSBO for object transform data, and mipmap generation.

The engine also supports smooth WASD camera movement (with LShift to move up and LCtrl to move down).

## Running the code

As the architecture of the engine is based on the brilliant Vulkan Guide by Victor Blanco, to run it and play around with it on your own, you can follow the instructions here: https://vkguide.dev/docs/chapter-0/building_project/.

Note that Vulkan validation layers are enabled by default. To turn them off, change `request_validation_layers(true)` to `request_validation_layers(false)` on line 83 in the `src/vk_engine.cpp` file.

## Work in progress (loosely arranged in order of priority)

* Texture arrays
* Point lights
* Multiple lights
* Specular lighting
* Alpha blending
* MSAA
* More advanced lighting (shadows, deferred shading, PBR, etc.)
* Basic raytracing
* Asset system
* GPU driven rendering
* ...etc.

## Acknowledgements

This project is based on the Vulkan Guide by Victor Blanco (https://vkguide.dev/), Vulkan Tutorial by Alexander Overvoorde (https://vulkan-tutorial.com/) and Vulkan samples by Sascha Willems (https://github.com/SaschaWillems/Vulkan).

