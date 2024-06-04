#include "tcnn_wrapper.h"
#include <stdio.h>

#include <tiny-cuda-nn/common.h>
#include <tiny-cuda-nn/gpu_matrix.h>
#include <tiny-cuda-nn/config.h>
#include <tiny-cuda-nn/common_device.h>
#include <tiny-cuda-nn/multi_stream.h>

#define TRAINING_BATCH_SIZE (1 << 17)
#define INFERENCE_BATCH_SIZE (1920 * 1080)
#define TRAINING_STEPS_PER_FRAME 1


struct RadianceCache
{
	cudaStream_t trainingStream;
	cudaStream_t inferenceStream;
	uint32_t inputDims;
	uint32_t outputDims;
    tcnn::TrainableModel model;
};

static RadianceCache diffuseRC;
static RadianceCache specularRC;
static RadianceCache classicRC;


void vktcnn::create_cache_from_config(uint32_t inputDims, uint32_t outputDims, nlohmann::json config, vktcnn::CacheType type)
{
	auto model = tcnn::create_from_config(inputDims, outputDims, config);

	switch (type)
	{
	case vktcnn::CacheType::eDiffuse:
		diffuseRC.model = model;
		diffuseRC.inputDims = inputDims;
		diffuseRC.outputDims = outputDims;

		CUDA_CHECK_THROW(cudaStreamCreate(&diffuseRC.inferenceStream));
		diffuseRC.trainingStream = diffuseRC.inferenceStream;
		break;
	case vktcnn::CacheType::eSpecular:
		specularRC.model = model;
		specularRC.inputDims = inputDims;
		specularRC.outputDims = outputDims;

		CUDA_CHECK_THROW(cudaStreamCreate(&specularRC.inferenceStream));
		specularRC.trainingStream = specularRC.inferenceStream;
		break;
	case vktcnn::CacheType::eClassic:
		classicRC.model = model;
		classicRC.inputDims = inputDims;
		classicRC.outputDims = outputDims;

		CUDA_CHECK_THROW(cudaStreamCreate(&classicRC.inferenceStream));
		classicRC.trainingStream = classicRC.inferenceStream;
		break;
	default:
		break;
	}
}

void vktcnn::train(uint32_t numElements, const float* inputs, const float* targets, CacheType cacheType)
{
	RadianceCache* cache = nullptr;
	switch (cacheType)
	{
	case vktcnn::CacheType::eDiffuse:
		cache = &diffuseRC;
		break;
	case vktcnn::CacheType::eSpecular:
		cache = &specularRC;
		break;
	case vktcnn::CacheType::eClassic:
		cache = &classicRC;
		break;
	default:
		break;
	}

	if (numElements < tcnn::batch_size_granularity * TRAINING_STEPS_PER_FRAME)
	{
		return;
	}

	tcnn::GPUMemory<float> inputMemory(numElements * cache->inputDims);
	tcnn::GPUMemory<float> targetMemory(numElements * cache->outputDims);

	inputMemory.copy_from_host(inputs);
	targetMemory.copy_from_host(targets);

	uint32_t largestBatch = tcnn::previous_multiple(numElements, tcnn::batch_size_granularity * TRAINING_STEPS_PER_FRAME);

	uint32_t splitBatchSize = largestBatch / TRAINING_STEPS_PER_FRAME;

	for (int i = 0; i < TRAINING_STEPS_PER_FRAME; ++i)
	{
		tcnn::GPUMatrix<float> inputMatrix(inputMemory.data() + splitBatchSize * i, cache->inputDims, splitBatchSize);
		tcnn::GPUMatrix<float> targetMatrix(targetMemory.data() + splitBatchSize * i, cache->outputDims, splitBatchSize);

		tcnn::SyncedMultiStream syncedStream{ cache->trainingStream, 2 };
		auto ctx = cache->model.trainer->training_step(syncedStream.get(1), inputMatrix, targetMatrix);
	}
}

void vktcnn::inference(uint32_t numElements, const float* input, float* output, CacheType cacheType)
{
	RadianceCache* cache = nullptr;
	switch (cacheType)
	{
	case vktcnn::CacheType::eDiffuse:
		cache = &diffuseRC;
		break;
	case vktcnn::CacheType::eSpecular:
		cache = &specularRC;
		break;
	case vktcnn::CacheType::eClassic:
		cache = &classicRC;
		break;
	default:
		break;
	}

	tcnn::GPUMemory<float> inputMemory(numElements * cache->inputDims);
	inputMemory.copy_from_host(input);
	tcnn::GPUMemory<float> outputMemory(numElements * cache->outputDims);


	tcnn::GPUMatrix<float> inputs(inputMemory.data(), cache->inputDims, numElements);
	tcnn::GPUMatrix<float> outputs(outputMemory.data(), cache->outputDims, numElements);

	tcnn::SyncedMultiStream syncedStream{ cache->inferenceStream, 2 };
	cache->model.network->inference(syncedStream.get(1), inputs, outputs);
	cudaDeviceSynchronize();
	outputMemory.copy_to_host(output);
}

void vktcnn::terminate()
{
	tcnn::free_all_gpu_memory_arenas();
}