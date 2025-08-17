#pragma once

#include "render_types.h"
#include "render_system.h"

namespace RenderUtil
{
	bool LoadImageFromFile(Render::System* renderSys, const std::string& fileName, Render::Image& outImage,
		bool generateMipmaps = true, vk::Format imageFormat = vk::Format::eR8G8B8A8Srgb);

	bool LoadCubemapFromFiles(Render::System* renderSys, const std::vector<std::string>& files, Render::Image& outImage);
}