#include "render_system.h"

#include "render_initializers.h"

#include "render_texture_utils.h"
#include "render_lights.h"
#include "render_rt_backend_utils.h"
#include "render_shader.h"

#include <unordered_map>

#include <glm/gtx/transform.hpp>


#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


void Render::System::init_backend_and_data(const Render::System::InitData& initData)
{
	_pCamera = initData.pCam;
	_pLightManager = initData.pLightManager;

	Render::Backend* backend = Render::Backend::AcquireInstance();
	
	backend->_pWindow = initData.pWindow;
	backend->_windowExtent = initData.windowExtent;
	
	backend->_windowExtent3D.width = backend->_windowExtent.width;
	backend->_windowExtent3D.height = backend->_windowExtent.height;
	backend->_windowExtent3D.depth = 1;

	backend->Init();
}


void Render::System::init(const Render::System::InitData& initData)
{
	init_backend_and_data(initData);

	Render::Backend* backend = Render::Backend::AcquireInstance();

	init_frame_context();

	init_gbuffer_images();

	init_path_tracing_gbuffer_images();

	load_meshes();

	load_images();

	init_descriptors();

	init_scene();

	init_blas();

	init_tlas();

	init_rt_descriptors();

	backend->AllocateDescriptorSets();

	init_passes();

	backend->UpdateDescriptorSets();

	// everything went well
	_isInitialized = true;
}


void Render::System::switch_intermediate_image_layout(bool beforeRendering)
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Image::TransitionInfo transitionInfo;

	if (beforeRendering)
	{
		transitionInfo.dstAccessMask = (_renderMode == RenderMode::eHybrid) ? vk::AccessFlagBits::eColorAttachmentWrite :
			vk::AccessFlagBits::eShaderWrite;
		transitionInfo.newLayout = (_renderMode == RenderMode::eHybrid) ? vk::ImageLayout::eColorAttachmentOptimal :
			vk::ImageLayout::eGeneral;
		transitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;


		if (_renderMode == RenderMode::eHybrid)
		{
			transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
		else
		{
			transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;
		}
	}
	else
	{
		transitionInfo.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		transitionInfo.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		transitionInfo.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eVertexShader;
	}

	backend->_intermediateImage.LayoutTransition(transitionInfo);
}


void Render::System::switch_swapchain_image_layout(uint32_t swapchainImageIndex, bool beforeRendering)
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Image::TransitionInfo transitionInfo;

	if (beforeRendering)
	{
		transitionInfo.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		transitionInfo.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		transitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	}
	else
	{
		transitionInfo.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		transitionInfo.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
		transitionInfo.newLayout = vk::ImageLayout::ePresentSrcKHR;
		transitionInfo.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
	}

	backend->_swapchainImages[swapchainImageIndex].LayoutTransition(transitionInfo);
}


void Render::System::switch_frame_image_layout(Render::Image& image)
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Image::TransitionInfo transitionInfo;

	transitionInfo.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
	transitionInfo.newLayout = vk::ImageLayout::eGeneral;
	transitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
	transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;

	image.LayoutTransition(transitionInfo);
}


void Render::System::init_frame_context()
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Image::CreateInfo createInfo;
	createInfo.format = backend->_frameBufferFormat;
	createInfo.usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	createInfo.type = Render::Image::Type::eRTXOutput;
	createInfo.extent = backend->_windowExtent3D;

	_frameCtx.prevFrameImage = backend->CreateImage(createInfo);
}


void Render::System::init_gbuffer_images()
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::Format positionFormat = vk::Format::eR32G32B32A32Sfloat;
	vk::Format normalFormat = vk::Format::eR32G32B32A32Sfloat;
	vk::Format albedoFormat = vk::Format::eR16G16B16A16Sfloat;
	vk::Format metallicRoughnessFormat = vk::Format::eR32G32B32A32Sfloat;

	Render::Image::CreateInfo positionImageInfo = {};

	positionImageInfo.format = positionFormat;
	positionImageInfo.aspectMask = vk::ImageAspectFlagBits::eColor;
	positionImageInfo.usageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;

	_gBufferImages[GBUFFER_POSITION_SLOT] = backend->CreateImage(positionImageInfo);

	Render::Image::CreateInfo normalImageInfo = positionImageInfo;
	normalImageInfo.format = normalFormat;

	_gBufferImages[GBUFFER_NORMAL_SLOT] = backend->CreateImage(normalImageInfo);

	Render::Image::CreateInfo albedoImageInfo = positionImageInfo;
	albedoImageInfo.format = albedoFormat;

	_gBufferImages[GBUFFER_ALBEDO_SLOT] = backend->CreateImage(albedoImageInfo);

	Render::Image::CreateInfo metallicRoughnessImageInfo = positionImageInfo;
	metallicRoughnessImageInfo.format = metallicRoughnessFormat;

	_gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT] = backend->CreateImage(metallicRoughnessImageInfo);

	// Depth image
	Render::Image::CreateInfo depthInfo = {};
	depthInfo.aspectMask = vk::ImageAspectFlagBits::eDepth;
	depthInfo.usageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment;
	depthInfo.format = backend->_depthFormat;

	_frameCtx.depthImage = backend->CreateImage(depthInfo);
}


