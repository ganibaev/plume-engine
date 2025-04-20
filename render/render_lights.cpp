#include "render_lights.h"


DirectionalLightGPU Render::LightManager::make_gpu_directional_light(const PlumeLightManager::Light& light)
{
	DirectionalLightGPU resLight = {};
	resLight.color = glm::vec4(light.color, light.intensity);
	resLight.direction = glm::vec4(light.direction, 0.0f);

	return resLight;
}


PointLightGPU Render::LightManager::make_gpu_point_light(const PlumeLightManager::Light& light)
{
	PointLightGPU resLight = {};
	resLight.color = glm::vec4(light.color, light.intensity);
	resLight.position = glm::vec4(light.position, 1.0f);

	return resLight;
}


LightingData Render::LightManager::make_lighting_data(const std::array<PlumeLightManager::Light, PlumeLightManager::MAX_NUM_OF_LIGHTS>& lights)
{
	LightingData resLightData = {};
	
	for (const auto& light : lights) {
		int32_t curPointLightId = 0;

		switch (light.type)
		{
		case PlumeLightManager::LightType::eDirectional:
			resLightData.dirLight = make_gpu_directional_light(light);
			break;
		case PlumeLightManager::LightType::eAmbient:
			resLightData.ambientLight = glm::vec4(light.color, light.intensity);
			break;
		case PlumeLightManager::LightType::ePoint:
			assert(curPointLightId < MAX_POINT_LIGHTS_PER_FRAME);

			resLightData.pointLights[curPointLightId] = make_gpu_point_light(light);
			++curPointLightId;
			break;
		default:
			break;
		}
	}

	return resLightData;
}
