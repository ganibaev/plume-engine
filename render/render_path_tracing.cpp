#include "render_path_tracing.h"


void Render::PathTracing::Init(const Plume::Camera* pCamera)
{
	_pCamera = pCamera;

	InitPass();
}


void Render::PathTracing::InitResources()
{
	InitFrameContext();
	InitGBuffer();
	InitDescriptors();
}


void Render::PathTracing::RenderFrame()
{
	PrepareFrame();

	RenderPass();

	PrepareNextFrame();
}


void Render::PathTracing::ResetFrame()
{
	_rayConstants.frame = -1;
}


void Render::PathTracing::InitDescriptors() const
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


void Render::PathTracing::InitFrameContext()
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Image::CreateInfo createInfo;
	createInfo.format = backend->_frameBufferFormat;
	createInfo.usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	createInfo.type = Render::Image::Type::eRTXOutput;
	createInfo.extent = backend->_windowExtent3D;

	_frameCtx.prevFrameImage = backend->CreateImage(createInfo);
}


void Render::PathTracing::InitGBuffer()
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


void Render::PathTracing::InitPass()
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

	pass.InitRT(pathTracingPassInfo);
}


void Render::PathTracing::SwitchFrameImageLayout()
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Image::TransitionInfo transitionInfo;

	transitionInfo.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
	transitionInfo.newLayout = vk::ImageLayout::eGeneral;
	transitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
	transitionInfo.dstStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;

	_frameCtx.prevFrameImage.LayoutTransition(transitionInfo);
}


void Render::PathTracing::PrepareFrame()
{
	SwitchFrameImageLayout();

	Render::Image::TransitionInfo preRenderPosTransition = {};
	preRenderPosTransition.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	preRenderPosTransition.newLayout = vk::ImageLayout::eGeneral;
	preRenderPosTransition.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
	preRenderPosTransition.dstStageMask = vk::PipelineStageFlagBits::eRayTracingShaderKHR;

	_frameCtx.ptPositionImage.LayoutTransition(preRenderPosTransition);

	Render::Image::TransitionInfo preRenderPrevPosTransition = preRenderPosTransition;
	preRenderPrevPosTransition.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	_frameCtx.prevPositionImage.LayoutTransition(preRenderPrevPosTransition);


	ASSERT(_pCamera != nullptr, "Invalid camera");

	auto* backend = Render::Backend::AcquireInstance();

	const Plume::Camera& camera = *_pCamera;

	static glm::mat4 refCamMatrix = {};
	static float refFov = camera._zoom;
	static glm::vec3 refPosition{ 0.0f };

	const glm::mat4 m = camera.GetViewMatrix();
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
			ResetFrame();
		}
	}
	++_rayConstants.frame;
}


void Render::PathTracing::RenderPass()
{
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

	backend->TraceRays(pass, &pcInfo, true);
}


void Render::PathTracing::PrepareNextFrame()
{
	auto* backend = Render::Backend::AcquireInstance();

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

	backend->CopyImage(backend->_intermediateImage, _frameCtx.prevFrameImage);

	Render::Image::TransitionInfo copyPosDstTransition = copyFrameDstTransition;
	copyPosDstTransition.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	_frameCtx.prevPositionImage.LayoutTransition(copyPosDstTransition);

	backend->CopyImage(_frameCtx.ptPositionImage, _frameCtx.prevPositionImage);
}