void Render::System::init_descriptors()
{
	auto* backend = Render::Backend::AcquireInstance();

	const size_t camSceneParamBufferSize = FRAME_OVERLAP * backend->PadUniformBufferSize(sizeof(CameraDataGPU) + sizeof(LightingData));

	Render::Buffer::CreateInfo camSceneParamsInfo;
	camSceneParamsInfo.allocSize = camSceneParamBufferSize;
	camSceneParamsInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst;
	camSceneParamsInfo.memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	_frameCtx.camLightingBuffer = backend->CreateBuffer(camSceneParamsInfo);

	Render::DescriptorManager::BufferInfo camSceneBufferInfo;
	camSceneBufferInfo.buffer = _frameCtx.camLightingBuffer.GetHandle();
	camSceneBufferInfo.bufferType = vk::DescriptorType::eUniformBufferDynamic;
	camSceneBufferInfo.offset = 0;
	camSceneBufferInfo.range = sizeof(CameraDataGPU) + sizeof(LightingData);

	backend->RegisterBuffer(Render::RegisteredDescriptorSet::eGlobal, vk::ShaderStageFlagBits::eVertex |
		vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
		{ camSceneBufferInfo }, 0);

	std::vector<Render::DescriptorManager::BufferInfo> objectBufferInfos(FRAME_OVERLAP);

	auto gBufferSampler = backend->GetSampler(Render::SamplerType::eLinearClamp);

	std::vector<Render::DescriptorManager::ImageInfo> postprocessInfos(FRAME_OVERLAP);

	Render::DescriptorManager::ImageInfo gBufferImageInfo;
	gBufferImageInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	gBufferImageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	gBufferImageInfo.sampler = gBufferSampler;

	for (int i = 0; i < FRAME_OVERLAP; ++i)
	{
		constexpr int MAX_OBJECTS = 10000;
		Render::Buffer::CreateInfo objectBufferCreateInfo;
		objectBufferCreateInfo.allocSize = sizeof(ObjectData) * MAX_OBJECTS;
		objectBufferCreateInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		objectBufferCreateInfo.memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		backend->_frames[i]._objectBuffer = backend->CreateBuffer(objectBufferCreateInfo);

		objectBufferInfos[i].buffer = backend->_frames[i]._objectBuffer.GetHandle();
		objectBufferInfos[i].bufferType = vk::DescriptorType::eStorageBuffer;
		objectBufferInfos[i].offset = 0;
		objectBufferInfos[i].range = sizeof(ObjectData) * MAX_OBJECTS;

		postprocessInfos[i] = gBufferImageInfo;
		postprocessInfos[i].imageView = backend->_intermediateImage.GetView();
	}

	Render::DescriptorManager::ImageInfo positionInfo;
	Render::DescriptorManager::ImageInfo normalInfo;
	Render::DescriptorManager::ImageInfo albedoInfo;
	Render::DescriptorManager::ImageInfo metallicRoughnessInfo;

	positionInfo = gBufferImageInfo;
	positionInfo.imageView = _gBufferImages[GBUFFER_POSITION_SLOT].GetView();

	normalInfo = gBufferImageInfo;
	normalInfo.imageView = _gBufferImages[GBUFFER_NORMAL_SLOT].GetView();

	albedoInfo = gBufferImageInfo;
	albedoInfo.imageView = _gBufferImages[GBUFFER_ALBEDO_SLOT].GetView();

	metallicRoughnessInfo = gBufferImageInfo;
	metallicRoughnessInfo.imageView = _gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT].GetView();

	backend->RegisterBuffer(Render::RegisteredDescriptorSet::eObjects, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR, objectBufferInfos, 0, 1, true);

	backend->RegisterImage(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { positionInfo },
		GBUFFER_POSITION_SLOT);
	backend->RegisterImage(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { normalInfo },
		GBUFFER_NORMAL_SLOT);
	backend->RegisterImage(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { albedoInfo },
		GBUFFER_ALBEDO_SLOT);
	backend->RegisterImage(Render::RegisteredDescriptorSet::eGBuffer, vk::ShaderStageFlagBits::eFragment, { metallicRoughnessInfo },
		GBUFFER_METALLIC_ROUGHNESS_SLOT);

	backend->RegisterImage(Render::RegisteredDescriptorSet::ePostprocess, vk::ShaderStageFlagBits::eVertex |
		vk::ShaderStageFlagBits::eFragment, postprocessInfos, 0, 1, false, true);

}


void Render::System::init_geometry_pass()
{
	Render::Pass::InitInfo geometryPassInfo = {};

	geometryPassInfo.usedDescSets = Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eObjects |
		Render::DescriptorSetFlagBits::eDiffuseTextures | Render::DescriptorSetFlagBits::eMetallicTextures |
		Render::DescriptorSetFlagBits::eRoughnessTextures | Render::DescriptorSetFlagBits::eNormalMapTextures | Render::DescriptorSetFlagBits::eTLAS;
	geometryPassInfo.cullMode = vk::CullModeFlagBits::eNone;

	std::vector<Render::Pass::AttachmentStateInfo> gBufferAttachmentInfos(NUM_GBUFFER_ATTACHMENTS);
	gBufferAttachmentInfos[GBUFFER_POSITION_SLOT].pImage = &_gBufferImages[GBUFFER_POSITION_SLOT];
	gBufferAttachmentInfos[GBUFFER_NORMAL_SLOT].pImage = &_gBufferImages[GBUFFER_NORMAL_SLOT];
	gBufferAttachmentInfos[GBUFFER_ALBEDO_SLOT].pImage = &_gBufferImages[GBUFFER_ALBEDO_SLOT];
	gBufferAttachmentInfos[GBUFFER_METALLIC_ROUGHNESS_SLOT].pImage = &_gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT];

	geometryPassInfo.pColorAttachmentInfos = &gBufferAttachmentInfos;

	Render::Pass::AttachmentStateInfo depthAttachment = {};
	depthAttachment.pImage = &_frameCtx.depthImage;

	geometryPassInfo.pDepthAttachment = &depthAttachment;

	std::vector<std::string> geometryPassShaders = {
		"geometry_pass.vert", "geometry_pass.frag"
	};

	geometryPassInfo.pShaderNames = &geometryPassShaders;

	geometryPassInfo.useVertexAttributes = true;

	auto geometryPassId = static_cast<size_t>(RenderPassType::eGeometryPass);
	_renderPasses[geometryPassId].Init(geometryPassInfo);
}


