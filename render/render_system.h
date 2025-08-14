#pragma once

#include "render_types.h"
#include "render_cfg.h"
#include <vector>
#include <tuple>
#include <string>
#include <iostream>
#include <unordered_set>

#include "../engine/plm_camera.h"
#include "../engine/plm_lights.h"

#include "render_core.h"
#include "render_descriptors.h"
#include "render_mesh.h"

#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/gtx/io.hpp>


#define VK_CHECK(x)																\
	do																			\
	{																			\
		VkResult err = static_cast<VkResult>(x);								\
		if (err)																\
		{																		\
			std::cout << "Detected Vulkan error: " << err << std::endl;			\
			abort();															\
		}																		\
	} while (0)

struct SDL_Window;


enum class RenderMode
{
	eHybrid = 0,
	ePathTracing = 1
};

struct MeshPushConstants
{
	glm::mat4 render_matrix;
};


struct FrameContext
{
	Render::Image prevFrameImage;
	Render::Image depthImage;

	Render::Image ptPositionImage;
	Render::Image prevPositionImage;

	Render::Buffer camLightingBuffer;
};

namespace Render
{

class System
{
	friend Render::Backend;
public:
	struct InitData
	{
		const Plume::LightManager* pLightManager = nullptr;
		const Plume::Camera* pCam = nullptr;

		SDL_Window* pWindow = nullptr;
		vk::Extent2D windowExtent{ 0, 0 };
	};

	bool _isInitialized = false;

	constexpr static RenderMode _renderMode = RenderMode::ePathTracing;

	void init_backend_and_data(const InitData& initData);

	// initializes everything in the rendering system
	void init(const InitData& initData);

	// shuts down the rendering system
	void cleanup();

	// draw loop
	void render_frame();

	void setup_debug_ui_frame();

	std::unordered_set<std::string> _supportedExtensions;

	Scene _scene;

	const Plume::LightManager* _pLightManager = nullptr;

	const Plume::Camera* _pCamera = nullptr;
	Plume::Camera _prevCamera = Plume::Camera(glm::vec3(2.8f, 6.0f, 40.0f));

	glm::mat4 _prevViewProj = glm::identity<glm::mat4>();

	bool _showDebugUi = false;

	vk::DeviceSize align_up(vk::DeviceSize originalSize, vk::DeviceSize alignment);

	std::vector<AccelerationStructure> _bottomLevelASVec = {};
	AccelerationStructure _topLevelAS = {};

	RayPushConstants _rayConstants = {};

	void switch_swapchain_image_layout(uint32_t swapchainImageIndex, bool beforeRendering);
	void switch_intermediate_image_layout(bool beforeRendering);
	void switch_frame_image_layout(Render::Image& image);

	FrameContext _frameCtx;

	// effectively remove frame accumulation limit for path tracing, but reserve it for future use
	int _maxAccumFrames = 60000;

	vk::PipelineRenderingCreateInfo _geometryPassPipelineInfo;
	vk::PipelineRenderingCreateInfo _lightingPassPipelineInfo;

	std::array<Render::Image, NUM_GBUFFER_ATTACHMENTS> _gBufferImages;
	vk::RenderingAttachmentInfo _gBufferDepthAttachment;
	std::array<vk::Format, NUM_GBUFFER_ATTACHMENTS> _colorAttachmentFormats;

	Render::RTShaderBindingTable _pathTracingShaderBindingTable;
	Render::Buffer _rtSbtBuffer;
	vk::StridedDeviceAddressRegionKHR _rgenRegion;
	vk::StridedDeviceAddressRegionKHR _rmissRegion;
	vk::StridedDeviceAddressRegionKHR _rchitRegion;
	vk::StridedDeviceAddressRegionKHR _rcallRegion;

	void copy_image(vk::CommandBuffer cmd, vk::ImageAspectFlags aspectMask, vk::Image srcImage,
		vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::Extent3D extent);

	std::vector<RenderObject> _renderables;

	std::array<Render::Pass, static_cast<size_t>(RenderPassType::eMaxValue)> _renderPasses;

	std::unordered_map<std::string, std::array<Render::Image, NUM_TEXTURE_TYPES>> _loadedTextures;

	Render::Image _skybox;
	RenderObject _skyboxObject;

	Model* get_model(const std::string& name);

	void upload_cam_scene_data(RenderObject* first, size_t count);

	void gbuffer_geometry_pass();
	void gbuffer_lighting_pass();
	void sky_pass();
	void fxaa_pass();
	void denoiser_pass();

	void debug_ui_pass(vk::CommandBuffer cmd, vk::ImageView targetImageView);

	void path_tracing_pass();

private:
	void init_frame_context();

	void init_gbuffer_images();

	void init_descriptors();

	void init_passes();
	void init_geometry_pass();
	void init_lighting_pass();
	void init_postprocess_pass();
	void init_sky_pass();

	void init_path_tracing_gbuffer_images();
	void init_path_tracing_pass();

	void init_scene();

	void load_meshes();

	void init_blas();

	void init_tlas();

	void init_rt_descriptors();

	bool load_material_texture(Render::Image& tex, const std::string& texName, const std::string& matName,
		uint32_t texSlot, bool generateMipmaps = true, vk::Format format = vk::Format::eR8G8B8A8Srgb);

	void load_skybox(Render::Image& skybox, const std::string& directory);

	void load_images();

	void reset_frame();

	void update_rt_frame();

	void upload_mesh(Mesh& mesh);
};

} // namespace Render
