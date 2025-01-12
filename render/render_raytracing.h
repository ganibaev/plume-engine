#pragma once

#include "render_types.h"
#include "render_system.h"

namespace RenderRT
{
	vk::TransformMatrixKHR convertToTransformKHR(const glm::mat4& glmTransform);

	AccelerationStructure allocateAndBindAccel(RenderSystem* renderSys, vk::AccelerationStructureCreateInfoKHR& createInfo);

	void cmdCreateBlas(RenderSystem* renderSys, vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool);

	void cmdCompactBlas(RenderSystem* renderSys, vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::QueryPool queryPool);

	void destroyNonCompacted(RenderSystem* renderSys, std::vector<uint32_t>& indices, std::vector<AccelerationStructureBuild>& buildAs);

	void buildBlas(RenderSystem* renderSys, const std::vector<BLASInput>& inputs,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	vk::DeviceAddress getBlasDeviceAddress(RenderSystem* renderSys, uint32_t blasId);

	void buildTlas(RenderSystem* renderSys, const std::vector<vk::AccelerationStructureInstanceKHR>& instances, bool update = false,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	void cmdCreateTlas(RenderSystem* renderSys, vk::CommandBuffer cmd, uint32_t instanceCount, vk::DeviceAddress instBufferAddress,
		AllocatedBuffer& scratchBuffer, vk::BuildAccelerationStructureFlagsKHR flags, bool update = false, bool motion = false);
}