void Render::System::init_lighting_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Pass::InitInfo lightingPassInfo = {};

	lightingPassInfo.usedDescSets = Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eGBuffer |
		Render::DescriptorSetFlagBits::eTLAS;

	std::vector<Render::Pass::AttachmentStateInfo> lightingPassAttachmentInfos(1);
	lightingPassAttachmentInfos[0].pImage = &backend->_intermediateImage;

	lightingPassInfo.pColorAttachmentInfos = &lightingPassAttachmentInfos;

	Render::Image::CreateInfo lightingDepthInfo = {};
	lightingDepthInfo.aspectMask = vk::ImageAspectFlagBits::eDepth;
	lightingDepthInfo.usageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment;
	lightingDepthInfo.format = backend->_depthFormat;

	Render::Image lightingDepthImage = backend->CreateImage(lightingDepthInfo);

	Render::Pass::AttachmentStateInfo depthAttachment = {};
	depthAttachment.pImage = &lightingDepthImage;

	lightingPassInfo.pDepthAttachment = &depthAttachment;

	std::vector<std::string> lightingPassShaders = {
		"lighting_pass.vert", "lighting_pass.frag"
	};

	lightingPassInfo.pShaderNames = &lightingPassShaders;

	auto lightingPassId = static_cast<size_t>(RenderPassType::eLightingPass);
	_renderPasses[lightingPassId].Init(lightingPassInfo);
}


void Render::System::init_postprocess_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Pass::InitInfo postprocessPassInfo = {};

	postprocessPassInfo.usedDescSets = Render::DescriptorSetFlagBits::ePostprocess;

	std::vector<Render::Pass::AttachmentStateInfo> postprocessPassAttachmentInfos(1);
	postprocessPassAttachmentInfos[0].isSwapchainImage = true;

	Render::Image::CreateInfo postprocessDepthInfo = {};
	postprocessDepthInfo.aspectMask = vk::ImageAspectFlagBits::eDepth;
	postprocessDepthInfo.usageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment;
	postprocessDepthInfo.format = backend->_depthFormat;

	Render::Image postprocessDepthImage = backend->CreateImage(postprocessDepthInfo);

	postprocessPassInfo.pColorAttachmentInfos = &postprocessPassAttachmentInfos;

	Render::Pass::AttachmentStateInfo depthAttachment = {};
	depthAttachment.pImage = &postprocessDepthImage;

	postprocessPassInfo.pDepthAttachment = &depthAttachment;

	std::vector<std::string> postprocessPassShaders(2);
	postprocessPassShaders[0] = "postprocess.vert";
	if (_renderMode == RenderMode::eHybrid)
	{
		postprocessPassShaders[1] = "fxaa.frag";
	}
	else
	{
		postprocessPassShaders[1] = "morrone_denoiser.frag";
	}

	postprocessPassInfo.pShaderNames = &postprocessPassShaders;

	Render::Pass::PushConstantsInitInfo pcInitInfo;
	pcInitInfo.pcBufferSize = sizeof(static_cast<int32_t>(backend->_renderCfg.FXAA));
	pcInitInfo.stageFlags = vk::ShaderStageFlagBits::eFragment;

	postprocessPassInfo.pcInitInfo = pcInitInfo;

	auto postprocessPassId = static_cast<size_t>(RenderPassType::ePostprocess);
	_renderPasses[postprocessPassId].Init(postprocessPassInfo);
}


void Render::System::init_sky_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Pass::InitInfo skyboxPassInfo = {};

	skyboxPassInfo.usedDescSets = Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eSkyboxTextures;

	std::vector<Render::Pass::AttachmentStateInfo> skyboxPassAttachmentInfos(1);
	skyboxPassAttachmentInfos[0].pImage = &backend->_intermediateImage;
	skyboxPassAttachmentInfos[0].loadOp = vk::AttachmentLoadOp::eLoad;

	skyboxPassInfo.pColorAttachmentInfos = &skyboxPassAttachmentInfos;

	Render::Pass::AttachmentStateInfo depthAttachment = {};
	depthAttachment.pImage = &_frameCtx.depthImage;
	depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
	skyboxPassInfo.pDepthAttachment = &depthAttachment;

	std::vector<std::string> skyboxPassShaders = {
		"skybox.vert", "skybox.frag"
	};

	skyboxPassInfo.pShaderNames = &skyboxPassShaders;

	Render::Pass::PushConstantsInitInfo pcInitInfo;
	pcInitInfo.pcBufferSize = sizeof(MeshPushConstants);
	pcInitInfo.stageFlags = vk::ShaderStageFlagBits::eVertex;

	skyboxPassInfo.pcInitInfo = pcInitInfo;

	skyboxPassInfo.useVertexAttributes = true;

	auto skyboxPassId = static_cast<size_t>(RenderPassType::eSky);
	_renderPasses[skyboxPassId].Init(skyboxPassInfo);
}


