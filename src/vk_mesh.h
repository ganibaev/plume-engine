#pragma once

#include "vk_types.h"
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <string>

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
	
	std::vector<std::string> _matNames;
	std::vector<std::string> _diffuseTexNames;

	bool load_from_obj(const char* filePath);
};