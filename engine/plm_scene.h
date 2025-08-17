#pragma once

#include "plm_common.h"
#include "../render/shaders/host_device_common.h"

#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


namespace Plume
{

struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	int32_t matIndex = -1;
	glm::vec3 emittance{ 0.0f };
};


struct Scene;

struct Model
{
	Scene* pParentScene = nullptr;

	std::vector<Mesh> meshes;
	glm::mat4 transformMatrix = glm::identity<glm::mat4>();

	bool LoadAssimp(std::string filePath);

	void ProcessNode(aiNode* node, const aiScene& scene);
	void ProcessMesh(aiMesh* mesh, const aiScene& scene);

	void LoadTextureNames(aiMaterial* mat, aiTextureType type, std::vector<std::string>& names, std::string* curName = nullptr) const;
};


struct Scene
{
	void DefaultInit();
	const Plume::Model* GetPModel(const std::string& modelName) const;

	size_t matOffset = 0;

	std::unordered_map<std::string, Model> models;

	std::vector<std::string> matNames;
	std::vector<std::string> diffuseTexNames;
	std::vector<std::string> metallicTexNames;
	std::vector<std::string> roughnessTexNames;
	std::vector<std::string> normalMapNames;

	std::string directory;
};

} // namespace Plume
