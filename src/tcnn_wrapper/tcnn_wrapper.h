#pragma once

#include <json/json.hpp>
#include <windows.h>

namespace vktcnn
{
	enum class CacheType
	{
		eDiffuse,
		eSpecular,
		eClassic
	};

	void create_cache_from_config(uint32_t inputDims, uint32_t outputDims, nlohmann::json config, CacheType type);

	float* get_external_memory_ptr(HANDLE handle, uint64_t size);

	void train(uint32_t numElements, const HANDLE inputs, const HANDLE targets, CacheType cacheType,
		size_t inputBufferSize, size_t targetBufferSize);
	void inference(uint32_t numElements, const HANDLE input, HANDLE output, CacheType cacheType, size_t inputBufferSize,
		size_t resBufferSize);
	void terminate();
}