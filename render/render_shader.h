#pragma once

#include "render_types.h"


namespace Render
{

class Shader
{
public:
	void create(vk::Device* pDevice, std::string shaderFileName, vk::ShaderStageFlagBits shaderStage);
	void destroy();

	vk::PipelineShaderStageCreateInfo get_stage_create_info() const { return _stageCreateInfo; }

private:
	static constexpr const char* SHADER_BINARY_PATH = "../../../render/shader_binaries/";

	void load_module(std::string shaderFileName);
	vk::PipelineShaderStageCreateInfo make_stage_create_info() const;

	vk::Device* _pDevice = nullptr;

	vk::ShaderStageFlagBits _stage;
	vk::ShaderModule _module;
	vk::PipelineShaderStageCreateInfo _stageCreateInfo;
};

} // namespace Render
