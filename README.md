# Plume Engine

![plume-v0 16](https://github.com/ganibaev/plume-engine/assets/55918604/9ce43291-aeba-4681-944a-02287ca8898a)
![plume-v0 16EXT](https://github.com/ganibaev/plume-engine/assets/55918604/c30e2887-3fbc-4172-adc0-e274f0b398a5)
![plume-v0 16EXT2](https://github.com/ganibaev/plume-engine/assets/55918604/af016d77-5ff1-4a9f-a3db-88769498fa7e)
***Pic 1, 2, 3:** Rendered in path tracing mode.*

![plume-v0 13](https://github.com/ganibaev/plume-engine/assets/55918604/047ae73f-af23-46d3-8ef9-f62027e21ce6)
***Pic 4:** Rendered in hybrid mode.*


This is the repository for Plume, my toy rendering engine written in Vulkan and C++.

It has two modes of rendering: path tracing and hybrid (rasterization + ray traced shadows).

Both modes use normal mapping, texturing with an arbitrary number of bindless textures; path tracing mode uses camera jittering, motion vectors, temporal sample accumulation, Russian roulette path termination, shader execution reordering and a simple denoiser, while hybrid mode uses deferred shading with dynamic rendering, has real-time ray traced shadows, PBR materials and FXAA.

You can press F2 to enable a Dear ImGui window with various configuration options (for example, to get images like in pics 1-3, turn Use Motion Vectors off), press F8 to just show the cursor and be able to move it, zoom in and out via mouse scroll wheel, or press ESC to close the engine.

## Running the code

Please note that at the moment Plume requires a GPU with real-time ray tracing support.

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

By default, Vulkan validation layers are enabled in Debug mode and disabled in Release mode. To switch between hybrid and path tracing modes (for now) you should change the variable `RenderSystem::_renderMode` on line 79 in the `render/render_system.h` file.

## Acknowledgements

This project is based on the Vulkan Guide by Victor Blanco (https://vkguide.dev/), Vulkan Tutorial by Alexander Overvoorde (https://vulkan-tutorial.com/), Vulkan samples by Sascha Willems (https://github.com/SaschaWillems/Vulkan), NVIDIA Vulkan Ray Tracing Tutorials (https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR), NVIDIA Vulkan Ray Tracing Samples (https://github.com/nvpro-samples/vk_raytrace/, https://github.com/nvpro-samples/vk_mini_samples), Vulkan Game Engine Tutorial by Brendan Galea (https://github.com/blurrypiano/littleVulkanEngine), glslSmartDeNoise by Michele Morrone (https://github.com/BrutPitt/glslSmartDeNoise) and Learn OpenGL by Joey de Vries (https://learnopengl.com/).
