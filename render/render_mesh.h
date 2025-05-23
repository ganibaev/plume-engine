#pragma once

#include "render_types.h"
#include <vector>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <../render/shaders/host_device_common.h>

struct VertexInputDescription
{
	std::vector<vk::VertexInputBindingDescription> bindings;
	std::vector<vk::VertexInputAttributeDescription> attributes;

	vk::PipelineVertexInputStateCreateFlags flags;

	void construct_from_vertex();
};

struct Mesh
{
	std::vector<Vertex> _vertices;
	AllocatedBuffer _vertexBuffer;
	std::vector<uint32_t> _indices;
	AllocatedBuffer _indexBuffer;
	int32_t _matIndex = -1;
	glm::vec3 _emittance{ 0.0f };
};

struct Scene;

struct Model
{
	Scene* _parentScene = nullptr;

	std::vector<Mesh> _meshes;

	bool load_assimp(std::string filePath);

	void process_node(aiNode* node, const aiScene& scene);
	void process_mesh(aiMesh* mesh, const aiScene& scene);

	void load_texture_names(aiMaterial* mat, aiTextureType type, std::vector<std::string>& names, std::string* curName = nullptr);
};

struct Scene
{
	size_t _matOffset = 0;

	std::unordered_map<std::string, Model> _models;

	std::vector<std::string> _matNames;
	std::vector<std::string> _diffuseTexNames;
	std::vector<std::string> _metallicTexNames;
	std::vector<std::string> _roughnessTexNames;
	std::vector<std::string> _normalMapNames;

	std::string _directory;
};