#pragma once

#include "vk_types.h"
#include "vk_engine.h"

namespace vkutil
{
	bool load_image_from_file(VulkanEngine* engine, const char* file, AllocatedImage& outImage, bool generateMipmaps = true);

	bool load_cubemap_from_files(VulkanEngine* engine, const std::vector<std::string>& files, AllocatedImage& outImage);
	
	void generate_mipmaps(vk::CommandBuffer cmd, vk::Image image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
}