void Render::System::init_path_tracing_gbuffer_images()
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::Format positionFormat = vk::Format::eR32G32B32A32Sfloat;

	Render::Image::CreateInfo ptPosInfo;
	ptPosInfo.aspectMask = vk::ImageAspectFlagBits::eColor;
	ptPosInfo.extent = backend->_windowExtent3D;
	ptPosInfo.format = positionFormat;
	ptPosInfo.usageFlags = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;

	_frameCtx.ptPositionImage = backend->CreateImage(ptPosInfo);

	auto gBufferSampler = backend->GetSampler(Render::SamplerType::eLinearClamp);

	std::vector<Render::DescriptorManager::ImageInfo> ptPositionInfos(FRAME_OVERLAP);
	for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		ptPositionInfos[i].imageType = vk::DescriptorType::eStorageImage;
		ptPositionInfos[i].imageView = _frameCtx.ptPositionImage.GetView();
		ptPositionInfos[i].layout = vk::ImageLayout::eGeneral;
		ptPositionInfos[i].sampler = gBufferSampler;
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR,
		ptPositionInfos, 2, 1, false, true);


	Render::Image::CreateInfo prevPosInfo = ptPosInfo;
	prevPosInfo.usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

	_frameCtx.prevPositionImage = backend->CreateImage(prevPosInfo);

	std::vector<Render::DescriptorManager::ImageInfo> prevPosInfos(FRAME_OVERLAP);
	for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		prevPosInfos[i].imageType = vk::DescriptorType::eCombinedImageSampler;
		prevPosInfos[i].imageView = _frameCtx.prevPositionImage.GetView();
		prevPosInfos[i].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
		prevPosInfos[i].sampler = gBufferSampler;
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR,
		prevPosInfos, 3, 1, false, true);
}


void Render::System::init_path_tracing_pass()
{
	Render::Pass::RTInitInfo pathTracingPassInfo = {};

	pathTracingPassInfo.usedDescSets = Render::DescriptorSetFlagBits::eRTXGeneral | Render::DescriptorSetFlagBits::eRTXPerFrame |
		Render::DescriptorSetFlagBits::eGlobal | Render::DescriptorSetFlagBits::eObjects | Render::DescriptorSetFlagBits::eDiffuseTextures |
		Render::DescriptorSetFlagBits::eMetallicTextures | Render::DescriptorSetFlagBits::eRoughnessTextures |
		Render::DescriptorSetFlagBits::eNormalMapTextures | Render::DescriptorSetFlagBits::eSkyboxTextures;

	std::vector<std::string> ptPassShaders = {
		"path_tracing.rgen", "path_tracing.rmiss", "trace_shadow.rmiss", "path_tracing.rchit", "path_tracing.rahit"
	};

	pathTracingPassInfo.pShaderNames = &ptPassShaders;

	Render::Pass::PushConstantsInitInfo pcInitInfo;
	pcInitInfo.pcBufferSize = sizeof(RayPushConstants);
	pcInitInfo.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

	pathTracingPassInfo.pcInitInfo = pcInitInfo;

	auto ptPassId = static_cast<size_t>(RenderPassType::ePathTracing);
	_renderPasses[ptPassId].InitRT(pathTracingPassInfo);
}


void Render::System::init_passes()
{
	init_geometry_pass();

	init_lighting_pass();

	init_postprocess_pass();

	init_sky_pass();

	init_path_tracing_pass();
}


void Render::System::load_meshes()
{
	Model suzanneModel;
	suzanneModel._parentScene = &_scene;
	suzanneModel.load_assimp("../../../assets/suzanne/Suzanne.gltf");

	for (Mesh& mesh : suzanneModel._meshes)
	{
		upload_mesh(mesh);
	}

	_scene._models["suzanne"] = suzanneModel;

	Model sponza;
	sponza._parentScene = &_scene;
	sponza.load_assimp("../../../assets/sponza/Sponza.gltf");

	for (Mesh& mesh : sponza._meshes)
	{
		upload_mesh(mesh);
	}

	_scene._models["sponza"] = sponza;

	Model cube;
	cube._parentScene = &_scene;
	cube.load_assimp("../../../assets/cube.gltf");

	for (Mesh& mesh : cube._meshes)
	{
		upload_mesh(mesh);
	}

	_scene._models["cube"] = cube;
}


void Render::System::init_blas()
{
	auto* backend = Render::Backend::AcquireInstance();

	std::vector<Render::Backend::BLASInput> blasInputs;

	vk::GeometryFlagBitsKHR blasGeometryFlags = (_renderMode == RenderMode::ePathTracing) ?
		vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation : vk::GeometryFlagBitsKHR::eOpaque;

	for (size_t i = 0; i < _renderables.size() - 1; ++i)
	{
		blasInputs.emplace_back(backend->ConvertMeshToBlasInput(*_renderables[i].mesh, blasGeometryFlags));
	}

	RenderBackendRTUtils::buildBlas(this, blasInputs);
}


void Render::System::init_tlas()
{
	std::vector<vk::AccelerationStructureInstanceKHR> tlas;
	tlas.reserve(_renderables.size() - 1);

	for (uint32_t i = 0; i < _renderables.size() - 1; ++i)
	{
		vk::AccelerationStructureInstanceKHR accelInst;
		accelInst.setTransform(RenderBackendRTUtils::convertToTransformKHR(_renderables[i].transformMatrix));
		accelInst.setInstanceCustomIndex(i);
		accelInst.setAccelerationStructureReference(RenderBackendRTUtils::getBlasDeviceAddress(this, i));
		accelInst.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
		accelInst.setMask(0xFF);

		tlas.emplace_back(accelInst);
	}

	RenderBackendRTUtils::buildTlas(this, tlas);

	auto* backend = Render::Backend::AcquireInstance();

	backend->RegisterAccelerationStructure(Render::RegisteredDescriptorSet::eTLAS, vk::ShaderStageFlagBits::eFragment,
		_topLevelAS._structure, 0);

	backend->RegisterAccelerationStructure(Render::RegisteredDescriptorSet::eRTXGeneral, vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eClosestHitKHR, _topLevelAS._structure, 0);
}


