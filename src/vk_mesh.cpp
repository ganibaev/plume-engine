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

	// tangents at location 5
	vk::VertexInputAttributeDescription tangentAttribute = {};
	tangentAttribute.binding = 0;
	tangentAttribute.location = 5;
	tangentAttribute.format = vk::Format::eR32G32B32Sfloat;
	tangentAttribute.offset = offsetof(Vertex, tangent);
	
	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);
	description.attributes.push_back(materialIDAttribute);
	description.attributes.push_back(tangentAttribute);
	return description;
}

bool Model::load_assimp(std::string filePath)
{
	Scene& parentScene = *_parentScene;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs
		| aiProcess_CalcTangentSpace);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
		return false;
	}

	size_t dirPosWin = filePath.find_last_of('\\');
	size_t dirPosUnix = filePath.find_last_of('/');

	parentScene._directory = filePath.substr(0, dirPosUnix == std::string::npos ? dirPosWin : dirPosUnix) + '/';

	parentScene._matOffset = parentScene._diffuseTexNames.size();

	for (size_t i = 0; i < scene->mNumMaterials; ++i)
	{
		aiMaterial* material = scene->mMaterials[i];

		std::string matName = material->GetName().C_Str();
		std::string diffTexName = "";

		load_texture_names(material, aiTextureType_DIFFUSE, parentScene._diffuseTexNames, &diffTexName);
		load_texture_names(material, aiTextureType_AMBIENT, parentScene._ambientTexNames);
		load_texture_names(material, aiTextureType_SPECULAR, parentScene._specularTexNames);
		load_texture_names(material, aiTextureType_NORMALS, parentScene._normalMapNames);

		if (matName.empty())
		{
			matName = diffTexName;
		}
		parentScene._matNames.push_back(matName);
	}

	process_node(scene->mRootNode, *scene);
	return true;
}

void Model::process_node(aiNode* node, const aiScene& scene)
{
	for (size_t i = 0; i < node->mNumMeshes; ++i)
	{
		aiMesh* mesh = scene.mMeshes[node->mMeshes[i]];
		process_mesh(mesh, scene);
	}

	for (size_t i = 0; i < node->mNumChildren; ++i)
	{
		process_node(node->mChildren[i], scene);
	}
}

void Model::process_mesh(aiMesh* mesh, const aiScene& scene)
{
	Mesh newMesh = {};

	aiMaterial* material = nullptr;

	if (mesh->mMaterialIndex >= 0)
	{
		material = scene.mMaterials[mesh->mMaterialIndex];
	}

	for (size_t i = 0; i < mesh->mNumVertices; ++i)
	{
		Vertex newVertex = {};

		newVertex.position.x = mesh->mVertices[i].x;
		newVertex.position.y = mesh->mVertices[i].y;
		newVertex.position.z = mesh->mVertices[i].z;

		newVertex.normal.x = mesh->mNormals[i].x;
		newVertex.normal.y = mesh->mNormals[i].y;
		newVertex.normal.z = mesh->mNormals[i].z;

		newVertex.tangent.x = mesh->mTangents[i].x;
		newVertex.tangent.y = mesh->mTangents[i].y;
		newVertex.tangent.z = mesh->mTangents[i].z;
		newVertex.tangent.w = 0.0f;

		if (mesh->mTextureCoords[0])
		{
			newVertex.uv.x = mesh->mTextureCoords[0][i].x;
			newVertex.uv.y = mesh->mTextureCoords[0][i].y;
		}
		else
		{
			newVertex.uv = glm::vec2(0.0f, 0.0f);
		}

		if (material)
		{
			newVertex.materialID = _parentScene->_matOffset + mesh->mMaterialIndex;
		}
		newVertex.color = newVertex.normal;

		newMesh._vertices.push_back(newVertex);
	}

	for (size_t i = 0; i < mesh->mNumFaces; ++i)
	{
		aiFace face = mesh->mFaces[i];
		for (size_t j = 0; j < face.mNumIndices; ++j)
		{
			newMesh._indices.push_back(face.mIndices[j]);
		}
	}

	_meshes.push_back(std::move(newMesh));
}

void Model::load_texture_names(aiMaterial* mat, aiTextureType type, std::vector<std::string>& names, std::string* curName)
{
	size_t texCount = mat->GetTextureCount(type);
	if (texCount == 0)
	{
		names.push_back("");
		return;
	}
	for (size_t i = 0; i < texCount; ++i)
	{
		aiString name;
		if (mat->GetTexture(type, i, &name) != aiReturn_FAILURE)
		{
			names.push_back(_parentScene->_directory + name.C_Str());
			if (curName)
			{
				*curName = name.C_Str();
			}
		}
		else
		{
			names.push_back("");
		}
	}
}

bool Model::load_from_obj(std::string filePath)
{
	Scene& parentScene = *_parentScene;

	std::string matBaseDir = "../../../assets/";

	tinyobj::attrib_t vertexAttributes;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warn;
	std::string err;

	tinyobj::LoadObj(&vertexAttributes, &shapes, &materials, &warn, &err, filePath.data(), matBaseDir.data());

	if (!warn.empty())
	{
		std::cout << "WARNING: " << warn << std::endl;
	}

	if (!err.empty())
	{
		std::cout << "ERROR: " << err << std::endl;
		return false;
	}

	parentScene._diffuseTexNames.resize(materials.size());
	parentScene._ambientTexNames.resize(materials.size());
	parentScene._specularTexNames.resize(materials.size());
	size_t matNamesBaseSize = parentScene._matNames.size();
	parentScene._matNames.resize(matNamesBaseSize + materials.size());

	for (size_t m = 0; m < matNamesBaseSize + materials.size(); ++m)
	{
		parentScene._matNames[m] = materials[m].name;

		if (!materials[m].diffuse_texname.empty())
		{
			parentScene._diffuseTexNames[m] = matBaseDir + materials[m].diffuse_texname;
		}

		if (!materials[m].ambient_texname.empty())
		{
			parentScene._ambientTexNames[m] = matBaseDir + materials[m].ambient_texname;
		}

		if (!materials[m].specular_texname.empty())
		{
			parentScene._specularTexNames[m] = matBaseDir + materials[m].specular_texname;
		}
	}

	for (size_t s = 0; s < shapes.size(); ++s)
	{
		Mesh newMesh;

		size_t indexOffset = 0;
		constexpr int verticesPerFace = 3;

		newMesh._vertices.reserve(newMesh._vertices.size() +
			shapes[s].mesh.num_face_vertices.size() * verticesPerFace);

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

				// draw normals as default material
				newVertex.color = newVertex.normal;

				// vertex uv
				tinyobj::real_t ux = vertexAttributes.texcoords.empty() ? 0 : vertexAttributes.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t uy = vertexAttributes.texcoords.empty() ? 0 : vertexAttributes.texcoords[2 * idx.texcoord_index + 1];

				newVertex.uv.x = ux;
				newVertex.uv.y = 1 - uy; // Vulkan convention

				newVertex.materialID = shapes[s].mesh.material_ids[f];

				newMesh._vertices.push_back(newVertex);
			}
			indexOffset += verticesPerFace;
		}
	}

	return true;
}