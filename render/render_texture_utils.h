#pragma once

#include "render_types.h"
#include "render_system.h"

namespace RenderUtil
{
	bool load_image_from_file(Render::System* renderSys, const std::string& fileName, Render::Image& outImage,
		bool generateMipmaps = true, vk::Format imageFormat = vk::Format::eR8G8B8A8Srgb);

	bool load_cubemap_from_files(Render::System* renderSys, const std::vector<std::string>& files, Render::Image& outImage);
}