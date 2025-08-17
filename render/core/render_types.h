#pragma once

#include "../engine/plm_common.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#define VMA_DEBUG_LOG_FORMAT
#include "vk_mem_alloc.h"

#include <deque>
#include <functional>

#include "../render/shaders/host_device_common.h"

constexpr uint32_t FRAME_OVERLAP = 3;


struct AccelerationStructure
{
	vk::AccelerationStructureKHR _structure;
	vk::DeviceAddress _deviceAddress;
	vk::Buffer _buffer;
	VmaAllocation _allocation = {};
};


struct AccelerationStructureBuild
{
	vk::AccelerationStructureBuildGeometryInfoKHR _geometryInfo;
	vk::AccelerationStructureBuildRangeInfoKHR _rangeInfo;
	vk::AccelerationStructureBuildSizesInfoKHR _sizesInfo;

	AccelerationStructure _as;
	AccelerationStructure _cleanupAs;
};


struct DeletionQueue {
	std::deque<std::function<void()>> deleters;

	void PushFunction(std::function<void()>&& function)
	{
		deleters.push_back(function);
	}

	void Flush()
	{
		for (auto& func : deleters)
		{
			func();
		}

		deleters.clear();
	}
};


#ifndef NDEBUG
#define ASSERT_VK(condition, message)                                          \
    do                                                                         \
    {                                                                          \
		VkResult vkRes = static_cast<VkResult>(condition);                     \
        ASSERT(vkRes == VK_SUCCESS, message);                                  \
    }                                                                          \
    while (false)
#else
#define ASSERT_VK(condition, message) do { } while (false)
#endif
