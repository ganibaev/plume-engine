#include "render_cfg.h"
#include "render_descriptors.h"
#include "render_initializers.h"


void Render::DescriptorManager::Init(vk::Device* pDevice, DeletionQueue* pDeletionQueue)
{
	_pDevice = pDevice;
	_pDeletionQueue = pDeletionQueue;
	_setQueue.resize(NUM_DESCRIPTOR_SETS);

	for (auto& set : _setQueue)
	{
		set.writes.reserve(MAX_BINDING_SLOTS_PER_SET * FRAME_OVERLAP);
		set.imageInfos.reserve(MAX_BINDING_SLOTS_PER_SET * FRAME_OVERLAP);
		set.bufferInfos.reserve(MAX_BINDING_SLOTS_PER_SET * FRAME_OVERLAP);
	}
}

void Render::DescriptorManager::AllocateSets()
{
	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{ vk::DescriptorType::eUniformBuffer, 10 },
		{ vk::DescriptorType::eUniformBufferDynamic, 10 },
		{ vk::DescriptorType::eStorageBuffer, 10 },
		{ vk::DescriptorType::eStorageImage, 10 },
		{ vk::DescriptorType::eCombinedImageSampler, 500 },
		{ vk::DescriptorType::eAccelerationStructureKHR, 10 }
	};

	vk::DescriptorPoolCreateInfo poolInfo = {};
	poolInfo.maxSets = 600;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	_pool = _pDevice->createDescriptorPool(poolInfo);

	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = _pool;
	allocateInfo.descriptorSetCount = _setQueue.size();

	// TODO: bulk allocate all sets
	for (size_t i = 0; i < _setQueue.size(); ++i)
	{
		DescriptorSetInfo& set = _setQueue[i];

		if (set.hasBindless)
		{
			vk::StructureChain<vk::DescriptorSetLayoutCreateInfo, vk::DescriptorSetLayoutBindingFlagsCreateInfo> c;

			auto& texSetInfo = c.get<vk::DescriptorSetLayoutCreateInfo>();
			texSetInfo.setBindings(set.layoutBindings);

			// Allow for usage of unwritten descriptor sets and variable size arrays of textures
			vk::DescriptorBindingFlags bindFlags = vk::DescriptorBindingFlagBits::ePartiallyBound |
				vk::DescriptorBindingFlagBits::eVariableDescriptorCount;

			auto& bindFlagsInfo = c.get<vk::DescriptorSetLayoutBindingFlagsCreateInfo>();
			bindFlagsInfo.setBindingFlags(bindFlags);

			set.setLayout = _pDevice->createDescriptorSetLayout(texSetInfo);
		}
		else
		{
			vk::DescriptorSetLayoutCreateInfo layoutInfo;
			layoutInfo.setBindings(set.layoutBindings);

			set.setLayout = _pDevice->createDescriptorSetLayout(layoutInfo);
		}

		_pDeletionQueue->PushFunction([=]() {
			_pDevice->destroyDescriptorSetLayout(set.setLayout);
		});

		if (set.isPerFrame)
		{
			std::vector<vk::DescriptorSetLayout> layouts(FRAME_OVERLAP);

			for (int8_t i = 0; i < FRAME_OVERLAP; ++i)
			{
				layouts[i] = set.setLayout;
			}

			vk::DescriptorSetAllocateInfo setAllocInfo = {};
			setAllocInfo.descriptorPool = _pool;
			setAllocInfo.setSetLayouts(layouts);

			auto tmpSets = _pDevice->allocateDescriptorSets(setAllocInfo);

			for (size_t s = 0; s < FRAME_OVERLAP; ++s)
			{
				set.sets[s] = tmpSets[s];
			}
		}
		else
		{
			if (set.hasBindless)
			{
				std::vector<uint32_t> variableDescCounts;
				variableDescCounts.reserve(set.layoutBindings.size());

				for (auto& binding : set.layoutBindings)
				{
					variableDescCounts.push_back(binding.descriptorCount);
				}

				vk::StructureChain<vk::DescriptorSetAllocateInfo, vk::DescriptorSetVariableDescriptorCountAllocateInfo> descCntC;

				auto& variableDescriptorCountAllocInfo =
					descCntC.get<vk::DescriptorSetVariableDescriptorCountAllocateInfo>();
				variableDescriptorCountAllocInfo.setDescriptorCounts(variableDescCounts);

				auto& allocInfo = descCntC.get<vk::DescriptorSetAllocateInfo>();
				allocInfo.setDescriptorPool(_pool);
				allocInfo.setSetLayouts(set.setLayout);

				set.sets[0] = _pDevice->allocateDescriptorSets(allocInfo)[0];
			}
			else
			{
				vk::DescriptorSetAllocateInfo setAllocInfo = {};
				setAllocInfo.descriptorPool = _pool;
				setAllocInfo.setSetLayouts(set.setLayout);

				set.sets[0] = _pDevice->allocateDescriptorSets(setAllocInfo)[0];
			}
		}

		for (uint32_t w = 0; w < set.writes.size(); ++w)
		{
			set.writes[w].dstSet = *(set.writeToSetsAt[w]);
		}
	}

	_pDeletionQueue->PushFunction([=]() {
		_pDevice->destroyDescriptorPool(_pool);
	});
}

