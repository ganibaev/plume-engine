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
#include "../engine/plm_scene.h"

#include "render_core.h"
#include "render_descriptors.h"

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
		const Plume::Scene* pScene = nullptr;

		SDL_Window* pWindow = nullptr;
		vk::Extent2D windowExtent{ 0, 0 };
	};

	bool _isInitialized = false;

	constexpr static RenderMode _renderMode = RenderMode::ePathTracing;

	void InitBackendAndData(const InitData& initData);

	// initializes everything in the rendering system
	void Init(const InitData& initData);

	// shuts down the rendering system
	void Cleanup();

	// draw loop
	void RenderFrame();

	void SetupDebugUIFrame();

	std::unordered_set<std::string> _supportedExtensions;

	const Plume::Scene* _pScene = nullptr;

	const Plume::LightManager* _pLightManager = nullptr;

	const Plume::Camera* _pCamera = nullptr;
	Plume::Camera _prevCamera = Plume::Camera(glm::vec3(2.8f, 6.0f, 40.0f));

	glm::mat4 _prevViewProj = glm::identity<glm::mat4>();

	bool _showDebugUi = false;

	vk::DeviceSize AlignUp(vk::DeviceSize originalSize, vk::DeviceSize alignment);

	std::vector<AccelerationStructure> _bottomLevelASVec = {};
	AccelerationStructure _topLevelAS = {};

	RayPushConstants _rayConstants = {};

	void SwitchSwapchainImageLayout(uint32_t swapchainImageIndex, bool beforeRendering);
	void SwitchIntermediateImageLayout(bool beforeRendering);
	void SwitchFrameImageLayout(Render::Image& image);

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

	void CopyImage(vk::CommandBuffer cmd, vk::ImageAspectFlags aspectMask, vk::Image srcImage,
		vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::Extent3D extent);

	std::vector<Render::Object> _renderables;

	std::array<Render::Pass, static_cast<size_t>(Render::Pass::Type::eMaxValue)> _renderPasses;

	std::unordered_map<std::string, std::array<Render::Image, NUM_TEXTURE_TYPES>> _loadedTextures;

	Render::Image _skybox;
	Render::Object _skyboxObject;

	void UploadCamSceneData(Render::Object* first, size_t count);

	void GBufferGeometryPass();
	void GBufferLightingPass();
	void SkyPass();
	void FXAAPass();
	void DenoiserPass();

	void DebugUIPass(vk::CommandBuffer cmd, vk::ImageView targetImageView);

	void PathTracingPass();

private:
	void InitFrameContext();

	void InitGBufferImages();

	void InitDescriptors();

	void InitPasses();
	void InitGeometryPass();
	void InitLightingPass();
	void InitPostprocessPass();
	void InitSkyPass();

	void InitPathTracingGBufferImages();
	void InitPathTracingPass();

	void InitRenderScene();

	void InitBLAS();

	void InitTLAS();

	void InitRTDescriptors();

	bool LoadMaterialTexture(Render::Image& tex, const std::string& texName, const std::string& matName,
		uint32_t texSlot, bool generateMipmaps = true, vk::Format format = vk::Format::eR8G8B8A8Srgb);

	void LoadSkybox(Render::Image& skybox, const std::string& directory);

	void LoadImages();

	void ResetFrame();

	void UpdateRTFrame();
};

} // namespace Render
