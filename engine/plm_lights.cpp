#include "plm_lights.h"


void Plume::LightManager::Init()
{
	// Set up default lighting
	// TODO: Read static lighting from the scene description
	Light dirLight = {};
	dirLight.type = LightType::eDirectional;
	dirLight.direction = glm::vec3(glm::normalize(glm::vec3(0.0f, -30.0f, -10.0f)));
	dirLight.color = glm::vec3{ 253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f };
	dirLight.intensity = 1.0f;
	AddDirectionalLight(dirLight);

	Light ambientLight = {};
	ambientLight.type = LightType::eAmbient;
	ambientLight.color = glm::vec3{ 1.0f, 1.0f, 1.0f };
	ambientLight.intensity = 0.1f;
	AddAmbientLight(ambientLight);

	Light centralLight = {};
	centralLight.type = LightType::ePoint;
	centralLight.position = glm::vec3{ 2.8f, 20.0f, 17.5f };
	centralLight.color = glm::vec3{ 253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f };
	centralLight.intensity = 400.0f;
	AddPointLight(centralLight);

	Light frontLight = {};
	frontLight.type = LightType::ePoint;
	frontLight.position = glm::vec3{ 2.8f, 3.0f, -5.0f };
	frontLight.color = glm::vec3{ 1.0f, 1.0f, 1.0f };
	frontLight.intensity = 100.0f;
	AddPointLight(frontLight);

	Light backLight = {};
	backLight.type = LightType::ePoint;
	backLight.position = glm::vec3{ 2.8f, 3.0f, 28.0f };
	backLight.color = glm::vec3{ 1.0f, 1.0f, 1.0f };
	backLight.intensity = 100.0f;
	AddPointLight(backLight);
}


const Plume::LightManager::Light& Plume::LightManager::GetPointLight(int32_t lightId) const
{
	ASSERT(lightId >= 0 && lightId < MAX_NUM_OF_POINT_LIGHTS, "Light ID out of range");

	return _lights[MAX_NUM_OF_DIRECTIONAL_LIGHTS + MAX_NUM_OF_AMBIENT_LIGHTS + lightId];
}


void Plume::LightManager::AddDirectionalLight(Light& dirLight)
{
	ASSERT(dirLight.type == LightType::eDirectional, "Invalid light type");
	ASSERT(_numOfRegisteredDirectionalLights != MAX_NUM_OF_DIRECTIONAL_LIGHTS, "Can't add any more lights of this type");

	dirLight.id = DIRECTIONAL_LIGHT_ID;
	_lights[dirLight.id] = dirLight;
	++_numOfRegisteredDirectionalLights;
}


void Plume::LightManager::AddAmbientLight(Light& ambientLight)
{
	ASSERT(ambientLight.type == LightType::eAmbient, "Invalid light type");
	ASSERT(_numOfRegisteredAmbientLights != MAX_NUM_OF_AMBIENT_LIGHTS, "Can't add any more lights of this type");

	ambientLight.id = AMBIENT_LIGHT_ID;
	_lights[ambientLight.id] = ambientLight;
	++_numOfRegisteredAmbientLights;
}


void Plume::LightManager::AddPointLight(Light& pointLight)
{
	ASSERT(pointLight.type == LightType::ePoint, "Invalid light type");
	ASSERT(_numOfRegisteredPointLights != MAX_NUM_OF_POINT_LIGHTS, "Can't add any more lights of this type");

	pointLight.id = MAX_NUM_OF_DIRECTIONAL_LIGHTS + MAX_NUM_OF_AMBIENT_LIGHTS + _numOfRegisteredPointLights;
	_lights[pointLight.id] = pointLight;
	++_numOfRegisteredPointLights;
}
