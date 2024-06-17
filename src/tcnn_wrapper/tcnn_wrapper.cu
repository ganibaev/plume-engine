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

	float* trainingDataBuffer = nullptr;
	float* trainingTargetsBuffer = nullptr;
	float* queryBuffer = nullptr;

	float* resBuffer = nullptr;
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

float* vktcnn::get_external_memory_ptr(HANDLE handle, uint64_t size)
{
	cudaExternalMemory_t ext = nullptr;

	cudaExternalMemoryHandleDesc desc = {};
	desc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
	desc.handle.win32.handle = handle;
	desc.size = size;

	CUDA_CHECK_THROW(cudaImportExternalMemory(&ext, &desc));

	void* ptr = nullptr;

	cudaExternalMemoryBufferDesc bDesc = {};
	bDesc.flags = 0;
	bDesc.offset = 0;
	bDesc.size = size;

	CUDA_CHECK_THROW(cudaExternalMemoryGetMappedBuffer(&ptr, ext, &bDesc));

	return reinterpret_cast<float*>(ptr);
}

void vktcnn::train(uint32_t numElements, const HANDLE inputs, const HANDLE targets, CacheType cacheType,
	size_t inputBufferSize, size_t targetBufferSize)
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
		return;
	}

	if (numElements < tcnn::batch_size_granularity * TRAINING_STEPS_PER_FRAME)
	{
		return;
	}

	if (!cache->trainingDataBuffer)
	{
		cache->trainingDataBuffer = get_external_memory_ptr(inputs, inputBufferSize);
	}

	if (!cache->trainingTargetsBuffer)
	{
		cache->trainingTargetsBuffer = get_external_memory_ptr(targets, targetBufferSize);
	}

	uint32_t largestBatch = tcnn::previous_multiple(numElements, tcnn::batch_size_granularity * TRAINING_STEPS_PER_FRAME);

	uint32_t splitBatchSize = largestBatch / TRAINING_STEPS_PER_FRAME;
	CUDA_CHECK_THROW(cudaDeviceSynchronize());
	for (int i = 0; i < TRAINING_STEPS_PER_FRAME; ++i)
	{
		tcnn::GPUMatrix<float> inputMatrix(cache->trainingDataBuffer + splitBatchSize * i, cache->inputDims, splitBatchSize);
		tcnn::GPUMatrix<float> targetMatrix(cache->trainingTargetsBuffer + splitBatchSize * i, cache->outputDims, splitBatchSize);

		tcnn::SyncedMultiStream syncedStream{ cache->trainingStream, 2 };
		auto ctx = cache->model.trainer->training_step(syncedStream.get(1), inputMatrix, targetMatrix);
	}
	CUDA_CHECK_THROW(cudaDeviceSynchronize());
}

void vktcnn::inference(uint32_t numElements, const HANDLE input, HANDLE output, CacheType cacheType, size_t inputBufferSize,
	size_t resBufferSize)
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
		return;
	}

	if (!cache->queryBuffer)
	{
		cache->queryBuffer = get_external_memory_ptr(input, inputBufferSize);
	}

	if (!cache->resBuffer)
	{
		cache->resBuffer = get_external_memory_ptr(output, resBufferSize);
	}

	tcnn::GPUMatrix<float> inputs(cache->queryBuffer, cache->inputDims, numElements);
	tcnn::GPUMatrix<float> outputs(cache->resBuffer, cache->outputDims, numElements);

	tcnn::SyncedMultiStream syncedStream{ cache->inferenceStream, 2 };
	CUDA_CHECK_THROW(cudaDeviceSynchronize());
	cache->model.network->inference(syncedStream.get(1), inputs, outputs);
	CUDA_CHECK_THROW(cudaDeviceSynchronize());
}

void vktcnn::terminate()
{
	tcnn::free_all_gpu_memory_arenas();
}