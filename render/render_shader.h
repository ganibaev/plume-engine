#pragma once

#include "render_types.h"


namespace Render
{


class Shader
{
public:
	void create(vk::Device* pDevice, std::string shaderFileName);
	void destroy();

	vk::PipelineShaderStageCreateInfo get_stage_create_info() const { return _stageCreateInfo; }

	enum class RTStageIndices
	{
		eRaygen,
		eMiss,
		eShadowMiss,
		eClosestHit,
		eAnyHit,
		eShaderGroupCount
	};

	static RTStageIndices get_rt_shader_index_from_file_name(std::string shaderFileName);

private:
	static constexpr const char* SHADER_BINARY_PATH = "../../../render/shader_binaries/";

	static vk::ShaderStageFlagBits get_shader_stage_from_file_name(std::string shaderFileName);

	void load_module(std::string shaderFileName);
	vk::PipelineShaderStageCreateInfo make_stage_create_info() const;

	vk::Device* _pDevice = nullptr;

	vk::ShaderStageFlagBits _stage;
	vk::ShaderModule _module;
	vk::PipelineShaderStageCreateInfo _stageCreateInfo;
};

} // namespace Render
