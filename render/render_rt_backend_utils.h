#pragma once

#include "render_types.h"
#include "render_system.h"

namespace RenderBackendRTUtils
{
	vk::TransformMatrixKHR convertToTransformKHR(const glm::mat4& glmTransform);

	AccelerationStructure allocateAndBindAccel(vk::AccelerationStructureCreateInfoKHR& createInfo);

	void cmdCreateBlas(vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool);

	void cmdCompactBlas(vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::QueryPool queryPool);

	void destroyNonCompacted(std::vector<uint32_t>& indices, std::vector<AccelerationStructureBuild>& buildAs);

	void buildBlas(Render::System* renderSys, const std::vector<Render::Backend::BLASInput>& inputs,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	vk::DeviceAddress getBlasDeviceAddress(Render::System* renderSys, uint32_t blasId);

	void buildTlas(Render::System* renderSys, const std::vector<vk::AccelerationStructureInstanceKHR>& instances, bool update = false,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	void cmdCreateTlas(Render::System* renderSys, vk::CommandBuffer cmd, uint32_t instanceCount, vk::DeviceAddress instBufferAddress,
		Render::Buffer& scratchBuffer, vk::BuildAccelerationStructureFlagsKHR flags, bool update = false, bool motion = false);
}