void Render::DescriptorManager::UpdateSets()
{
	std::vector<vk::WriteDescriptorSet> bulkWrites;
	bulkWrites.reserve(_setQueue.size() * FRAME_OVERLAP);

	for (size_t i = 0; i < _setQueue.size(); ++i)
	{
		DescriptorSetInfo& set = _setQueue[i];
		for (auto& descWrite : set.writes)
		{
			if (descWrite.dstSet)
			{
				bulkWrites.push_back(descWrite);
			}
		}
	}

	_pDevice->updateDescriptorSets(bulkWrites, {});
}

void Render::DescriptorManager::RegisterBuffer(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
	const std::vector<BufferInfo>& bufferInfos, uint32_t binding, uint32_t numDescs /* = 1 */, bool isPerFrame /* = false */)
{
	DescriptorSetInfo& set = _setQueue[static_cast<uint16_t>(descriptorSetType)];

	if (isPerFrame)
	{
		set.isPerFrame = true;
		for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
		{
			vk::DescriptorBufferInfo bufferInfo = {};

			bufferInfo.buffer = bufferInfos[i].buffer;
			bufferInfo.offset = bufferInfos[i].offset;
			bufferInfo.range = bufferInfos[i].range;

			set.bufferInfos.push_back(bufferInfo);

			vk::WriteDescriptorSet bufferWrite = vkinit::WriteDescriptorBuffer(
				bufferInfos[i].bufferType, set.sets[i], &(set.bufferInfos.back()), binding);

			set.writes.push_back(bufferWrite);
			set.writeToSetsAt.push_back(&(set.sets[i]));
		}
	}
	else
	{
		vk::DescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = bufferInfos[0].buffer;
		bufferInfo.offset = bufferInfos[0].offset;
		bufferInfo.range = bufferInfos[0].range;

		set.bufferInfos.push_back(bufferInfo);

		vk::WriteDescriptorSet bufferWrite = vkinit::WriteDescriptorBuffer(
			bufferInfos[0].bufferType, set.sets[0], &(set.bufferInfos.back()), binding);

		set.writes.push_back(bufferWrite);
		set.writeToSetsAt.push_back(&(set.sets[0]));
	}

	if (set.layoutBindings.size() <= binding)
	{
		set.layoutBindings.resize(binding + 1);
	}

	vk::DescriptorSetLayoutBinding setBinding = vkinit::SetLayoutBinding(bufferInfos[0].bufferType,
		shaderStages, binding, numDescs);

	set.layoutBindings[binding] = setBinding;
}

void Render::DescriptorManager::RegisterImage(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
	const std::vector<ImageInfo>& imageInfos, uint32_t binding, uint32_t numDescs /* = 1 */, bool isBindless /* = false */,
	bool isPerFrame /* = false */)
{
	DescriptorSetInfo& set = _setQueue[static_cast<uint16_t>(descriptorSetType)];

	if (isPerFrame)
	{
		set.isPerFrame = true;
		for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
		{
			vk::DescriptorImageInfo newInfo;
			newInfo.imageView = imageInfos[i].imageView;
			newInfo.imageLayout = imageInfos[i].layout;
			newInfo.sampler = imageInfos[i].sampler;

			set.imageInfos.push_back(newInfo);

			vk::WriteDescriptorSet imageWrite = vkinit::WriteDescriptorImage(imageInfos[i].imageType, set.sets[i],
				&(set.imageInfos.back()), binding, numDescs);

			set.writes.push_back(imageWrite);
			set.writeToSetsAt.push_back(&(set.sets[i]));
		}
	}
	else
	{
		if (isBindless)
		{
			set.hasBindless = true;

			vk::DescriptorImageInfo* writeStart = nullptr;
			set.imageInfos.reserve(set.imageInfos.size() + numDescs);
			for (size_t i = 0; i < numDescs; ++i)
			{
				vk::DescriptorImageInfo newInfo;
				newInfo.sampler = imageInfos[i].sampler;
				newInfo.imageLayout = imageInfos[i].layout;
				newInfo.imageView = imageInfos[i].imageView;

				set.imageInfos.push_back(newInfo);
				if (i == 0)
				{
					writeStart = &(set.imageInfos.back());
				}
			}

			vk::WriteDescriptorSet imageWrite = vkinit::WriteDescriptorImage(imageInfos[0].imageType, set.sets[0],
				writeStart, binding, numDescs);

			set.writes.push_back(imageWrite);
			set.writeToSetsAt.push_back(&(set.sets[0]));
		}
		else
		{
			vk::DescriptorImageInfo imageInfo = {};
			imageInfo.imageView = imageInfos[0].imageView;
			imageInfo.imageLayout = imageInfos[0].layout;
			imageInfo.sampler = imageInfos[0].sampler;

			set.imageInfos.push_back(imageInfo);

			vk::WriteDescriptorSet imageWrite = vkinit::WriteDescriptorImage(imageInfos[0].imageType, set.sets[0],
				&(set.imageInfos.back()), binding, numDescs);

			set.writes.push_back(imageWrite);
			set.writeToSetsAt.push_back(&(set.sets[0]));
		}
	}

	if (set.layoutBindings.size() <= binding)
	{
		set.layoutBindings.resize(binding + 1);
	}

	vk::DescriptorSetLayoutBinding setBinding = vkinit::SetLayoutBinding(imageInfos[0].imageType,
		shaderStages, binding, numDescs);

	set.layoutBindings[binding] = setBinding;
}

