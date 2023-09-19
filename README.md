# Plume Engine

![plume-v0 11](https://github.com/ganibaev/plume-engine/assets/55918604/4f75ce01-c9c9-4680-85b6-4a1711590c3a)
![plume-v0 11EXT](https://github.com/ganibaev/plume-engine/assets/55918604/7d176ed5-839a-439e-9af2-10271c4b2c5a)

This is the repository for Plume, my toy rendering engine written in Vulkan and C++.

Currently it uses deferred shading with dynamic rendering, has ray-traced shadows, Blinn-Phong lighting, normal mapping, texturing with an arbitrary number of textures using variable descriptor count and nonuniform descriptor indexing.

The engine also has smooth mouse and WASD camera movement (with LShift to move up and LCtrl to move down), you can zoom in and out via mouse scroll wheel and move the main light source in world space with arrow keys (RShift to move up and RCtrl to move down).

## Work in progress (loosely arranged in order of priority)

* PBR
* FXAA
* More advanced raytracing
* GPU driven rendering
* ...etc.

## Running the code

Note that Plume requires a GPU with real-time raytracing support.

To run the code and play around with it on your own on Windows, you can do the following:
```bash
git clone --recursive https://github.com/ganibaev/plume-engine.git
cd plume-engine
mkdir build
cd build
cmake ..
msbuild plume.sln
```
The executable will be located in `{project-root}/build/bin/Debug/plume.exe`.

Vulkan validation layers are enabled by default. To turn them off, set `ENABLE_VALIDATION_LAYERS` to `false` on line 17 in the `src/vk_engine.cpp` file.

## Acknowledgements

This project is based on the Vulkan Guide by Victor Blanco (https://vkguide.dev/), Vulkan Tutorial by Alexander Overvoorde (https://vulkan-tutorial.com/), Vulkan samples by Sascha Willems (https://github.com/SaschaWillems/Vulkan), NVIDIA Vulkan Ray Tracing Tutorials (https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR), Vulkan Game Engine Tutorial by Brendan Galea (https://github.com/blurrypiano/littleVulkanEngine) and Learn OpenGL by Joey de Vries (https://learnopengl.com/).
