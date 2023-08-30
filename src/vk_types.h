#pragma once

#include <vulkan/vulkan.hpp>

#include "vk_mem_alloc.h"

struct AllocatedBuffer
{
	vk::Buffer _buffer;
	VmaAllocation _allocation;
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
	VmaAllocation _allocation;
};