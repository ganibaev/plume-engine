#include "render_system.h"

#include "render_initializers.h"

#include "render_texture_utils.h"
#include "render_lights.h"
#include "render_rt_backend_utils.h"
#include "render_shader.h"

#include <unordered_map>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


void Render::System::InitBackendAndData(const Render::System::InitData& initData)
{
	_pCamera = initData.pCam;
	_pLightManager = initData.pLightManager;
	_pScene = initData.pScene;

	Render::Backend* backend = Render::Backend::AcquireInstance();
	
	backend->_pWindow = initData.pWindow;
	backend->_windowExtent = initData.windowExtent;
	
	backend->_windowExtent3D.width = backend->_windowExtent.width;
	backend->_windowExtent3D.height = backend->_windowExtent.height;
	backend->_windowExtent3D.depth = 1;

	backend->Init();
}


void Render::System::Init(const Render::System::InitData& initData)
{
	InitBackendAndData(initData);

	Render::Backend* backend = Render::Backend::AcquireInstance();

	_pathTracingManager.InitResources();

	InitGBufferImages();

	LoadImages();

	InitDescriptors();

	InitRenderScene();

	InitBLAS();

	InitTLAS();

	backend->AllocateDescriptorSets();

	InitPasses();

	backend->UpdateDescriptorSets();

	// everything went well
	_isInitialized = true;
}


void Render::System::SwitchIntermediateImageLayout(bool beforeRendering)
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


void Render::System::SwitchSwapchainImageLayout(uint32_t swapchainImageIndex, bool beforeRendering)
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


void Render::System::InitGBufferImages()
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


void Render::System::InitDescriptors()
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


void Render::System::InitGeometryPass()
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

	auto geometryPassId = static_cast<size_t>(Render::Pass::Type::eGeometryPass);
	_renderPasses[geometryPassId].Init(geometryPassInfo);
}


void Render::System::InitLightingPass()
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

	auto lightingPassId = static_cast<size_t>(Render::Pass::Type::eLightingPass);
	_renderPasses[lightingPassId].Init(lightingPassInfo);
}


void Render::System::InitPostprocessPass()
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

	auto postprocessPassId = static_cast<size_t>(Render::Pass::Type::ePostprocess);
	_renderPasses[postprocessPassId].Init(postprocessPassInfo);
}


void Render::System::InitSkyPass()
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

	auto skyboxPassId = static_cast<size_t>(Render::Pass::Type::eSky);
	_renderPasses[skyboxPassId].Init(skyboxPassInfo);
}


void Render::System::InitPasses()
{
	InitGeometryPass();

	InitLightingPass();

	InitPostprocessPass();

	InitSkyPass();

	_pathTracingManager.Init(_pCamera);
}


void Render::System::InitBLAS()
{
	auto* backend = Render::Backend::AcquireInstance();

	std::vector<Render::Backend::BLASInput> blasInputs;

	vk::GeometryFlagBitsKHR blasGeometryFlags = (_renderMode == RenderMode::ePathTracing) ?
		vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation : vk::GeometryFlagBitsKHR::eOpaque;

	for (size_t i = 0; i < _renderables.size() - 1; ++i)
	{
		blasInputs.emplace_back(backend->ConvertMeshToBlasInput(_renderables[i].mesh, blasGeometryFlags));
	}

	RenderBackendRTUtils::BuildBLAS(this, blasInputs);
}


void Render::System::InitTLAS()
{
	std::vector<vk::AccelerationStructureInstanceKHR> tlas;
	tlas.reserve(_renderables.size() - 1);

	for (uint32_t i = 0; i < _renderables.size() - 1; ++i)
	{
		vk::AccelerationStructureInstanceKHR accelInst;
		accelInst.setTransform(RenderBackendRTUtils::ConvertToTransformKHR(_renderables[i].transformMatrix));
		accelInst.setInstanceCustomIndex(i);
		accelInst.setAccelerationStructureReference(RenderBackendRTUtils::GetBLASDeviceAddress(this, i));
		accelInst.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
		accelInst.setMask(0xFF);

		tlas.emplace_back(accelInst);
	}

	RenderBackendRTUtils::BuildTLAS(this, tlas);

	auto* backend = Render::Backend::AcquireInstance();

	backend->RegisterAccelerationStructure(Render::RegisteredDescriptorSet::eTLAS, vk::ShaderStageFlagBits::eFragment,
		_topLevelAS._structure, 0);

	backend->RegisterAccelerationStructure(Render::RegisteredDescriptorSet::eRTXGeneral, vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eClosestHitKHR, _topLevelAS._structure, 0);
}


