#include "plm_scene.h"

#include "tiny_obj_loader.h"
#include <iostream>


bool Plume::Model::LoadAssimp(std::string filePath)
{
	Scene& parentScene = *pParentScene;

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

	parentScene.directory = filePath.substr(0, dirPosUnix == std::string::npos ? dirPosWin : dirPosUnix) + '/';

	parentScene.matOffset = parentScene.diffuseTexNames.size();

	for (size_t i = 0; i < scene->mNumMaterials; ++i)
	{
		aiMaterial* material = scene->mMaterials[i];

		std::string matName = material->GetName().C_Str();
		std::string diffTexName = "";

		LoadTextureNames(material, aiTextureType_DIFFUSE, parentScene.diffuseTexNames, &diffTexName);
		LoadTextureNames(material, aiTextureType_METALNESS, parentScene.metallicTexNames);
		LoadTextureNames(material, aiTextureType_DIFFUSE_ROUGHNESS, parentScene.roughnessTexNames);
		LoadTextureNames(material, aiTextureType_NORMALS, parentScene.normalMapNames);

		if (matName.empty())
		{
			matName = diffTexName;
		}
		parentScene.matNames.push_back(matName);
	}

	ProcessNode(scene->mRootNode, *scene);
	return true;
}


void Plume::Model::ProcessNode(aiNode* node, const aiScene& scene)
{
	for (size_t i = 0; i < node->mNumMeshes; ++i)
	{
		aiMesh* mesh = scene.mMeshes[node->mMeshes[i]];
		ProcessMesh(mesh, scene);
	}

	for (size_t i = 0; i < node->mNumChildren; ++i)
	{
		ProcessNode(node->mChildren[i], scene);
	}
}


void Plume::Model::ProcessMesh(aiMesh* mesh, const aiScene& scene)
{
	Mesh newMesh = {};

	aiMaterial* material = nullptr;

	if (mesh->mMaterialIndex >= 0)
	{
		material = scene.mMaterials[mesh->mMaterialIndex];
		newMesh.matIndex = static_cast<int32_t>(pParentScene->matOffset + mesh->mMaterialIndex);
		material->Get(AI_MATKEY_COLOR_EMISSIVE, newMesh.emittance);
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

		newMesh.vertices.push_back(newVertex);
	}

	for (size_t i = 0; i < mesh->mNumFaces; ++i)
	{
		aiFace face = mesh->mFaces[i];
		for (size_t j = 0; j < face.mNumIndices; ++j)
		{
			newMesh.indices.push_back(face.mIndices[j]);
		}
	}

	meshes.push_back(std::move(newMesh));
}


void Plume::Model::LoadTextureNames(aiMaterial* mat, aiTextureType type, std::vector<std::string>& names, std::string* curName) const
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
			names.push_back(pParentScene->directory + name.C_Str());
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


void Plume::Scene::DefaultInit()
{
	// TODO: support runtime scene loading with ImGui interface
	
	Model suzanneModel;
	suzanneModel.pParentScene = this;
	suzanneModel.LoadAssimp("../../../assets/suzanne/Suzanne.gltf");
	suzanneModel.transformMatrix = glm::translate(glm::mat4{ 1.0f }, glm::vec3(2.8f, -8.0f, 0));

	models["suzanne"] = suzanneModel;


	Model sponza;
	sponza.pParentScene = this;
	sponza.LoadAssimp("../../../assets/sponza/Sponza.gltf");
	sponza.transformMatrix = glm::translate(glm::vec3{ 5, -10, 0 }) * glm::rotate(glm::radians(90.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4{ 1.0f }, glm::vec3(0.05f, 0.05f, 0.05f));

	models["sponza"] = sponza;


	Model cube;
	cube.pParentScene = this;
	cube.LoadAssimp("../../../assets/cube.gltf");
	cube.transformMatrix = glm::scale(glm::mat4{ 1.0f }, glm::vec3(6000.0f, 6000.0f, 6000.0f));

	models["skybox"] = cube;
}


const Plume::Model* Plume::Scene::GetPModel(const std::string& modelName) const
{
	auto it = models.find(modelName);
	if (it == models.end())
	{
		return nullptr;
	}
	else
	{
		return &(*it).second;
	}
}
