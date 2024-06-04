#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

namespace vknrc
{
	struct DiffuseTrainingData
	{
		glm::vec3 position{ 0.0f };
		glm::vec2 normal{ 0.0f };
		glm::vec3 diffuseAlbedo{ 0.0f };
		bool wasVisible = false;
		glm::vec3 prevRadiance{ 0.0f };
	};

	struct SpecularTrainingData
	{
		glm::vec3 position{ 0.0f };
		glm::vec2 direction{ 0.0f };
		glm::vec2 normal{ 0.0f };
		float roughness = 1.0f;
		glm::vec3 specularReflectance{ 0.0f };
	};

	struct ClassicTrainingData
	{
		glm::vec3 position{ 0.0f };
		glm::vec2 direction{ 0.0f };
		glm::vec2 normal{ 0.0f };
		float roughness = 1.0f;
		glm::vec3 diffuseAlbedo{ 0.0f };
		glm::vec3 specularReflectance{ 0.0f };
	};
}