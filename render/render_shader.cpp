#include "render_shader.h"
#include "render_initializers.h"

#include <fstream>


void Render::Shader::Create(vk::Device* pDevice, std::string shaderFileName)
{
	_pDevice = pDevice;
	_stage = GetShaderStageFromFileName(shaderFileName);

	LoadModule(shaderFileName);

	_stageCreateInfo = MakeStageCreateInfo();
}


void Render::Shader::LoadModule(std::string shaderFileName)
{
	std::string filePath = SHADER_BINARY_PATH + shaderFileName + ".spv";
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);
	ASSERT(file.is_open(), "Can't open .spirv file");

	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	vk::ShaderModuleCreateInfo smCreateInfo = vkinit::ShaderModuleInfo(buffer);

	ASSERT(_pDevice, "Invalid vk::Device");

	_module = _pDevice->createShaderModule(smCreateInfo);

	ASSERT(_module, "Failed to build shader " << filePath);
}


vk::PipelineShaderStageCreateInfo Render::Shader::MakeStageCreateInfo() const
{
	vk::PipelineShaderStageCreateInfo info = {};

	info.stage = _stage;
	info.module = _module;

	// shader entry point
	info.pName = "main";
	return info;
}


Render::Shader::RTStageIndices Render::Shader::GetRTShaderIndexFromFileName(std::string shaderName)
{
	vk::ShaderStageFlagBits shaderStage = GetShaderStageFromFileName(shaderName);

	Render::Shader::RTStageIndices stageIndex = {};

	switch (shaderStage)
	{
	case vk::ShaderStageFlagBits::eRaygenKHR:
		return Shader::RTStageIndices::eRaygen;
	case vk::ShaderStageFlagBits::eAnyHitKHR:
		return Shader::RTStageIndices::eAnyHit;
	case vk::ShaderStageFlagBits::eClosestHitKHR:
		return Shader::RTStageIndices::eClosestHit;
	case vk::ShaderStageFlagBits::eMissKHR:
		if (shaderName.length() >= 12 && shaderName.substr(shaderName.length() - 12, 12) == "shadow.rmiss")
		{
			return Shader::RTStageIndices::eShadowMiss;
		}

		return Shader::RTStageIndices::eMiss;
	default:
		ASSERT(false, "Invalid or unsupported RT shader stage");
		return Shader::RTStageIndices::eShaderGroupCount;
	}
}


vk::ShaderStageFlagBits Render::Shader::GetShaderStageFromFileName(std::string shaderFileName)
{
	ASSERT(shaderFileName.length() >= 5, "Invalid shader file name");

	vk::ShaderStageFlagBits resStage = {};
	std::string extensionSubstr = shaderFileName.substr(shaderFileName.length() - 4, 4);
	if (extensionSubstr == "vert")
	{
		resStage = vk::ShaderStageFlagBits::eVertex;
	}
	else if (extensionSubstr == "frag")
	{
		resStage = vk::ShaderStageFlagBits::eFragment;
	}
	else if (extensionSubstr == "comp")
	{
		resStage = vk::ShaderStageFlagBits::eCompute;
	}
	else if (extensionSubstr == "rgen")
	{
		resStage = vk::ShaderStageFlagBits::eRaygenKHR;
	}
	else if (extensionSubstr == "ahit")
	{
		resStage = vk::ShaderStageFlagBits::eAnyHitKHR;
	}
	else if (extensionSubstr == "chit")
	{
		resStage = vk::ShaderStageFlagBits::eClosestHitKHR;
	}
	else if (extensionSubstr == "miss")
	{
		resStage = vk::ShaderStageFlagBits::eMissKHR;
	}
	else
	{
		ASSERT(false, "Invalid shader format. If it's a new format please handle it in the function above.");
	}

	return resStage;
}


void Render::Shader::Destroy()
{
	ASSERT(_pDevice, "Invalid vk::Device");

	_pDevice->destroyShaderModule(_module);
}
