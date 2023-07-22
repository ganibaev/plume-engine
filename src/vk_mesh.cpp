#include "vk_mesh.h"

#include "tiny_obj_loader.h"
#include <iostream>

VertexInputDescription Vertex::get_vertex_description()
{
	VertexInputDescription description;

	// 1 vertex buffer binding, per-vertex rate
	vk::VertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = vk::VertexInputRate::eVertex;
	
	description.bindings.push_back(mainBinding);

	// store position at location 0
	vk::VertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = vk::Format::eR32G32B32Sfloat;
	positionAttribute.offset = offsetof(Vertex, position);
	
	// normals at location 1
	vk::VertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = vk::Format::eR32G32B32Sfloat;
	normalAttribute.offset = offsetof(Vertex, normal);

	// colors at location 2
	vk::VertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = vk::Format::eR32G32B32Sfloat;
	colorAttribute.offset = offsetof(Vertex, color);

	// UV at location 3
	vk::VertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = vk::Format::eR32G32Sfloat;
	uvAttribute.offset = offsetof(Vertex, uv);

	// material ID at location 4
	vk::VertexInputAttributeDescription materialIDAttribute = {};
	materialIDAttribute.binding = 0;
	materialIDAttribute.location = 4;
	materialIDAttribute.format = vk::Format::eR32Uint;
	materialIDAttribute.offset = offsetof(Vertex, materialID);
	
	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);
	description.attributes.push_back(materialIDAttribute);
	return description;
}

bool Mesh::load_from_obj(const char* filePath)
{
	std::string matBaseDir = "../../assets/";

	tinyobj::attrib_t vertexAttributes;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warn;
	std::string err;

	tinyobj::LoadObj(&vertexAttributes, &shapes, &materials, &warn, &err, filePath, matBaseDir.data());

	if (!warn.empty())
	{
		std::cout << "WARNING: " << warn << std::endl;
	}

	if (!err.empty())
	{
		std::cout << "ERROR: " << err << std::endl;
		return false;
	}

	_diffuseTexNames.resize(materials.size());
	_ambientTexNames.resize(materials.size());
	_specularTexNames.resize(materials.size());
	_matNames.resize(materials.size());

	for (size_t m = 0; m < materials.size(); ++m)
	{
		_matNames[m] = materials[m].name;

		if (!materials[m].diffuse_texname.empty())
		{
			_diffuseTexNames[m] = matBaseDir + materials[m].diffuse_texname;
		}

		if (!materials[m].ambient_texname.empty())
		{
			_ambientTexNames[m] = matBaseDir + materials[m].ambient_texname;
		}

		if (!materials[m].specular_texname.empty())
		{
			_specularTexNames[m] = matBaseDir + materials[m].specular_texname;
		}
	}

	for (size_t s = 0; s < shapes.size(); ++s)
	{
		size_t indexOffset = 0;
		constexpr int verticesPerFace = 3;

		_vertices.reserve(_vertices.size() + shapes[s].mesh.num_face_vertices.size() * verticesPerFace);

		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); ++f)
		{
			for (size_t v = 0; v < verticesPerFace; ++v)
			{
				// access the vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[indexOffset + v];

				// positions
				tinyobj::real_t vx = vertexAttributes.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = vertexAttributes.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = vertexAttributes.vertices[3 * idx.vertex_index + 2];

				// normals
				tinyobj::real_t nx = vertexAttributes.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = vertexAttributes.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = vertexAttributes.normals[3 * idx.normal_index + 2];

				// copy into a Vertex
				Vertex newVertex;
				newVertex.position.x = vx;
				newVertex.position.y = vy;
				newVertex.position.z = vz;

				newVertex.normal.x = nx;
				newVertex.normal.y = ny;
				newVertex.normal.z = nz;

				// we will basically draw a normal buffer by default
				newVertex.color = newVertex.normal;

				// vertex uv
				tinyobj::real_t ux = vertexAttributes.texcoords.empty() ? 0 : vertexAttributes.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t uy = vertexAttributes.texcoords.empty() ? 0 : vertexAttributes.texcoords[2 * idx.texcoord_index + 1];

				newVertex.uv.x = ux;
				newVertex.uv.y = 1 - uy; // Vulkan convention

				newVertex.materialID = shapes[s].mesh.material_ids[f];

				_vertices.push_back(newVertex);
			}
			indexOffset += verticesPerFace;
		}
	}

	return true;
}