void Render::System::init_rt_descriptors()
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::Sampler frameSampler = backend->GetSampler(Render::SamplerType::eLinearClamp);

	std::vector<Render::DescriptorManager::ImageInfo> outImageInfos(FRAME_OVERLAP);
	std::vector<Render::DescriptorManager::ImageInfo> prevFrameInfos(FRAME_OVERLAP);

	for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
	{
		Render::DescriptorManager::ImageInfo fullFrameImage;
		fullFrameImage.imageType = vk::DescriptorType::eCombinedImageSampler;
		fullFrameImage.layout = vk::ImageLayout::eGeneral;
		fullFrameImage.sampler = frameSampler;

		outImageInfos[i] = fullFrameImage;
		outImageInfos[i].imageType = vk::DescriptorType::eStorageImage;
		outImageInfos[i].imageView = backend->_intermediateImage.GetView();

		prevFrameInfos[i] = fullFrameImage;
		prevFrameInfos[i].layout = vk::ImageLayout::eGeneral;
		prevFrameInfos[i].imageView = _frameCtx.prevFrameImage.GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR, outImageInfos, 0,
		1, false, true);

	backend->RegisterImage(Render::RegisteredDescriptorSet::eRTXPerFrame, vk::ShaderStageFlagBits::eRaygenKHR, prevFrameInfos, 1,
		1, false, true);
}


bool Render::System::load_material_texture(Render::Image& tex, const std::string& texName, const std::string& matName,
	uint32_t texSlot, bool generateMipmaps/* = true */, vk::Format format/* = vk::Format::eR8G8B8A8Srgb */)
{
	if (texName.empty() || !RenderUtil::load_image_from_file(this, texName, tex, generateMipmaps, format))
	{
		return false;
	}

	_loadedTextures[matName][texSlot] = tex;

	return true;
}


void Render::System::load_skybox(Render::Image& skybox, const std::string& directory)
{
	std::vector<std::string> files = {
		"px.png", "nx.png",
		"py.png", "ny.png",
		"pz.png", "nz.png"
	};

	for (size_t i = 0; i < files.size(); ++i)
	{
		files[i] = directory + files[i];
	}

	ASSERT(RenderUtil::load_cubemap_from_files(this, files, skybox), "Failed to load skybox");
}


