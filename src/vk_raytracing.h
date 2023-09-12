#pragma once

#include "vk_types.h"
#include "vk_engine.h"

namespace vkrt
{
	vk::TransformMatrixKHR convertToTransformKHR(const glm::mat4& glmTransform);

	AccelerationStructure allocateAndBindAccel(VulkanEngine* engine, vk::AccelerationStructureCreateInfoKHR& createInfo);

	void cmdCreateBlas(VulkanEngine* engine, vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool);

	void cmdCompactBlas(VulkanEngine* engine, vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::QueryPool queryPool);

	void destroyNonCompacted(VulkanEngine* engine, std::vector<uint32_t>& indices, std::vector<AccelerationStructureBuild>& buildAs);

	void buildBlas(VulkanEngine* engine, const std::vector<BLASInput>& inputs,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	vk::DeviceAddress getBlasDeviceAddress(VulkanEngine* engine, uint32_t blasId);

	void buildTlas(VulkanEngine* engine, const std::vector<vk::AccelerationStructureInstanceKHR>& instances, bool update = false,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	void cmdCreateTlas(VulkanEngine* engine, vk::CommandBuffer cmd, uint32_t instanceCount, vk::DeviceAddress instBufferAddress,
		AllocatedBuffer& scratchBuffer, vk::BuildAccelerationStructureFlagsKHR flags, bool update = false, bool motion = false);
}
