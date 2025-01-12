#pragma once

#include "render_types.h"
#include "render_system.h"

namespace RenderUtil
{
	bool load_image_from_file(RenderSystem* renderSys, const char* file, AllocatedImage& outImage,
		bool generateMipmaps = true, vk::Format imageFormat = vk::Format::eR8G8B8A8Srgb);

	bool load_cubemap_from_files(RenderSystem* renderSys, const std::vector<std::string>& files, AllocatedImage& outImage);
	
	void generate_mipmaps(vk::CommandBuffer cmd, vk::Image image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
}