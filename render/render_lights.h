#pragma once

#include "shaders/host_device_common.h"
#include "../engine/plm_lights.h"


namespace Render
{

// Currently this is just a collection of static functions, can be extended in the future
class LightManager
{
public:
	static DirectionalLightGPU make_gpu_directional_light(const PlumeLightManager::Light& light);
	static PointLightGPU make_gpu_point_light(const PlumeLightManager::Light& light);
	static LightingData make_lighting_data(const std::array<PlumeLightManager::Light, PlumeLightManager::MAX_NUM_OF_LIGHTS>& lights);
};

} // namespace Render
