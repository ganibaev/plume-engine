#include "render_raytracing.h"
#include "render_initializers.h"

vk::TransformMatrixKHR RenderRT::convertToTransformKHR(const glm::mat4& glmTransform)
{
	vk::TransformMatrixKHR res;

	std::array<std::array<float, 4U>, 3U> buffer = {};
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			buffer[j][i] = glmTransform[i][j];
		}
	}

	res.setMatrix(buffer);
	return res;
}

AccelerationStructure RenderRT::allocateAndBindAccel(RenderSystem* renderSys, vk::AccelerationStructureCreateInfoKHR& createInfo)
{
	AccelerationStructure resultAccel;

	AllocatedBuffer accelBuffer = renderSys->create_buffer(createInfo.size, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR
		| vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);

	renderSys->_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(renderSys->_allocator, accelBuffer._buffer, accelBuffer._allocation);
	});

	createInfo.buffer = accelBuffer._buffer;

	resultAccel._buffer = accelBuffer._buffer;
	resultAccel._allocation = accelBuffer._allocation;
	resultAccel._structure = renderSys->_device.createAccelerationStructureKHR(createInfo);
	
	return resultAccel;
}

void RenderRT::cmdCreateBlas(RenderSystem* renderSys, vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
	std::vector<AccelerationStructureBuild>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool)
{
	if (queryPool)
	{
		renderSys->_device.resetQueryPool(queryPool, 0, static_cast<uint32_t>(indices.size()));
	}
	uint32_t queryCount = 0;

	for (uint32_t index : indices)
	{
		vk::AccelerationStructureCreateInfoKHR blasCreateInfo;
		blasCreateInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		blasCreateInfo.size = buildAs[index]._sizesInfo.accelerationStructureSize;
		buildAs[index]._as = allocateAndBindAccel(renderSys, blasCreateInfo);

		buildAs[index]._geometryInfo.dstAccelerationStructure = buildAs[index]._as._structure;
		buildAs[index]._geometryInfo.scratchData = scratchAddress;

		// finally build the BLAS
		cmd.buildAccelerationStructuresKHR(buildAs[index]._geometryInfo, &buildAs[index]._rangeInfo);

		// reusing the same scratch buffer for now, ensuring previous build is finished via barrier
		vk::MemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
		barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;
		
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, {}, barrier, {}, {});

		if (queryPool)
		{
			// query compacted amount of memory needed
			cmd.writeAccelerationStructuresPropertiesKHR(buildAs[index]._geometryInfo.dstAccelerationStructure,
				vk::QueryType::eAccelerationStructureCompactedSizeKHR, queryPool, queryCount++);
		}
	}
}

void RenderRT::cmdCompactBlas(RenderSystem* renderSys, vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
	std::vector<AccelerationStructureBuild>& buildAs, vk::QueryPool queryPool)
{
	uint32_t queryCount = 0;

	std::vector<vk::DeviceSize> compactSizes(indices.size());
	VK_CHECK(renderSys->_device.getQueryPoolResults(queryPool, 0, static_cast<uint32_t>(compactSizes.size()), compactSizes.size(),
		compactSizes.data(), sizeof(vk::DeviceSize), vk::QueryResultFlagBits::eWait));

	for (uint32_t index : indices)
	{
		buildAs[index]._cleanupAs = buildAs[index]._as;
		buildAs[index]._sizesInfo.accelerationStructureSize = compactSizes[queryCount++];

		vk::AccelerationStructureCreateInfoKHR accelCreateInfo;
		accelCreateInfo.size = buildAs[index]._sizesInfo.accelerationStructureSize;
		accelCreateInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		buildAs[index]._as = allocateAndBindAccel(renderSys, accelCreateInfo);

		// copy original BLAS into its compact version
		vk::CopyAccelerationStructureInfoKHR copyInfo;
		copyInfo.src = buildAs[index]._geometryInfo.dstAccelerationStructure;
		copyInfo.dst = buildAs[index]._as._structure;
		copyInfo.mode = vk::CopyAccelerationStructureModeKHR::eCompact;

		cmd.copyAccelerationStructureKHR(copyInfo);
	}
}

void RenderRT::destroyNonCompacted(RenderSystem* renderSys, std::vector<uint32_t>& indices, std::vector<AccelerationStructureBuild>& buildAs)
{
	for (uint32_t i : indices)
	{
		vmaDestroyBuffer(renderSys->_allocator, buildAs[i]._cleanupAs._buffer, buildAs[i]._cleanupAs._allocation);
	}
}