void Render::System::load_images()
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::Sampler smoothSampler = backend->GetSampler(Render::SamplerType::eLinearRepeat);

	Render::Image defaultTex = {};

	RenderUtil::load_image_from_file(this, "../../../assets/null-texture.png", defaultTex);

	Render::DescriptorManager::ImageInfo texInfo;
	texInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	texInfo.sampler = smoothSampler;
	texInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

	std::vector<Render::DescriptorManager::ImageInfo> diffuseInfos(_scene._diffuseTexNames.size());

	for (size_t i = 0; i < _scene._diffuseTexNames.size(); ++i)
	{
		Render::Image diffuse = {};

		if (!_scene._diffuseTexNames[i].empty())
		{
			bool loadRes = load_material_texture(diffuse, _scene._diffuseTexNames[i], _scene._matNames[i], DIFFUSE_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
		}
		
		diffuseInfos[i] = texInfo;
		diffuseInfos[i].imageView = _loadedTextures[_scene._matNames[i]][DIFFUSE_TEX_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eDiffuseTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eAnyHitKHR, diffuseInfos, 0, static_cast<uint32_t>(_scene._diffuseTexNames.size()),
		true);


	std::vector<Render::DescriptorManager::ImageInfo> metallicInfos(_scene._metallicTexNames.size());
	for (size_t i = 0; i < _scene._metallicTexNames.size(); ++i)
	{
		Render::Image metallic = {};

		if (!_scene._metallicTexNames[i].empty())
		{
			bool loadRes = load_material_texture(metallic, _scene._metallicTexNames[i], _scene._matNames[i], METALLIC_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
		}

		metallicInfos[i] = texInfo;
		metallicInfos[i].imageView = _loadedTextures[_scene._matNames[i]][METALLIC_TEX_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eMetallicTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR,
		metallicInfos, 0, static_cast<uint32_t>(_scene._metallicTexNames.size()), true);


	std::vector<Render::DescriptorManager::ImageInfo> roughnessInfos(_scene._roughnessTexNames.size());
	for (size_t i = 0; i < _scene._roughnessTexNames.size(); ++i)
	{
		Render::Image roughness = {};

		if (!_scene._roughnessTexNames[i].empty())
		{
			bool loadRes = load_material_texture(roughness, _scene._roughnessTexNames[i], _scene._matNames[i], ROUGHNESS_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
		}

		roughnessInfos[i] = texInfo;
		roughnessInfos[i].imageView = _loadedTextures[_scene._matNames[i]][ROUGHNESS_TEX_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eRoughnessTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR, roughnessInfos, 0,
		static_cast<uint32_t>(_scene._roughnessTexNames.size()), true);


	Render::Image defaultNormal = {};

	RenderUtil::load_image_from_file(this, "../../../assets/null-normal.png", defaultNormal, true,
		vk::Format::eR32G32B32A32Sfloat);

	std::vector<Render::DescriptorManager::ImageInfo> normalMapInfos(_scene._normalMapNames.size());
	for (size_t i = 0; i < _scene._normalMapNames.size(); ++i)
	{
		Render::Image normalMap = {};

		bool loadRes = load_material_texture(normalMap, _scene._normalMapNames[i], _scene._matNames[i], NORMAL_MAP_SLOT,
			true, vk::Format::eR32G32B32A32Sfloat);

		if (!loadRes)
		{
			_loadedTextures[_scene._matNames[i]][NORMAL_MAP_SLOT] = defaultNormal;
		}
		else
		{
			_loadedTextures[_scene._matNames[i]][NORMAL_MAP_SLOT] = normalMap;
		}

		normalMapInfos[i] = texInfo;
		normalMapInfos[i].imageView = _loadedTextures[_scene._matNames[i]][NORMAL_MAP_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eNormalMapTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eClosestHitKHR |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR, normalMapInfos,
		0, static_cast<uint32_t>(_scene._normalMapNames.size()), true);


	load_skybox(_skybox, "../../../assets/skybox/");

	Render::DescriptorManager::ImageInfo skyboxInfo;
	skyboxInfo.imageView = _skybox.GetView();
	skyboxInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	skyboxInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	skyboxInfo.sampler = smoothSampler;

	backend->RegisterImage(Render::RegisteredDescriptorSet::eSkyboxTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eMissKHR, { skyboxInfo }, 0);
}


void Render::System::reset_frame()
{
	_rayConstants.frame = -1;
}


void Render::System::update_rt_frame()
{
	ASSERT(_pCamera != nullptr, "Invalid camera");

	auto* backend = Render::Backend::AcquireInstance();

	const Plume::Camera& camera = *_pCamera;

	static glm::mat4 refCamMatrix = {};
	static float refFov = camera._zoom;
	static glm::vec3 refPosition{ 0.0f };

	const glm::mat4 m = camera.get_view_matrix();
	const float fov = camera._zoom;
	const glm::vec3 position = camera._position;

	if (std::memcmp(&refCamMatrix[0][0], &m[0][0], sizeof(glm::mat4)) != 0 || refFov != fov ||
		std::memcmp(&refPosition[0], &position[0], sizeof(glm::vec3)) != 0)
	{
		refCamMatrix = m;
		refFov = fov;
		refPosition = position;
		if (!backend->_renderCfg.MOTION_VECTORS)
		{
			reset_frame();
		}
	}
	++_rayConstants.frame;
}


void Render::System::init_scene()
{
	RenderObject suzanne = {};
	suzanne.model = get_model("suzanne");
	suzanne.mesh = &(suzanne.model->_meshes.front());
	glm::mat4 meshTranslate = glm::translate(glm::mat4{ 1.0f }, glm::vec3(2.8f, -8.0f, 0));
	suzanne.transformMatrix = meshTranslate;

	_renderables.push_back(suzanne);

	Model* pSceneModel = get_model("sponza");

	ASSERT(pSceneModel != nullptr, "Can't load scene model");

	glm::mat4 sceneTransform = glm::translate(glm::vec3{ 5, -10, 0 }) * glm::rotate(glm::radians(90.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4{ 1.0f }, glm::vec3(0.05f, 0.05f, 0.05f));

	for (Mesh& mesh : pSceneModel->_meshes)
	{
		RenderObject renderSceneMesh = {};
		renderSceneMesh.model = pSceneModel;
		renderSceneMesh.mesh = &mesh;
		renderSceneMesh.transformMatrix = sceneTransform;

		_renderables.push_back(std::move(renderSceneMesh));
	}

	_skyboxObject.model = get_model("cube");
	_skyboxObject.mesh = &(_skyboxObject.model->_meshes.front());
	glm::mat4 meshScale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(600.0f, 600.0f, 600.0f));
	_skyboxObject.transformMatrix = meshScale;
}


void Render::System::upload_mesh(Mesh& mesh)
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::BufferUsageFlags vertexBufferUsage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
		vk::BufferUsageFlagBits::eStorageBuffer;

	Render::Buffer::CreateInfo vertexBufferInfo = {};
	vertexBufferInfo.usage = vertexBufferUsage;
	vertexBufferInfo.allocSize = mesh._vertices.size() * sizeof(Vertex);
	vertexBufferInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	mesh._vertexBuffer = backend->CreateBuffer(vertexBufferInfo);

	backend->UploadBufferImmediately(mesh._vertexBuffer, mesh._vertices);


	vk::BufferUsageFlags indexBufferUsage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst |
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
		vk::BufferUsageFlagBits::eStorageBuffer;

	Render::Buffer::CreateInfo indexBufferInfo = {};
	indexBufferInfo.usage = indexBufferUsage;
	indexBufferInfo.allocSize = mesh._indices.size() * sizeof(uint32_t);
	indexBufferInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	mesh._indexBuffer = backend->CreateBuffer(indexBufferInfo);

	backend->UploadBufferImmediately(mesh._indexBuffer, mesh._indices);
}


void Render::System::cleanup()
{
	if (!_isInitialized)
	{
		return;
	}

	auto* backend = Render::Backend::AcquireInstance();

	backend->Terminate();

	_isInitialized = false;
}


void Render::System::render_frame()
{
	auto* backend = Render::Backend::AcquireInstance();

	backend->BeginFrameRendering();

	switch_intermediate_image_layout(true);

	if (_renderMode == RenderMode::ePathTracing)
	{
		switch_frame_image_layout(_frameCtx.prevFrameImage);
	}
	
	// ========================================   RENDERING   ========================================

	upload_cam_scene_data(_renderables.data(), _renderables.size());

	if (_renderMode == RenderMode::eHybrid)
	{
		gbuffer_geometry_pass();

		Render::Image::TransitionInfo gbufferTransitionInfo = {};
		gbufferTransitionInfo.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		gbufferTransitionInfo.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		gbufferTransitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		gbufferTransitionInfo.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;

		_gBufferImages[GBUFFER_POSITION_SLOT].LayoutTransition(gbufferTransitionInfo);
		_gBufferImages[GBUFFER_NORMAL_SLOT].LayoutTransition(gbufferTransitionInfo);
		_gBufferImages[GBUFFER_ALBEDO_SLOT].LayoutTransition(gbufferTransitionInfo);
		_gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT].LayoutTransition(gbufferTransitionInfo);

		gbuffer_lighting_pass();

		sky_pass();

		switch_intermediate_image_layout(false);

		switch_swapchain_image_layout(backend->_swapchainImageIndex, true);

		fxaa_pass();
	}
	else if (_renderMode == RenderMode::ePathTracing)
	{
		Render::Image::TransitionInfo preRenderPosTransition = {};
		preRenderPosTransition.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		preRenderPosTransition.newLayout = vk::ImageLayout::eGeneral;
		preRenderPosTransition.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		preRenderPosTransition.dstStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;

		_frameCtx.ptPositionImage.LayoutTransition(preRenderPosTransition);

		Render::Image::TransitionInfo preRenderPrevPosTransition = preRenderPosTransition;
		preRenderPrevPosTransition.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		_frameCtx.prevPositionImage.LayoutTransition(preRenderPrevPosTransition);

		path_tracing_pass();


		Render::Image::TransitionInfo copySrcTransition = {};
		copySrcTransition.srcAccessMask = vk::AccessFlagBits::eShaderRead;
		copySrcTransition.oldLayout = vk::ImageLayout::eGeneral;
		copySrcTransition.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		copySrcTransition.srcStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;
		copySrcTransition.dstStageMask = vk::PipelineStageFlagBits::eVertexShader;

		_frameCtx.ptPositionImage.LayoutTransition(copySrcTransition);

		Render::Image::TransitionInfo copySrcIntermediateImageTransition = copySrcTransition;
		copySrcIntermediateImageTransition.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		copySrcIntermediateImageTransition.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

		backend->_intermediateImage.LayoutTransition(copySrcIntermediateImageTransition);


		Render::Image::TransitionInfo copyFrameDstTransition = {};
		copyFrameDstTransition.srcAccessMask = vk::AccessFlagBits::eShaderRead;
		copyFrameDstTransition.oldLayout = vk::ImageLayout::eGeneral;
		copyFrameDstTransition.newLayout = vk::ImageLayout::eTransferDstOptimal;
		copyFrameDstTransition.srcStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;
		copyFrameDstTransition.dstStageMask = vk::PipelineStageFlagBits::eVertexShader;

		_frameCtx.prevFrameImage.LayoutTransition(copyFrameDstTransition);

		Render::Image::TransitionInfo copyPosDstTransition = copyFrameDstTransition;
		copyPosDstTransition.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		_frameCtx.prevPositionImage.LayoutTransition(copyPosDstTransition);


		backend->CopyImage(backend->_intermediateImage, _frameCtx.prevFrameImage);
		backend->CopyImage(_frameCtx.ptPositionImage, _frameCtx.prevPositionImage);

		switch_intermediate_image_layout(false);

		switch_swapchain_image_layout(backend->_swapchainImageIndex, true);

		denoiser_pass();
	}

	if (_showDebugUi)
	{
		debug_ui_pass(backend->GetCurrentCommandBuffer(), backend->_swapchainImages[backend->_swapchainImageIndex].GetView());
	}

	// ======================================== END RENDERING ========================================

	// transfer swapchain image to presentable layout

	switch_swapchain_image_layout(backend->_swapchainImageIndex, false);

	_prevCamera = *_pCamera;

	backend->EndFrameRendering();
	// display image
	// wait on render semaphore so that rendered image is complete 

	backend->Present();
}


vk::DeviceSize Render::System::align_up(vk::DeviceSize originalSize, vk::DeviceSize alignment)
{
	vk::DeviceSize alignedSize = originalSize;
	if (alignment > 0)
	{
		alignedSize = (alignedSize + alignment - 1) & ~(alignment - 1);
	}
	return alignedSize;
}


void Render::System::copy_image(vk::CommandBuffer cmd, vk::ImageAspectFlags aspectMask, vk::Image srcImage,
	vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::Extent3D extent)
{
	vk::ImageCopy copyInfo;
	copyInfo.srcSubresource.aspectMask = aspectMask;
	copyInfo.srcSubresource.layerCount = 1;
	copyInfo.srcSubresource.mipLevel = 0;
	copyInfo.srcSubresource.baseArrayLayer = 0;
	copyInfo.dstSubresource = copyInfo.srcSubresource;
	copyInfo.extent = extent;
	copyInfo.srcOffset = { { 0, 0, 0 } };
	copyInfo.dstOffset = { { 0, 0, 0 } };

	cmd.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, copyInfo);
}


Model* Render::System::get_model(const std::string& name)
{
	auto it = _scene._models.find(name);
	if (it == _scene._models.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}


void Render::System::upload_cam_scene_data(RenderObject* first, size_t count)
{
	ASSERT(_pCamera != nullptr, "Invalid camera");
	ASSERT(_pLightManager != nullptr, "Invalid light manager");

	auto* backend = Render::Backend::AcquireInstance();

	struct CamLightingData
	{
		CameraDataGPU camData;
		LightingData lightingData;
	} camLightingData = {};

	const Plume::Camera& camera = *_pCamera;

	camLightingData.camData = camera.make_gpu_camera_data(_prevCamera, { backend->_windowExtent.width, backend->_windowExtent.height });
	camLightingData.lightingData = Render::LightManager::make_lighting_data(_pLightManager->GetLights());

	size_t camLightingDataBufferSize = backend->PadUniformBufferSize(sizeof(CamLightingData));

	int32_t frameIndex = backend->_frameId % FRAME_OVERLAP;

	backend->CopyDataToBuffer(&camLightingData, camLightingDataBufferSize, _frameCtx.camLightingBuffer, camLightingDataBufferSize * frameIndex);


	std::vector<ObjectData> objectSsboVector = {};
	objectSsboVector.resize(count);

	for (int32_t i = 0; i < count; ++i)
	{
		RenderObject& object = first[i];
		objectSsboVector[i].model = object.transformMatrix;
		if (object.mesh)
		{
			uint64_t indexAddress = object.mesh->_indexBuffer.GetDeviceAddress();
			uint64_t vertexAddress = object.mesh->_vertexBuffer.GetDeviceAddress();
			objectSsboVector[i].matIndex = object.mesh->_matIndex;
			objectSsboVector[i].indexBufferAddress = indexAddress;
			objectSsboVector[i].vertexBufferAddress = vertexAddress;
			objectSsboVector[i].emittance = object.mesh->_emittance;
		}
	}

	backend->CopyDataToBuffer(objectSsboVector.data(), objectSsboVector.size() * sizeof(ObjectData), backend->GetCurrentFrameData()._objectBuffer);
}


void Render::System::gbuffer_geometry_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	auto geometryPassId = static_cast<int32_t>(RenderPassType::eGeometryPass);
	backend->DrawObjects(_renderables, _renderPasses[geometryPassId], nullptr, true);
}


void Render::System::gbuffer_lighting_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	auto lightingPassId = static_cast<int32_t>(RenderPassType::eLightingPass);
	backend->DrawScreenQuad(_renderPasses[lightingPassId], nullptr, true);
}


void Render::System::sky_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	std::vector<RenderObject> skyObj = {
		_skyboxObject
	};

	MeshPushConstants constants = {};
	constants.render_matrix = _skyboxObject.transformMatrix;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &constants;
	pcInfo.size = sizeof(MeshPushConstants);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eVertex;

	auto skyPassId = static_cast<int32_t>(RenderPassType::eSky);
	backend->DrawObjects(skyObj, _renderPasses[skyPassId], &pcInfo, true);
}


void Render::System::fxaa_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	int32_t fxaaOn = backend->_renderCfg.FXAA;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &fxaaOn;
	pcInfo.size = sizeof(fxaaOn);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eFragment;

	auto postprocessPassId = static_cast<int32_t>(RenderPassType::ePostprocess);

	_renderPasses[postprocessPassId].SetSwapchainImage(backend->_swapchainImages[backend->_swapchainImageIndex]);

	backend->DrawScreenQuad(_renderPasses[postprocessPassId], &pcInfo);
}


