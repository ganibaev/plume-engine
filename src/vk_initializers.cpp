#include <vk_initializers.h>

VkCommandPoolCreateInfo vkinit::command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags /* = 0 */)
{
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;

	// this command pool will submit graphics commands
	commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
	// allow the pool to reset individual command buffers
	commandPoolInfo.flags = flags;
	return commandPoolInfo;
}

VkCommandBufferAllocateInfo vkinit::command_buffer_allocate_info(VkCommandPool pool, uint32_t count /* = 1 */, VkCommandBufferLevel level /* = VK_COMMAND_BUFFER_LEVEL_PRIMARY */)
{
	VkCommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;

	// specify the command pool
	cmdAllocInfo.commandPool = pool;
	// allocate one command buffer
	cmdAllocInfo.commandBufferCount = count;
	// command buffer level is primary
	cmdAllocInfo.level = level;
	return cmdAllocInfo;
}