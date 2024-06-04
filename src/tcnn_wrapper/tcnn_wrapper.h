#pragma once

#include <json/json.hpp>

namespace vktcnn
{
	enum class CacheType
	{
		eDiffuse,
		eSpecular,
		eClassic
	};

	void create_cache_from_config(uint32_t inputDims, uint32_t outputDims, nlohmann::json config, CacheType type);
	void train(uint32_t numElements, const float* inputs, const float* targets, CacheType cacheType);

	void inference(uint32_t numElements, const float* input, float* output, CacheType cacheType);
	void terminate();
}