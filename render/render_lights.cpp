#include "render_lights.h"


DirectionalLightGPU Render::LightManager::MakeGPUDirectionalLight(const Plume::LightManager::Light& light)
{
	DirectionalLightGPU resLight = {};
	resLight.color = glm::vec4(light.color, light.intensity);
	resLight.direction = glm::vec4(light.direction, 0.0f);

	return resLight;
}


PointLightGPU Render::LightManager::MakeGPUPointLight(const Plume::LightManager::Light& light)
{
	PointLightGPU resLight = {};
	resLight.color = glm::vec4(light.color, light.intensity);
	resLight.position = glm::vec4(light.position, 1.0f);

	return resLight;
}


LightingData Render::LightManager::MakeLightingData(const std::array<Plume::LightManager::Light, Plume::LightManager::MAX_NUM_OF_LIGHTS>& lights)
{
	LightingData resLightData = {};
	
	for (const auto& light : lights) {
		int32_t curPointLightId = 0;

		switch (light.type)
		{
		case Plume::LightManager::LightType::eDirectional:
			resLightData.dirLight = MakeGPUDirectionalLight(light);
			break;
		case Plume::LightManager::LightType::eAmbient:
			resLightData.ambientLight = glm::vec4(light.color, light.intensity);
			break;
		case Plume::LightManager::LightType::ePoint:
			assert(curPointLightId < MAX_POINT_LIGHTS_PER_FRAME);

			resLightData.pointLights[curPointLightId] = MakeGPUPointLight(light);
			++curPointLightId;
			break;
		default:
			break;
		}
	}

	return resLightData;
}
