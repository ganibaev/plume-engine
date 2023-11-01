# Plume Engine

![plume-v0 15](https://github.com/ganibaev/plume-engine/assets/55918604/8155572f-0387-4d26-9757-42ad6d82ca79)
![plume-v0 15EXT](https://github.com/ganibaev/plume-engine/assets/55918604/40bc3e6e-3197-4e6e-b672-3c7bfa58a1e0)
![plume-v0 14EXT](https://github.com/ganibaev/plume-engine/assets/55918604/d86f749c-7511-453e-9cda-1b04d3aefabd)
***Pic 1, 2, 3:** Rendered in path tracing mode.*

![plume-v0 13](https://github.com/ganibaev/plume-engine/assets/55918604/047ae73f-af23-46d3-8ef9-f62027e21ce6)
***Pic 4:** Rendered in hybrid mode.*


This is the repository for Plume, my toy rendering engine written in Vulkan and C++.

It has two modes of rendering: path tracing and hybrid (rasterization + ray traced shadows).

Both modes use normal mapping, texturing with an arbitrary number of textures using variable descriptor count and nonuniform descriptor indexing (bindless texturing), path tracing mode uses camera jittering, temporal sample accumulation, Russian roulette path termination and a Morrone denoiser (also known as *glslSmartDenoise*), while hybrid mode uses deferred shading with dynamic rendering, has real-time ray traced shadows, PBR and FXAA.

The engine also has smooth mouse and WASD camera movement (with LShift to move up and LCtrl to move down), you can zoom in and out via mouse scroll wheel and move the main light source in world space with arrow keys (RShift to move up and RCtrl to move down).

## Work in progress (loosely arranged in order of priority)

* Environment map in path tracing mode
* Disney PBR (e.g. https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf)
* IBL
* ReSTIR GI
* GPU driven rendering for meshes
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

Vulkan validation layers are enabled by default. To turn them off, set `ENABLE_VALIDATION_LAYERS` to `false` on line 17 in the `src/vk_engine.cpp` file. To switch between hybrid and path tracing modes (for now) change the variable `VulkanEngine::_renderMode` on line 215 in the `src/vk_engine.h` file.

## Acknowledgements

This project is based on the Vulkan Guide by Victor Blanco (https://vkguide.dev/), Vulkan Tutorial by Alexander Overvoorde (https://vulkan-tutorial.com/), Vulkan samples by Sascha Willems (https://github.com/SaschaWillems/Vulkan), NVIDIA Vulkan Ray Tracing Tutorials (https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR), Vulkan Game Engine Tutorial by Brendan Galea (https://github.com/blurrypiano/littleVulkanEngine), glslSmartDeNoise by Michele Morrone (https://github.com/BrutPitt/glslSmartDeNoise) and Learn OpenGL by Joey de Vries (https://learnopengl.com/).
