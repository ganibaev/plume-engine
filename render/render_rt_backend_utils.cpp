#include "render_rt_backend_utils.h"
#include "render_initializers.h"


vk::TransformMatrixKHR RenderBackendRTUtils::convertToTransformKHR(const glm::mat4& glmTransform)
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


AccelerationStructure RenderBackendRTUtils::allocateAndBindAccel(vk::AccelerationStructureCreateInfoKHR& createInfo)
{
	auto* backend = Render::Backend::AcquireInstance();

	Render::Buffer::CreateInfo asInfo = {};
	asInfo.allocSize = createInfo.size;
	asInfo.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	asInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	Render::Buffer accelBuffer = backend->CreateBuffer(asInfo);

	createInfo.buffer = accelBuffer.GetHandle();

	AccelerationStructure resultAccel;

	resultAccel._buffer = accelBuffer.GetHandle();
	resultAccel._allocation = accelBuffer.GetAllocation();
	resultAccel._structure = backend->GetPDevice()->createAccelerationStructureKHR(createInfo);
	
	return resultAccel;
}


void RenderBackendRTUtils::cmdCreateBlas(vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
	std::vector<AccelerationStructureBuild>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool)
{
	auto* backend = Render::Backend::AcquireInstance();

	if (queryPool)
	{
		backend->GetPDevice()->resetQueryPool(queryPool, 0, static_cast<uint32_t>(indices.size()));
	}
	uint32_t queryCount = 0;

	for (uint32_t index : indices)
	{
		vk::AccelerationStructureCreateInfoKHR blasCreateInfo;
		blasCreateInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		blasCreateInfo.size = buildAs[index]._sizesInfo.accelerationStructureSize;
		buildAs[index]._as = allocateAndBindAccel(blasCreateInfo);

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


void RenderBackendRTUtils::cmdCompactBlas(vk::CommandBuffer cmd, std::vector<uint32_t>& indices,
	std::vector<AccelerationStructureBuild>& buildAs, vk::QueryPool queryPool)
{
	auto* backend = Render::Backend::AcquireInstance();

	uint32_t queryCount = 0;

	std::vector<vk::DeviceSize> compactSizes(indices.size());
	ASSERT_VK(backend->GetPDevice()->getQueryPoolResults(queryPool, 0, static_cast<uint32_t>(compactSizes.size()), compactSizes.size(),
		compactSizes.data(), sizeof(vk::DeviceSize), vk::QueryResultFlagBits::eWait), "Failed to get query pool results");

	for (uint32_t index : indices)
	{
		buildAs[index]._cleanupAs = buildAs[index]._as;
		buildAs[index]._sizesInfo.accelerationStructureSize = compactSizes[queryCount++];

		vk::AccelerationStructureCreateInfoKHR accelCreateInfo;
		accelCreateInfo.size = buildAs[index]._sizesInfo.accelerationStructureSize;
		accelCreateInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		buildAs[index]._as = allocateAndBindAccel(accelCreateInfo);

		// copy original BLAS into its compact version
		vk::CopyAccelerationStructureInfoKHR copyInfo;
		copyInfo.src = buildAs[index]._geometryInfo.dstAccelerationStructure;
		copyInfo.dst = buildAs[index]._as._structure;
		copyInfo.mode = vk::CopyAccelerationStructureModeKHR::eCompact;

		cmd.copyAccelerationStructureKHR(copyInfo);
	}
}


void RenderBackendRTUtils::destroyNonCompacted(std::vector<uint32_t>& indices, std::vector<AccelerationStructureBuild>& buildAs)
{
	auto* backend = Render::Backend::AcquireInstance();

	for (uint32_t i : indices)
	{
		vmaDestroyBuffer(backend->_allocator, buildAs[i]._cleanupAs._buffer, buildAs[i]._cleanupAs._allocation);
	}
}

void RenderBackendRTUtils::buildBlas(Render::System* renderSys, const std::vector<Render::Backend::BLASInput>& inputs,
	vk::BuildAccelerationStructureFlagsKHR flags /* = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace */)
{
	auto* backend = Render::Backend::AcquireInstance();

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
		buildAs[i]._sizesInfo = backend->GetPDevice()->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
			buildAs[i]._geometryInfo, maxPrimCount);

		blasTotalSize += buildAs[i]._sizesInfo.accelerationStructureSize;
		maxScratchSize = std::max(maxScratchSize, buildAs[i]._sizesInfo.buildScratchSize);
		numCompactions += (buildAs[i]._geometryInfo.flags & vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction)
			== vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
	}

	Render::Buffer::CreateInfo scratchInfo = {};
	scratchInfo.allocSize = maxScratchSize;
	scratchInfo.usage = vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer;
	scratchInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	Render::Buffer scratchBuffer = backend->CreateBuffer(scratchInfo);
	vk::DeviceAddress scratchAddress = scratchBuffer.GetDeviceAddress();

	// calculate BLAS size after compaction
	vk::QueryPool queryPool;
	if (numCompactions > 0)
	{
		assert(numCompactions == numBlas);
		vk::QueryPoolCreateInfo qPoolInfo;
		qPoolInfo.queryCount = numBlas;
		qPoolInfo.queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR;
		queryPool = backend->GetPDevice()->createQueryPool(qPoolInfo);
	}

	// batching BLAS creation
	std::vector<uint32_t> blasIndices;
	vk::DeviceSize batchSize = 0;
	vk::DeviceSize batchLimit = 236000000U;
	vk::CommandPool cmdPool = backend->GetUploadContext()._commandPool;

	for (uint32_t i = 0; i < numBlas; ++i)
	{
		blasIndices.push_back(i);
		batchSize += buildAs[i]._sizesInfo.accelerationStructureSize;

		// if over limit, switch to another command buffer
		if (batchSize >= batchLimit || i == numBlas - 1)
		{
			vk::CommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(cmdPool);
			vk::CommandBuffer newCmd = backend->GetPDevice()->allocateCommandBuffers(cmdAllocInfo)[0];
			backend->SubmitCmdImmediately([&](vk::CommandBuffer cmd) {
				cmdCreateBlas(cmd, blasIndices, buildAs, scratchAddress, queryPool);
			}, newCmd);

			if (queryPool)
			{
				vk::CommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(cmdPool);
				vk::CommandBuffer newCmd = backend->GetPDevice()->allocateCommandBuffers(cmdAllocInfo)[0];
				backend->SubmitCmdImmediately([&](vk::CommandBuffer cmd) {
					cmdCompactBlas(cmd, blasIndices, buildAs, queryPool);
				}, newCmd);

				// destroy the non-compacted version of the BLAS
				destroyNonCompacted(blasIndices, buildAs);
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
		backend->_mainDeletionQueue.push_function([=]() {
			backend->GetPDevice()->destroyAccelerationStructureKHR(blas._as._structure);
		});
	}

	backend->GetPDevice()->destroyQueryPool(queryPool);
}

vk::DeviceAddress RenderBackendRTUtils::getBlasDeviceAddress(Render::System* renderSys, uint32_t blasId)
{
	auto* backend = Render::Backend::AcquireInstance();

	ASSERT(static_cast<size_t>(blasId) < renderSys->_bottomLevelASVec.size(), "BLAS id out of range");
	
	vk::AccelerationStructureDeviceAddressInfoKHR addressInfo;
	addressInfo.setAccelerationStructure(renderSys->_bottomLevelASVec[blasId]._structure);

	return backend->GetPDevice()->getAccelerationStructureAddressKHR(addressInfo);
}

void RenderBackendRTUtils::buildTlas(Render::System* renderSys, const std::vector<vk::AccelerationStructureInstanceKHR>& instances, bool update /* = false */,
	vk::BuildAccelerationStructureFlagsKHR flags /* = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace */)
{
	auto* backend = Render::Backend::AcquireInstance();

	// make sure this function is only called once, unless an update is required
	ASSERT(!renderSys->_topLevelAS._structure || update, "This function should only be called on init/update");

	uint32_t instanceCount = static_cast<uint32_t>(instances.size());

	size_t instanceBufferSize = instances.size() * sizeof(vk::AccelerationStructureInstanceKHR);

	Render::Buffer::CreateInfo instBufferInfo = {};
	instBufferInfo.allocSize = instanceBufferSize;
	instBufferInfo.usage = vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eTransferDst;
	instBufferInfo.memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	instBufferInfo.isLifetimeManaged = false;

	Render::Buffer instanceBuffer = backend->CreateBuffer(instBufferInfo);

	backend->UploadBufferImmediately(instanceBuffer, instances.data(), instanceBufferSize);


	vk::DeviceAddress instBufferAddress = instanceBuffer.GetDeviceAddress();

	// make sure instance buffer is copied before TLAS gets built
	vk::MemoryBarrier barrier;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;

	Render::Buffer scratchBuffer;

	backend->SubmitCmdImmediately([&](vk::CommandBuffer cmd) {
		Render::Buffer::MemoryBarrierInfo instanceBufferBarrier;
		instanceBufferBarrier.srcAccess = vk::AccessFlagBits2::eTransferWrite;
		instanceBufferBarrier.dstAccess = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;

		instanceBuffer.MemoryBarrier(cmd, instanceBufferBarrier);

		cmdCreateTlas(renderSys, cmd, instanceCount, instBufferAddress, scratchBuffer,
			flags, update);
	}, backend->GetUploadContext()._commandBuffer);
	
	backend->_mainDeletionQueue.push_function([=]() {
		backend->GetPDevice()->destroyAccelerationStructureKHR(renderSys->_topLevelAS._structure);
	});

	scratchBuffer.DestroyManually();
	instanceBuffer.DestroyManually();
}

void RenderBackendRTUtils::cmdCreateTlas(Render::System* renderSys, vk::CommandBuffer cmd, uint32_t instanceCount, vk::DeviceAddress instBufferAddress,
	Render::Buffer& scratchBuffer, vk::BuildAccelerationStructureFlagsKHR flags, bool update /* = false */, bool motion /* = false */)
{
	auto* backend = Render::Backend::AcquireInstance();

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
	sizeInfo = backend->GetPDevice()->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
		buildInfo, instanceCount);

	vk::AccelerationStructureCreateInfoKHR createInfo;
	createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	createInfo.size = sizeInfo.accelerationStructureSize;

	renderSys->_topLevelAS = allocateAndBindAccel(createInfo);

	Render::Buffer::CreateInfo scratchBufferInfo = {};
	scratchBufferInfo.allocSize = sizeInfo.buildScratchSize;
	scratchBufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	scratchBufferInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;
	scratchBufferInfo.isLifetimeManaged = false;

	scratchBuffer = backend->CreateBuffer(scratchBufferInfo);

	vk::DeviceAddress scratchAddress = scratchBuffer.GetDeviceAddress();

	// finally building TLAS

	buildInfo.dstAccelerationStructure = renderSys->_topLevelAS._structure;
	buildInfo.scratchData.deviceAddress = scratchAddress;
	
	vk::AccelerationStructureBuildRangeInfoKHR offsetInfo;
	offsetInfo.primitiveCount = instanceCount;

	cmd.buildAccelerationStructuresKHR(buildInfo, &offsetInfo);
}