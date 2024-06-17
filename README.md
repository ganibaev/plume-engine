# Plume Engine

![plume-v0 16](https://github.com/ganibaev/plume-engine/assets/55918604/9ce43291-aeba-4681-944a-02287ca8898a)
![plume-v0 16EXT](https://github.com/ganibaev/plume-engine/assets/55918604/c30e2887-3fbc-4172-adc0-e274f0b398a5)
![plume-v0 16EXT2](https://github.com/ganibaev/plume-engine/assets/55918604/af016d77-5ff1-4a9f-a3db-88769498fa7e)
***Pic 1, 2, 3:** Rendered in path tracing mode.*

![plume-v0 13](https://github.com/ganibaev/plume-engine/assets/55918604/047ae73f-af23-46d3-8ef9-f62027e21ce6)
***Pic 4:** Rendered in hybrid mode.*


This is an experimental branch for researching Neural Radiance Caching (Müller et al., 2021) and Dedicated Temporal Adaptation — my technique aimed to improve NRC (both available in path tracing mode through a Dear ImGui window). Please note that both of these techniques (and thus this branch) require an NVIDIA GPU with real-time ray tracing support.

The engine has two modes of rendering: path tracing and hybrid (rasterization + ray traced shadows).

Both modes use normal mapping, texturing with an arbitrary number of textures using variable descriptor count and nonuniform descriptor indexing (bindless texturing), path tracing mode uses camera jittering, motion vectors, temporal sample accumulation, Russian roulette path termination, shader execution reordering and a Morrone denoiser (also known as *glslSmartDenoise*), while hybrid mode uses deferred shading with dynamic rendering, has real-time ray traced shadows, PBR and FXAA.

The engine also has smooth mouse and WASD camera movement (with LShift to move up and LCtrl to move down), you can zoom in and out via mouse scroll wheel and move the main light source in world space with arrow keys (RShift to move up and RCtrl to move down).

You can press F2 to enable a Dear ImGui window with various configuration options, or press F8 to just show the cursor and be able to move it, or ESC to close the engine.

## Work in progress (loosely arranged in order of priority)

* Denoising improvements
* Texture system overhaul
* Motion vector filtering
* GI for Hybrid mode
* ReSTIR, ReSTIR GI
* GPU driven rendering for meshes
* ...etc.

## Running the code

Note that Plume requires a GPU with real-time ray tracing support.

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

By default, Vulkan validation layers are enabled in Debug mode and disabled in Release mode. To switch between hybrid and path tracing modes (for now) you should change the variable `VulkanEngine::_renderMode` on line 215 in the `src/vk_engine.h` file.

## Acknowledgements

This project is based on the Vulkan Guide by Victor Blanco (https://vkguide.dev/), Vulkan Tutorial by Alexander Overvoorde (https://vulkan-tutorial.com/), Vulkan samples by Sascha Willems (https://github.com/SaschaWillems/Vulkan), NVIDIA Vulkan Ray Tracing Tutorials (https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR), NVIDIA Vulkan Ray Tracing Samples (https://github.com/nvpro-samples/vk_raytrace/, https://github.com/nvpro-samples/vk_mini_samples), Vulkan Game Engine Tutorial by Brendan Galea (https://github.com/blurrypiano/littleVulkanEngine), glslSmartDeNoise by Michele Morrone (https://github.com/BrutPitt/glslSmartDeNoise) and Learn OpenGL by Joey de Vries (https://learnopengl.com/), Neural Radiance Caching by Thomas Müller et al. (Müller, T., Rousselle, F., Novák, J. and Keller, A., 2021. Real-time neural radiance caching for path tracing. arXiv preprint arXiv:2106.12372).
