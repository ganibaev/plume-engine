#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

#define VMA_DEBUG_LOG_FORMAT
#include "vk_mem_alloc.h"

struct AllocatedBuffer
{
	vk::Buffer _buffer;
	VmaAllocation _allocation = {};
};

enum class ImageType
{
	eTexture = 0,
	eCubemap = 1
};

struct AllocatedImage
{
	uint32_t _mipLevels = 1;
	vk::Image _image;
	VmaAllocation _allocation = {};
};

struct AccelerationStructure
{
	vk::AccelerationStructureKHR _structure;
	vk::DeviceAddress _deviceAddress;
	vk::Buffer _buffer;
	VmaAllocation _allocation = {};
};

struct BLASInput
{
	vk::AccelerationStructureGeometryTrianglesDataKHR _triangles;
	vk::AccelerationStructureGeometryKHR _geometry;
	vk::AccelerationStructureBuildRangeInfoKHR _buildRangeInfo;
};

struct AccelerationStructureBuild
{
	vk::AccelerationStructureBuildGeometryInfoKHR _geometryInfo;
	vk::AccelerationStructureBuildRangeInfoKHR _rangeInfo;
	vk::AccelerationStructureBuildSizesInfoKHR _sizesInfo;

	AccelerationStructure _as;
	AccelerationStructure _cleanupAs;
};