void Render::System::denoiser_pass()
{
	auto* backend = Render::Backend::AcquireInstance();

	int32_t denoisingOn = backend->_renderCfg.DENOISING;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &denoisingOn;
	pcInfo.size = sizeof(denoisingOn);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eFragment;

	auto postprocessPassId = static_cast<int32_t>(RenderPassType::ePostprocess);

	_renderPasses[postprocessPassId].SetSwapchainImage(backend->_swapchainImages[backend->_swapchainImageIndex]);

	backend->DrawScreenQuad(_renderPasses[postprocessPassId], &pcInfo);
}


void Render::System::debug_ui_pass(vk::CommandBuffer cmd, vk::ImageView targetImageView)
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::RenderingAttachmentInfo uiAttachmentInfo = vkinit::rendering_attachment_info(targetImageView,
		vk::ImageLayout::eColorAttachmentOptimal, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, {});

	vk::RenderingInfo uiRenderInfo;
	uiRenderInfo.renderArea.extent = backend->_windowExtent;
	uiRenderInfo.layerCount = 1;
	uiRenderInfo.setColorAttachments(uiAttachmentInfo);

	cmd.beginRendering(uiRenderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	cmd.endRendering();
}


void Render::System::path_tracing_pass()
{
	update_rt_frame();

	if (_rayConstants.frame >= _maxAccumFrames)
	{
		return;
	}

	auto* backend = Render::Backend::AcquireInstance();

	_rayConstants.MAX_BOUNCES = backend->_renderCfg.MAX_BOUNCES;
	_rayConstants.USE_MOTION_VECTORS = backend->_renderCfg.MOTION_VECTORS;
	_rayConstants.USE_SHADER_EXECUTION_REORDERING = backend->_renderCfg.SHADER_EXECUTION_REORDERING;
	_rayConstants.USE_TEMPORAL_ACCUMULATION = backend->_renderCfg.TEMPORAL_ACCUMULATION;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &_rayConstants;
	pcInfo.size = sizeof(_rayConstants);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eRaygenKHR;

	auto pathTracingPassId = static_cast<int32_t>(RenderPassType::ePathTracing);
	backend->TraceRays(_renderPasses[pathTracingPassId], &pcInfo, true);
}


