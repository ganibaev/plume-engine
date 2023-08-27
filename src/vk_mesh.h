#pragma once

#include "vk_types.h"
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

struct VertexInputDescription
{
	std::vector<vk::VertexInputBindingDescription> bindings;
	std::vector<vk::VertexInputAttributeDescription> attributes;

	vk::PipelineVertexInputStateCreateFlags flags;
};

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;
	glm::uint materialID;
	static VertexInputDescription get_vertex_description();
};

struct Mesh
{
	std::vector<Vertex> _vertices;
	AllocatedBuffer _vertexBuffer;
	std::vector<uint32_t> _indices;
	AllocatedBuffer _indexBuffer;
};

struct Model
{
	std::vector<Mesh> _meshes;

	std::vector<std::string> _matNames;
	std::vector<std::string> _ambientTexNames;
	std::vector<std::string> _diffuseTexNames;
	std::vector<std::string> _specularTexNames;

	std::string _directory;

	bool load_assimp(std::string filePath);
	bool load_from_obj(std::string filePath);

	void process_node(aiNode* node, const aiScene& scene);
	void process_mesh(aiMesh* mesh, const aiScene& scene);

	void load_texture_names(aiMaterial* mat, aiTextureType type, std::vector<std::string>& names, std::string* curName = nullptr);
};