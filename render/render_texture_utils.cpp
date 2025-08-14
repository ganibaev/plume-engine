#include "render_texture_utils.h"
#include "render_core.h"
#include <iostream>

#include "render_initializers.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


bool RenderUtil::load_image_from_file(Render::System* renderSys, const std::string& fileName, Render::Image& outImage,
	bool generateMipmaps/* = true */, vk::Format imageFormat/* = vk::Format::eR8G8B8A8Srgb */)
{
	int texWidth, texHeight, texChannels;

	stbi_uc* pixels = stbi_load(fileName.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		ASSERT(false, "Failed to load texture file");
		std::cout << fileName << std::endl;
		return false;
	}

	void* pixel_ptr = pixels;

	vk::DeviceSize imageSize = static_cast<uint64_t>(texWidth) * texHeight * 4;

	std::vector<float> pixBuffer(imageSize);
	for (size_t i = 0; i < imageSize; ++i)
	{
		pixBuffer[i] = pixels[i] / 255.0f;
	}

	switch (imageFormat)
	{
	case vk::Format::eR32G32B32A32Sfloat:
		pixel_ptr = pixBuffer.data();
		imageSize *= 4;
		break;
	default:
		break;
	}

	auto* backend = Render::Backend::AcquireInstance();

	// hold texture data
	Render::Buffer::CreateInfo stagingInfo = {};
	stagingInfo.allocSize = imageSize;
	stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
	stagingInfo.memUsage = VMA_MEMORY_USAGE_CPU_ONLY;

	Render::Buffer stagingBuffer = backend->CreateBuffer(stagingInfo);

	backend->CopyDataToBuffer(pixel_ptr, imageSize, stagingBuffer);

	stbi_image_free(pixels);

	vk::Extent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(texWidth);
	imageExtent.height = static_cast<uint32_t>(texHeight);
	imageExtent.depth = 1;

	Render::Image::CreateInfo loadedImageInfo = {};
	loadedImageInfo.format = imageFormat;
	loadedImageInfo.usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
	loadedImageInfo.extent = imageExtent;
	loadedImageInfo.mipLevels = generateMipmaps ? static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1 : 1;
	loadedImageInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	outImage = backend->CreateImage(loadedImageInfo);

	backend->SubmitCmdImmediately([&](vk::CommandBuffer cmd)
	{
		Render::Image::TransitionInfo outImageTransitionInfo = {};
		outImageTransitionInfo.newLayout = vk::ImageLayout::eTransferDstOptimal;
		outImageTransitionInfo.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		outImageTransitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		outImageTransitionInfo.dstStageMask = vk::PipelineStageFlagBits::eTransfer;

		outImage.LayoutTransition(cmd, outImageTransitionInfo);

		backend->CopyBufferToImage(cmd, stagingBuffer, outImage);

		vk::FormatProperties formatProperties = backend->GetFormatProperties(imageFormat);
		
		if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) || !generateMipmaps)
		{
			// change layout to shader read optimal
			Render::Image::TransitionInfo transitionToReadable = {};
			transitionToReadable.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			transitionToReadable.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			transitionToReadable.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			transitionToReadable.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			transitionToReadable.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
			transitionToReadable.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;

			outImage.LayoutTransition(cmd, transitionToReadable);
		}
		else
		{
			outImage.GenerateMipmaps(cmd);
		}

	}, backend->GetUploadContext()._commandBuffer);

	std::cout << "Texture from " << fileName << " loaded successfully" << std::endl;

	return true;
}


bool RenderUtil::load_cubemap_from_files(Render::System* renderSys, const std::vector<std::string>& files,
	Render::Image& outImage)
{
	vk::DeviceSize imageSize = 0;

	uint8_t* pCurPixel = nullptr;

	const std::string& first = files.front();

	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(first.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		std::cout << "Failed to load texture file " << first << std::endl;
		return false;
	}

	vk::DeviceSize subimageSize = static_cast<uint64_t>(texWidth) * texHeight * 4;

	pCurPixel = pixels;

	imageSize += subimageSize;

	std::vector<uint8_t> images(subimageSize * 6);

	// copy first image into the vector
	std::memcpy(images.data(), pCurPixel, subimageSize);

	for (size_t i = 1; i < files.size(); ++i)
	{
		stbi_image_free(pixels);

		pixels = stbi_load(files[i].data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

		if (!pixels)
		{
			std::cout << "Failed to load texture file " << files[i] << std::endl;
			return false;
		}

		pCurPixel = pixels;

		subimageSize = static_cast<uint64_t>(texWidth) * texHeight * 4;
		
		// fill the rest of the vector
		std::memcpy(&images[imageSize], pCurPixel, subimageSize);

		imageSize += subimageSize;
	}
	
	// reset pointer to front of the vector
	void* pPixel = images.data();

	// matching format
	vk::Format imageFormat = vk::Format::eR8G8B8A8Srgb;

	auto* backend = Render::Backend::AcquireInstance();

	// hold texture data
	Render::Buffer::CreateInfo stagingInfo = {};
	stagingInfo.allocSize = imageSize;
	stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
	stagingInfo.memUsage = VMA_MEMORY_USAGE_CPU_ONLY;

	Render::Buffer stagingBuffer = backend->CreateBuffer(stagingInfo);

	backend->CopyDataToBuffer(pPixel, imageSize, stagingBuffer);

	stbi_image_free(pixels);

	vk::Extent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(texWidth);
	imageExtent.height = static_cast<uint32_t>(texHeight);
	imageExtent.depth = 1;

	Render::Image::CreateInfo cubemapInfo = {};
	cubemapInfo.format = imageFormat;
	cubemapInfo.usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
	cubemapInfo.extent = imageExtent;
	cubemapInfo.type = Render::Image::Type::eCubemap;
	cubemapInfo.memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	outImage = backend->CreateImage(cubemapInfo);

	backend->SubmitCmdImmediately([&](vk::CommandBuffer cmd) {
		Render::Image::TransitionInfo outImageTransitionInfo = {};
		outImageTransitionInfo.newLayout = vk::ImageLayout::eTransferDstOptimal;
		outImageTransitionInfo.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		outImageTransitionInfo.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
		outImageTransitionInfo.dstStageMask = vk::PipelineStageFlagBits::eTransfer;

		outImage.LayoutTransition(cmd, outImageTransitionInfo);

		std::vector<vk::BufferImageCopy> bufferCopyRegions;

		uint32_t face = 0;
		for (size_t offset = 0; offset < images.size(); offset += subimageSize)
		{
			vk::BufferImageCopy copyRegion = {};
			copyRegion.bufferOffset = offset;
			copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = face;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent = imageExtent;
			bufferCopyRegions.push_back(copyRegion);

			++face;
		}

		backend->CopyBufferRegionsToImage(cmd, stagingBuffer, outImage, bufferCopyRegions);

		// change layout to shader read optimal
		Render::Image::TransitionInfo transitionToReadable = {};
		transitionToReadable.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		transitionToReadable.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		transitionToReadable.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		transitionToReadable.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		transitionToReadable.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
		transitionToReadable.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;

		outImage.LayoutTransition(cmd, transitionToReadable);

	}, backend->GetUploadContext()._commandBuffer);

	std::cout << "Cubemap texture loaded successfully" << std::endl;

	return true;
}