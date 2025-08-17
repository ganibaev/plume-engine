#pragma once

#include "render_types.h"
#include "render_system.h"

namespace RenderBackendRTUtils
{
	vk::TransformMatrixKHR ConvertToTransformKHR(const glm::mat4& glmTransform);

	AccelerationStructure AllocateAndBindAccel(vk::AccelerationStructureCreateInfoKHR& createInfo);

	void CmdCreateBLAS(vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool);

	void CmdCompactBLAS(vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
		std::vector<AccelerationStructureBuild>& buildAs, vk::QueryPool queryPool);

	void DestroyNonCompacted(std::vector<uint32_t>& indices, std::vector<AccelerationStructureBuild>& buildAs);

	void BuildBLAS(Render::System* renderSys, const std::vector<Render::Backend::BLASInput>& inputs,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	vk::DeviceAddress GetBLASDeviceAddress(Render::System* renderSys, uint32_t blasId);

	void BuildTLAS(Render::System* renderSys, const std::vector<vk::AccelerationStructureInstanceKHR>& instances, bool update = false,
		vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);

	void CmdCreateTLAS(Render::System* renderSys, vk::CommandBuffer cmd, uint32_t instanceCount, vk::DeviceAddress instBufferAddress,
		Render::Buffer& scratchBuffer, vk::BuildAccelerationStructureFlagsKHR flags, bool update = false, bool motion = false);
}
