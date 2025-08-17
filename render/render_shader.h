#pragma once

#include "render_types.h"


namespace Render
{


class Shader
{
public:
	void Create(vk::Device* pDevice, std::string shaderFileName);
	void Destroy();

	vk::PipelineShaderStageCreateInfo GetStageCreateInfo() const { return _stageCreateInfo; }

	enum class RTStageIndices
	{
		eRaygen,
		eMiss,
		eShadowMiss,
		eClosestHit,
		eAnyHit,
		eShaderGroupCount
	};

	static RTStageIndices GetRTShaderIndexFromFileName(std::string shaderFileName);

private:
	static constexpr const char* SHADER_BINARY_PATH = "../../../render/shader_binaries/";

	static vk::ShaderStageFlagBits GetShaderStageFromFileName(std::string shaderFileName);

	void LoadModule(std::string shaderFileName);
	vk::PipelineShaderStageCreateInfo MakeStageCreateInfo() const;

	vk::Device* _pDevice = nullptr;

	vk::ShaderStageFlagBits _stage;
	vk::ShaderModule _module;
	vk::PipelineShaderStageCreateInfo _stageCreateInfo;
};

} // namespace Render