void RenderRT::buildBlas(RenderSystem* renderSys, const std::vector<BLASInput>& inputs,
	vk::BuildAccelerationStructureFlagsKHR flags /* = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace */)
{
	uint32_t numBlas = static_cast<uint32_t>(inputs.size());
	vk::DeviceSize blasTotalSize = 0;
	uint32_t numCompactions = 0;
	vk::DeviceSize maxScratchSize = 0;

	std::vector<AccelerationStructureBuild> buildAs(numBlas);
	
	for (uint32_t i = 0; i < numBlas; ++i)
	{
		buildAs[i]._geometryInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		buildAs[i]._geometryInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
		buildAs[i]._geometryInfo.flags = flags;
		buildAs[i]._geometryInfo.setGeometries(inputs[i]._geometry);

		buildAs[i]._rangeInfo = inputs[i]._buildRangeInfo;

		uint32_t maxPrimCount = inputs[i]._buildRangeInfo.primitiveCount;
		buildAs[i]._sizesInfo = renderSys->_device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
			buildAs[i]._geometryInfo, maxPrimCount);

		blasTotalSize += buildAs[i]._sizesInfo.accelerationStructureSize;
		maxScratchSize = std::max(maxScratchSize, buildAs[i]._sizesInfo.buildScratchSize);
		numCompactions += (buildAs[i]._geometryInfo.flags & vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction)
			== vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
	}

	AllocatedBuffer scratchBuffer = renderSys->create_buffer(maxScratchSize, vk::BufferUsageFlagBits::eShaderDeviceAddress
		| vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	vk::BufferDeviceAddressInfo bufferAddressInfo(scratchBuffer._buffer);
	vk::DeviceAddress scratchAddress = renderSys->_device.getBufferAddress(bufferAddressInfo);

	// calculate BLAS size after compaction
	vk::QueryPool queryPool;
	if (numCompactions > 0)
	{
		assert(numCompactions == numBlas);
		vk::QueryPoolCreateInfo qPoolInfo;
		qPoolInfo.queryCount = numBlas;
		qPoolInfo.queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR;
		queryPool = renderSys->_device.createQueryPool(qPoolInfo);
	}

	// batching BLAS creation
	std::vector<uint32_t> blasIndices;
	vk::DeviceSize batchSize = 0;
	vk::DeviceSize batchLimit = 236000000U;
	vk::CommandPool cmdPool = renderSys->_uploadContext._commandPool;

	for (uint32_t i = 0; i < numBlas; ++i)
	{
		blasIndices.push_back(i);
		batchSize += buildAs[i]._sizesInfo.accelerationStructureSize;

		// if over limit, switch to another command buffer
		if (batchSize >= batchLimit || i == numBlas - 1)
		{
			vk::CommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(cmdPool);
			vk::CommandBuffer newCmd = renderSys->_device.allocateCommandBuffers(cmdAllocInfo)[0];
			renderSys->immediate_submit([&](vk::CommandBuffer cmd) {
				cmdCreateBlas(renderSys, cmd, blasIndices, buildAs, scratchAddress, queryPool);
			}, newCmd);

			if (queryPool)
			{
				vk::CommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(cmdPool);
				vk::CommandBuffer newCmd = renderSys->_device.allocateCommandBuffers(cmdAllocInfo)[0];
				renderSys->immediate_submit([&](vk::CommandBuffer cmd) {
					cmdCompactBlas(renderSys, cmd, blasIndices, buildAs, queryPool);
				}, newCmd);

				// destroy the non-compacted version of the BLAS
				destroyNonCompacted(renderSys, blasIndices, buildAs);
			}

			// reset the info for next batch
			batchSize = 0;
			blasIndices.clear();
		}
	}

	// move all BLAS to render system
	for (auto& blas : buildAs)
	{
		renderSys->_bottomLevelASVec.emplace_back(blas._as);
		renderSys->_mainDeletionQueue.push_function([=]() {
			renderSys->_device.destroyAccelerationStructureKHR(blas._as._structure);
		});
	}

	renderSys->_device.destroyQueryPool(queryPool);
	vmaDestroyBuffer(renderSys->_allocator, scratchBuffer._buffer, scratchBuffer._allocation);
}

vk::DeviceAddress RenderRT::getBlasDeviceAddress(RenderSystem* renderSys, uint32_t blasId)
{
	assert(static_cast<size_t>(blasId) < renderSys->_bottomLevelASVec.size());
	
	vk::AccelerationStructureDeviceAddressInfoKHR addressInfo;
	addressInfo.setAccelerationStructure(renderSys->_bottomLevelASVec[blasId]._structure);

	return renderSys->_device.getAccelerationStructureAddressKHR(addressInfo);
}