bool Render::System::LoadMaterialTexture(Render::Image& tex, const std::string& texName, const std::string& matName,
	uint32_t texSlot, bool generateMipmaps/* = true */, vk::Format format/* = vk::Format::eR8G8B8A8Srgb */)
{
	if (texName.empty() || !RenderUtil::LoadImageFromFile(this, texName, tex, generateMipmaps, format))
	{
		return false;
	}

	_loadedTextures[matName][texSlot] = tex;

	return true;
}


void Render::System::LoadSkybox(Render::Image& skybox, const std::string& directory)
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

	ASSERT(RenderUtil::LoadCubemapFromFiles(this, files, skybox), "Failed to load skybox");
}


void Render::System::LoadImages()
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::Sampler smoothSampler = backend->GetSampler(Render::SamplerType::eLinearRepeatAnisotropic);

	Render::Image defaultTex = {};

	RenderUtil::LoadImageFromFile(this, "../../../assets/null-texture.png", defaultTex);

	Render::DescriptorManager::ImageInfo texInfo;
	texInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	texInfo.sampler = smoothSampler;
	texInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

	ASSERT(_pScene != nullptr, "Invalid scene");
	const Plume::Scene& scene = *_pScene;

	std::vector<Render::DescriptorManager::ImageInfo> diffuseInfos(scene.diffuseTexNames.size());

	for (size_t i = 0; i < scene.diffuseTexNames.size(); ++i)
	{
		Render::Image diffuse = {};

		if (!scene.diffuseTexNames[i].empty())
		{
			bool loadRes = LoadMaterialTexture(diffuse, scene.diffuseTexNames[i], scene.matNames[i], DIFFUSE_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[scene.matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[scene.matNames[i]][DIFFUSE_TEX_SLOT] = defaultTex;
		}
		
		diffuseInfos[i] = texInfo;
		diffuseInfos[i].imageView = _loadedTextures[scene.matNames[i]][DIFFUSE_TEX_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eDiffuseTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eAnyHitKHR, diffuseInfos, 0, static_cast<uint32_t>(scene.diffuseTexNames.size()),
		true);


	std::vector<Render::DescriptorManager::ImageInfo> metallicInfos(scene.metallicTexNames.size());
	for (size_t i = 0; i < scene.metallicTexNames.size(); ++i)
	{
		Render::Image metallic = {};

		if (!scene.metallicTexNames[i].empty())
		{
			bool loadRes = LoadMaterialTexture(metallic, scene.metallicTexNames[i], scene.matNames[i], METALLIC_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[scene.matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[scene.matNames[i]][METALLIC_TEX_SLOT] = defaultTex;
		}

		metallicInfos[i] = texInfo;
		metallicInfos[i].imageView = _loadedTextures[scene.matNames[i]][METALLIC_TEX_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eMetallicTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR,
		metallicInfos, 0, static_cast<uint32_t>(scene.metallicTexNames.size()), true);


	std::vector<Render::DescriptorManager::ImageInfo> roughnessInfos(scene.roughnessTexNames.size());
	for (size_t i = 0; i < scene.roughnessTexNames.size(); ++i)
	{
		Render::Image roughness = {};

		if (!scene.roughnessTexNames[i].empty())
		{
			bool loadRes = LoadMaterialTexture(roughness, scene.roughnessTexNames[i], scene.matNames[i], ROUGHNESS_TEX_SLOT);

			if (!loadRes)
			{
				_loadedTextures[scene.matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
			}
		}
		else
		{
			_loadedTextures[scene.matNames[i]][ROUGHNESS_TEX_SLOT] = defaultTex;
		}

		roughnessInfos[i] = texInfo;
		roughnessInfos[i].imageView = _loadedTextures[scene.matNames[i]][ROUGHNESS_TEX_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eRoughnessTextures, vk::ShaderStageFlagBits::eFragment |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR, roughnessInfos, 0,
		static_cast<uint32_t>(scene.roughnessTexNames.size()), true);


	Render::Image defaultNormal = {};

	RenderUtil::LoadImageFromFile(this, "../../../assets/null-normal.png", defaultNormal, true,
		vk::Format::eR32G32B32A32Sfloat);

	std::vector<Render::DescriptorManager::ImageInfo> normalMapInfos(scene.normalMapNames.size());
	for (size_t i = 0; i < scene.normalMapNames.size(); ++i)
	{
		Render::Image normalMap = {};

		bool loadRes = LoadMaterialTexture(normalMap, scene.normalMapNames[i], scene.matNames[i], NORMAL_MAP_SLOT,
			true, vk::Format::eR32G32B32A32Sfloat);

		if (!loadRes)
		{
			_loadedTextures[scene.matNames[i]][NORMAL_MAP_SLOT] = defaultNormal;
		}
		else
		{
			_loadedTextures[scene.matNames[i]][NORMAL_MAP_SLOT] = normalMap;
		}

		normalMapInfos[i] = texInfo;
		normalMapInfos[i].imageView = _loadedTextures[scene.matNames[i]][NORMAL_MAP_SLOT].GetView();
	}

	backend->RegisterImage(Render::RegisteredDescriptorSet::eNormalMapTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eClosestHitKHR |
		vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eAnyHitKHR, normalMapInfos,
		0, static_cast<uint32_t>(scene.normalMapNames.size()), true);


	LoadSkybox(_skybox, "../../../assets/skybox/");

	Render::DescriptorManager::ImageInfo skyboxInfo;
	skyboxInfo.imageView = _skybox.GetView();
	skyboxInfo.imageType = vk::DescriptorType::eCombinedImageSampler;
	skyboxInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	skyboxInfo.sampler = smoothSampler;

	backend->RegisterImage(Render::RegisteredDescriptorSet::eSkyboxTextures, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eRaygenKHR |
		vk::ShaderStageFlagBits::eMissKHR, { skyboxInfo }, 0);
}


void Render::System::InitRenderScene()
{
	ASSERT(_pScene != nullptr, "Invalid scene");

	auto* backend = Render::Backend::AcquireInstance();

	const auto& models = _pScene->models;

	for (const auto& [name, model] : models)
	{
		for (const Plume::Mesh& mesh : model.meshes)
		{
			Render::Object object = {};
			object.model = &model;
			object.transformMatrix = model.transformMatrix;
			object.mesh = backend->UploadMesh(mesh);

			if (name != "skybox")
			{
				_renderables.push_back(std::move(object));
			}
			else
			{
				_skyboxObject = std::move(object);
			}
		}
	}
}


void Render::System::Cleanup()
{
	if (!_isInitialized)
	{
		return;
	}

	auto* backend = Render::Backend::AcquireInstance();

	backend->Terminate();

	_isInitialized = false;
}


void Render::System::RenderFrame()
{
	auto* backend = Render::Backend::AcquireInstance();

	backend->BeginFrameRendering();

	SwitchIntermediateImageLayout(true);
	
	// ========================================   RENDERING   ========================================

	UploadCamSceneData(_renderables.data(), _renderables.size());

	if (_renderMode == RenderMode::eHybrid)
	{
		GBufferGeometryPass();

		Render::Image::TransitionInfo gbufferTransitionInfo = {};
		gbufferTransitionInfo.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		gbufferTransitionInfo.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		gbufferTransitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		gbufferTransitionInfo.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;

		_gBufferImages[GBUFFER_POSITION_SLOT].LayoutTransition(gbufferTransitionInfo);
		_gBufferImages[GBUFFER_NORMAL_SLOT].LayoutTransition(gbufferTransitionInfo);
		_gBufferImages[GBUFFER_ALBEDO_SLOT].LayoutTransition(gbufferTransitionInfo);
		_gBufferImages[GBUFFER_METALLIC_ROUGHNESS_SLOT].LayoutTransition(gbufferTransitionInfo);

		GBufferLightingPass();

		SkyPass();

		SwitchIntermediateImageLayout(false);

		SwitchSwapchainImageLayout(backend->_swapchainImageIndex, true);

		FXAAPass();
	}
	else if (_renderMode == RenderMode::ePathTracing)
	{
		_pathTracingManager.RenderFrame();

		SwitchIntermediateImageLayout(false);

		SwitchSwapchainImageLayout(backend->_swapchainImageIndex, true);

		DenoiserPass();
	}

	if (_showDebugUi)
	{
		DebugUIPass(backend->GetCurrentCommandBuffer(), backend->_swapchainImages[backend->_swapchainImageIndex].GetView());
	}

	// ======================================== END RENDERING ========================================

	// transfer swapchain image to presentable layout

	SwitchSwapchainImageLayout(backend->_swapchainImageIndex, false);

	_prevCamera = *_pCamera;

	backend->EndFrameRendering();
	// display image
	// wait on render semaphore so that rendered image is complete 

	backend->Present();
}


vk::DeviceSize Render::System::AlignUp(vk::DeviceSize originalSize, vk::DeviceSize alignment)
{
	vk::DeviceSize alignedSize = originalSize;
	if (alignment > 0)
	{
		alignedSize = (alignedSize + alignment - 1) & ~(alignment - 1);
	}
	return alignedSize;
}


void Render::System::CopyImage(vk::CommandBuffer cmd, vk::ImageAspectFlags aspectMask, vk::Image srcImage,
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

void Render::System::UploadCamSceneData(Render::Object* first, size_t count)
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

	camLightingData.camData = camera.MakeGPUCameraData(_prevCamera, { backend->_windowExtent.width, backend->_windowExtent.height });
	camLightingData.lightingData = Render::LightManager::MakeLightingData(_pLightManager->GetLights());

	size_t camLightingDataBufferSize = backend->PadUniformBufferSize(sizeof(CamLightingData));

	int32_t frameIndex = backend->_frameId % FRAME_OVERLAP;

	backend->CopyDataToBuffer(&camLightingData, camLightingDataBufferSize, _frameCtx.camLightingBuffer, camLightingDataBufferSize * frameIndex);


	std::vector<ObjectData> objectSsboVector = {};
	objectSsboVector.resize(count);

	for (int32_t i = 0; i < count; ++i)
	{
		Render::Object& object = first[i];
		objectSsboVector[i].model = object.transformMatrix;
		if (object.mesh.pEngineMesh)
		{
			uint64_t indexAddress = object.mesh.indexBuffer.GetDeviceAddress();
			uint64_t vertexAddress = object.mesh.vertexBuffer.GetDeviceAddress();
			objectSsboVector[i].matIndex = object.mesh.pEngineMesh->matIndex;
			objectSsboVector[i].indexBufferAddress = indexAddress;
			objectSsboVector[i].vertexBufferAddress = vertexAddress;
			objectSsboVector[i].emittance = object.mesh.pEngineMesh->emittance;
		}
	}

	backend->CopyDataToBuffer(objectSsboVector.data(), objectSsboVector.size() * sizeof(ObjectData), backend->GetCurrentFrameData()._objectBuffer);
}


void Render::System::GBufferGeometryPass()
{
	auto* backend = Render::Backend::AcquireInstance();

	auto geometryPassId = static_cast<int32_t>(Render::Pass::Type::eGeometryPass);
	backend->DrawObjects(_renderables, _renderPasses[geometryPassId], nullptr, true);
}


void Render::System::GBufferLightingPass()
{
	auto* backend = Render::Backend::AcquireInstance();

	auto lightingPassId = static_cast<int32_t>(Render::Pass::Type::eLightingPass);
	backend->DrawScreenQuad(_renderPasses[lightingPassId], nullptr, true);
}


void Render::System::SkyPass()
{
	auto* backend = Render::Backend::AcquireInstance();

	std::vector<Render::Object> skyObj = {
		_skyboxObject
	};

	MeshPushConstants constants = {};
	constants.render_matrix = _skyboxObject.transformMatrix;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &constants;
	pcInfo.size = sizeof(MeshPushConstants);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eVertex;

	auto skyPassId = static_cast<int32_t>(Render::Pass::Type::eSky);
	backend->DrawObjects(skyObj, _renderPasses[skyPassId], &pcInfo, true);
}


void Render::System::FXAAPass()
{
	auto* backend = Render::Backend::AcquireInstance();

	int32_t fxaaOn = backend->_renderCfg.FXAA;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &fxaaOn;
	pcInfo.size = sizeof(fxaaOn);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eFragment;

	auto postprocessPassId = static_cast<int32_t>(Render::Pass::Type::ePostprocess);

	_renderPasses[postprocessPassId].SetSwapchainImage(backend->_swapchainImages[backend->_swapchainImageIndex]);

	backend->DrawScreenQuad(_renderPasses[postprocessPassId], &pcInfo);
}


void Render::System::DenoiserPass()
{
	auto* backend = Render::Backend::AcquireInstance();

	int32_t denoisingOn = backend->_renderCfg.DENOISING;

	Render::Backend::PushConstantsInfo pcInfo = {};
	pcInfo.pData = &denoisingOn;
	pcInfo.size = sizeof(denoisingOn);
	pcInfo.shaderStages = vk::ShaderStageFlagBits::eFragment;

	auto postprocessPassId = static_cast<int32_t>(Render::Pass::Type::ePostprocess);

	_renderPasses[postprocessPassId].SetSwapchainImage(backend->_swapchainImages[backend->_swapchainImageIndex]);

	backend->DrawScreenQuad(_renderPasses[postprocessPassId], &pcInfo);
}


void Render::System::DebugUIPass(vk::CommandBuffer cmd, vk::ImageView targetImageView)
{
	auto* backend = Render::Backend::AcquireInstance();

	vk::RenderingAttachmentInfo uiAttachmentInfo = vkinit::RenderAttachmentInfo(targetImageView,
		vk::ImageLayout::eColorAttachmentOptimal, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, {});

	vk::RenderingInfo uiRenderInfo;
	uiRenderInfo.renderArea.extent = backend->_windowExtent;
	uiRenderInfo.layerCount = 1;
	uiRenderInfo.setColorAttachments(uiAttachmentInfo);

	cmd.beginRendering(uiRenderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	cmd.endRendering();
}


void Render::System::SetupDebugUIFrame()
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
			_pathTracingManager.ResetFrame();
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
