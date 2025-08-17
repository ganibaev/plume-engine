#pragma once

#include "shaders/host_device_common.h"
#include "../engine/plm_lights.h"


namespace Render
{

// Currently this is just a collection of static functions, can be extended in the future
class LightManager
{
public:
	static DirectionalLightGPU MakeGPUDirectionalLight(const Plume::LightManager::Light& light);
	static PointLightGPU MakeGPUPointLight(const Plume::LightManager::Light& light);
	static LightingData MakeLightingData(const std::array<Plume::LightManager::Light, Plume::LightManager::MAX_NUM_OF_LIGHTS>& lights);
};

} // namespace Render
