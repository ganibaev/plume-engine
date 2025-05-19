#include "render_shader.h"
#include "render_initializers.h"

#include <fstream>


void Render::Shader::create(vk::Device* pDevice, std::string shaderFileName, vk::ShaderStageFlagBits shaderStage)
{
	_pDevice = pDevice;
	_stage = shaderStage;

	load_module(shaderFileName);

	_stageCreateInfo = make_stage_create_info();
}


void Render::Shader::load_module(std::string shaderFileName)
{
	std::string filePath = SHADER_BINARY_PATH + shaderFileName + ".spv";
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);
	ASSERT(file.is_open(), "Can't open .spirv file");

	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	vk::ShaderModuleCreateInfo smCreateInfo = vkinit::sm_create_info(buffer);

	ASSERT(_pDevice, "Invalid vk::Device");

	_module = _pDevice->createShaderModule(smCreateInfo);

	ASSERT(_module, "Failed to build shader " << filePath);
}


vk::PipelineShaderStageCreateInfo Render::Shader::make_stage_create_info() const
{
	vk::PipelineShaderStageCreateInfo info = {};

	info.stage = _stage;
	info.module = _module;

	// shader entry point
	info.pName = "main";
	return info;
}


void Render::Shader::destroy()
{
	ASSERT(_pDevice, "Invalid vk::Device");

	_pDevice->destroyShaderModule(_module);
}