void Render::DescriptorManager::RegisterAccelStructure(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
	vk::AccelerationStructureKHR accelStructure, uint32_t binding, bool isPerFrame /* = false */)
{
	DescriptorSetInfo& set = _setQueue[static_cast<uint16_t>(descriptorSetType)];

	if (isPerFrame)
	{
		set.isPerFrame = isPerFrame;
		for (uint8_t i = 0; i < FRAME_OVERLAP; ++i)
		{
			set.writes.reserve(set.writes.size() + FRAME_OVERLAP);

			auto initSize = set.accelWrites.size();
			set.accelWrites.resize(initSize + FRAME_OVERLAP);
			set.accels.reserve(set.accelWrites.size() + FRAME_OVERLAP);
			set.accels.push_back(accelStructure);
			set.accelWrites[initSize + i].setAccelerationStructures(set.accels[i]);

			vk::WriteDescriptorSet asDescWrite;
			asDescWrite.dstBinding = binding;
			asDescWrite.dstSet = set.sets[i];
			asDescWrite.descriptorCount = 1;
			asDescWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
			asDescWrite.pNext = &(set.accelWrites.back());

			set.writes.push_back(asDescWrite);
			set.writeToSetsAt.push_back(&(set.sets[i]));
		}
	}
	else
	{
		set.accelWrites.resize(set.accelWrites.size() + 1);
		set.accels.resize(set.accelWrites.size() + 1);
		set.accels[set.accelWrites.size() - 1] = accelStructure;
		set.accelWrites[set.accelWrites.size() - 1].setAccelerationStructures(set.accels[0]);

		vk::WriteDescriptorSet asDescWrite;
		asDescWrite.dstBinding = binding;
		asDescWrite.dstSet = set.sets[0];
		asDescWrite.descriptorCount = 1;
		asDescWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
		asDescWrite.pNext = &(set.accelWrites.back());

		set.writes.push_back(asDescWrite);
		set.writeToSetsAt.push_back(&(set.sets[0]));
	}

	if (set.layoutBindings.size() <= binding)
	{
		set.layoutBindings.resize(binding + 1);
	}

	vk::DescriptorSetLayoutBinding setBinding = vkinit::SetLayoutBinding(vk::DescriptorType::eAccelerationStructureKHR,
		shaderStages, binding);

	set.layoutBindings[binding] = setBinding;
}

std::vector<vk::DescriptorSetLayout> Render::DescriptorManager::GetLayouts(DescriptorSetFlags usedDscMask) const
{
	std::vector<vk::DescriptorSetLayout> layouts;

	uint32_t setId = 0;
	for (uint32_t i = 1; i < (1 << NUM_DESCRIPTOR_SETS); i <<= 1)
	{
		if (usedDscMask & i)
		{
			layouts.push_back(_setQueue[setId].setLayout);
		}
		++setId;
	}

	return layouts;
}

std::vector<vk::DescriptorSet> Render::DescriptorManager::GetDescriptorSets(DescriptorSetFlags usedDscMask, uint8_t perFrameId) const
{
	std::vector<vk::DescriptorSet> sets;

	uint32_t setId = 0;
	for (uint32_t i = 1; i < (1 << NUM_DESCRIPTOR_SETS); i <<= 1)
	{
		if (usedDscMask & i)
		{
			if (_setQueue[setId].isPerFrame)
			{
				sets.push_back(_setQueue[setId].sets[perFrameId]);
			}
			else
			{
				sets.push_back(_setQueue[setId].sets[0]);
			}
		}
		++setId;
	}

	return sets;
}