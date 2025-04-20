#pragma once

#include "glm/glm.hpp"
#include <array>


class PlumeLightManager
{
public:
	static constexpr int32_t MAX_NUM_OF_DIRECTIONAL_LIGHTS = 1;
	static constexpr int32_t MAX_NUM_OF_AMBIENT_LIGHTS = 1;
	static constexpr int32_t MAX_NUM_OF_POINT_LIGHTS = 5;
	static constexpr int32_t MAX_NUM_OF_LIGHTS = MAX_NUM_OF_DIRECTIONAL_LIGHTS + MAX_NUM_OF_POINT_LIGHTS;

	static constexpr int32_t DIRECTIONAL_LIGHT_ID = 0;
	static constexpr int32_t AMBIENT_LIGHT_ID = 1;

	enum class LightType
	{
		eNone = 0,
		eDirectional = 1,
		eAmbient = 2,
		ePoint = 3,
		eMaxEnum
	};

	struct Light
	{
		glm::vec3 direction;
		float intensity = 0.0f;

		glm::vec3 color = glm::vec3(1.0f);
		LightType type = LightType::eNone;

		glm::vec3 position;
		int32_t id = -1;
	};

	void Init();

	const Light& GetDirectionalLight() const { return _lights[DIRECTIONAL_LIGHT_ID]; }
	const Light& GetAmbientLight() const { return _lights[AMBIENT_LIGHT_ID]; }
	const Light& GetPointLight(int32_t lightId) const;

	void AddDirectionalLight(Light& dirLight);
	void AddAmbientLight(Light& ambientLight);
	void AddPointLight(Light& pointLight);

	const std::array<Light, MAX_NUM_OF_LIGHTS>& GetLights() const { return _lights; };

private:
	int32_t _numOfRegisteredDirectionalLights = 0;
	int32_t _numOfRegisteredAmbientLights = 0;
	int32_t _numOfRegisteredPointLights = 0;

	std::array<Light, MAX_NUM_OF_LIGHTS> _lights;
};