void Render::System::setup_debug_ui_frame()
{
	if (!_showDebugUi)
	{
		return;
	}

	auto* backend = Render::Backend::AcquireInstance();

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowSize(ImVec2(300, 300));
	ImGui::Begin("Options");
	if (_renderMode == RenderMode::ePathTracing)
	{
		ImGui::SliderInt("Max Bounces", &backend->_renderCfg.MAX_BOUNCES, 0, 30);
		ImGui::Checkbox("Use Denoising", &backend->_renderCfg.DENOISING);
		ImGui::Checkbox("Use Temporal Accumulation", &backend->_renderCfg.TEMPORAL_ACCUMULATION);
		bool prevMv = backend->_renderCfg.MOTION_VECTORS;
		ImGui::Checkbox("Use Motion Vectors", &backend->_renderCfg.MOTION_VECTORS);
		if (backend->_renderCfg.MOTION_VECTORS != prevMv)
		{
			reset_frame();
		}
		ImGui::Checkbox("Use Shader Execution Reordering", &backend->_renderCfg.SHADER_EXECUTION_REORDERING);
	}
	else if (_renderMode == RenderMode::eHybrid)
	{
		ImGui::Checkbox("Use FXAA", &backend->_renderCfg.FXAA);
	}
	ImGui::End();

	ImGui::Render();
}
