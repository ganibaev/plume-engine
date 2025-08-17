#pragma once

#include "render_core.h"
#include "../engine/plm_camera.h"


namespace Render
{


class PathTracing
{
public:
	void Init(const Plume::Camera* pCamera);
	void InitResources();

	void RenderFrame();

	void ResetFrame();

private:
	void InitDescriptors() const;
	void InitFrameContext();
	void InitGBuffer();
	void InitPass();

	void SwitchFrameImageLayout();

	void PrepareFrame();
	void RenderPass();
	void PrepareNextFrame();

	Render::Pass pass;

	struct FrameContext
	{
		Render::Image prevFrameImage;
		Render::Image ptPositionImage;
		Render::Image prevPositionImage;
	};

	FrameContext _frameCtx;

	RayPushConstants _rayConstants = {};

	const Plume::Camera* _pCamera;

	// effectively remove frame accumulation limit for path tracing, but reserve it for future use
	int _maxAccumFrames = 60000;
};


} // namespace Render