void RenderRT::buildTlas(RenderSystem* renderSys, const std::vector<vk::AccelerationStructureInstanceKHR>& instances, bool update /* = false */,
	vk::BuildAccelerationStructureFlagsKHR flags /* = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace */)
{
	// make sure this function is only called once, unless an update is required
	assert(!renderSys->_topLevelAS._structure || update);

	uint32_t instanceCount = static_cast<uint32_t>(instances.size());

	size_t instanceBufferSize = instances.size() * sizeof(vk::AccelerationStructureInstanceKHR);

	AllocatedBuffer instanceBuffer = renderSys->create_buffer(instanceBufferSize, vk::BufferUsageFlagBits::eShaderDeviceAddress
		| vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_CPU_TO_GPU);

	// copy instance data to buffer
	AllocatedBuffer stagingBuffer = renderSys->create_buffer(instanceBufferSize, vk::BufferUsageFlagBits::eTransferSrc,
		VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = nullptr;
	vmaMapMemory(renderSys->_allocator, stagingBuffer._allocation, &data);

	std::memcpy(data, instances.data(), instanceBufferSize);

	vmaUnmapMemory(renderSys->_allocator, stagingBuffer._allocation);

	renderSys->immediate_submit([&](vk::CommandBuffer cmd) {
		vk::BufferCopy copy;
		copy.setSrcOffset(0);
		copy.setDstOffset(0);
		copy.setSize(instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
		cmd.copyBuffer(stagingBuffer._buffer, instanceBuffer._buffer, copy);
	}, renderSys->_uploadContext._commandBuffer);

	vmaDestroyBuffer(renderSys->_allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	vk::DeviceAddress instBufferAddress = renderSys->get_buffer_device_address(instanceBuffer._buffer);

	// make sure instance buffer is copied before TLAS gets built
	vk::MemoryBarrier barrier;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;

	AllocatedBuffer scratchBuffer;

	renderSys->immediate_submit([&](vk::CommandBuffer cmd) {
		renderSys->_uploadContext._commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, {}, barrier, {}, {});
		cmdCreateTlas(renderSys, cmd, instanceCount, instBufferAddress, scratchBuffer,
			flags, update);
	}, renderSys->_uploadContext._commandBuffer);
	
	renderSys->_mainDeletionQueue.push_function([=]() {
		renderSys->_device.destroyAccelerationStructureKHR(renderSys->_topLevelAS._structure);
	});

	vmaDestroyBuffer(renderSys->_allocator, scratchBuffer._buffer, scratchBuffer._allocation);
	vmaDestroyBuffer(renderSys->_allocator, instanceBuffer._buffer, instanceBuffer._allocation);
}

void RenderRT::cmdCreateTlas(RenderSystem* renderSys, vk::CommandBuffer cmd, uint32_t instanceCount, vk::DeviceAddress instBufferAddress,
	AllocatedBuffer& scratchBuffer, vk::BuildAccelerationStructureFlagsKHR flags, bool update /* = false */, bool motion /* = false */)
{
	vk::AccelerationStructureGeometryInstancesDataKHR instData;
	instData.data.deviceAddress = instBufferAddress;

	vk::AccelerationStructureGeometryKHR tlasGeometry;
	tlasGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
	tlasGeometry.geometry.instances = instData;

	vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
	buildInfo.flags = flags;
	buildInfo.setGeometries(tlasGeometry);
	buildInfo.mode = update ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild;
	buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;

	vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
	sizeInfo = renderSys->_device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
		buildInfo, instanceCount);

	vk::AccelerationStructureCreateInfoKHR createInfo;
	createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	createInfo.size = sizeInfo.accelerationStructureSize;

	renderSys->_topLevelAS = allocateAndBindAccel(renderSys, createInfo);

	scratchBuffer = renderSys->create_buffer(sizeInfo.buildScratchSize, vk::BufferUsageFlagBits::eStorageBuffer |
		vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_GPU_ONLY);

	vk::DeviceAddress scratchAddress = renderSys->get_buffer_device_address(scratchBuffer._buffer);

	// finally building TLAS

	buildInfo.dstAccelerationStructure = renderSys->_topLevelAS._structure;
	buildInfo.scratchData.deviceAddress = scratchAddress;
	
	vk::AccelerationStructureBuildRangeInfoKHR offsetInfo;
	offsetInfo.primitiveCount = instanceCount;

	cmd.buildAccelerationStructuresKHR(buildInfo, &offsetInfo);
}