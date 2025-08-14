#include "render_mesh.h"

#include "tiny_obj_loader.h"
#include <iostream>


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
		load_texture_names(material, aiTextureType_METALNESS, parentScene._metallicTexNames);
		load_texture_names(material, aiTextureType_DIFFUSE_ROUGHNESS, parentScene._roughnessTexNames);
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
		newMesh._matIndex = static_cast<int32_t>(_parentScene->_matOffset + mesh->mMaterialIndex);
		material->Get(AI_MATKEY_COLOR_EMISSIVE, newMesh._emittance);
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

		if (mesh->mTextureCoords[0])
		{
			newVertex.uv.x = mesh->mTextureCoords[0][i].x;
			newVertex.uv.y = mesh->mTextureCoords[0][i].y;
		}
		else
		{
			newVertex.uv = glm::vec2(0.0f, 0.0f);
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
	for (unsigned int i = 0; i < texCount; ++i)
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