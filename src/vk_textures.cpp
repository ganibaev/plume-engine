#include "vk_textures.h"
#include <iostream>

#include "vk_initializers.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool vkutil::load_image_from_file(VulkanEngine* engine, const char* file, AllocatedImage& outImage,
	bool generateMipmaps/* = true */, vk::Format imageFormat/* = vk::Format::eR8G8B8A8Srgb */)
{
	int texWidth, texHeight, texChannels;

	stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		std::cout << "Failed to load texture file " << file << std::endl;
		return false;
	}

	void* pixel_ptr = pixels;
	
	vk::DeviceSize imageSize = static_cast<uint64_t>(texWidth) * texHeight * 4;

	outImage._mipLevels = generateMipmaps ? static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1 : 1;

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

	// hold texture data
	AllocatedBuffer stagingBuffer = engine->create_buffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
		VMA_MEMORY_USAGE_CPU_ONLY);

	// copy data to buffer
	void* data;
	vmaMapMemory(engine->_allocator, stagingBuffer._allocation, &data);

	memcpy(data, pixel_ptr, static_cast<size_t>(imageSize));
	
	vmaUnmapMemory(engine->_allocator, stagingBuffer._allocation);
	
	stbi_image_free(pixels);

	vk::Extent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(texWidth);
	imageExtent.height = static_cast<uint32_t>(texHeight);
	imageExtent.depth = 1;

	vk::ImageCreateInfo imageInfo = vkinit::image_create_info(imageFormat, vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, imageExtent, outImage._mipLevels);
	
	AllocatedImage newImage;

	newImage._mipLevels = outImage._mipLevels;

	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImageCreateInfo imageInfoC = static_cast<VkImageCreateInfo>(imageInfo);
	VkImage imageC = {};

	vmaCreateImage(engine->_allocator, &imageInfoC, &imgAllocInfo, &imageC, &newImage._allocation, nullptr);

	newImage._image = imageC;

	engine->immediate_submit([&](vk::CommandBuffer cmd) {
		vk::ImageSubresourceRange range;
		range.aspectMask = vk::ImageAspectFlagBits::eColor;
		range.baseMipLevel = 0;
		range.levelCount = outImage._mipLevels;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		vk::ImageMemoryBarrier imageBarrierToTransfer = {};
		
		imageBarrierToTransfer.oldLayout = vk::ImageLayout::eUndefined;
		imageBarrierToTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
		imageBarrierToTransfer.image = newImage._image;
		imageBarrierToTransfer.subresourceRange = range;
		
		imageBarrierToTransfer.srcAccessMask = vk::AccessFlagBits::eNone;
		imageBarrierToTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		
		// use barrier to prepare for writing image
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, imageBarrierToTransfer);

		vk::BufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = imageExtent;
		
		// copy the buffer
		cmd.copyBufferToImage(stagingBuffer._buffer, newImage._image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

		vk::FormatProperties formatProperties = engine->_chosenGPU.getFormatProperties(imageFormat);
		
		if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) || !generateMipmaps)
		{
			outImage._mipLevels = 1;
			// change layout to shader read optimal
			vk::ImageMemoryBarrier imageBarrierToReadable = imageBarrierToTransfer;
			
			imageBarrierToReadable.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			imageBarrierToReadable.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			
			imageBarrierToReadable.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			imageBarrierToReadable.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, imageBarrierToReadable);
		}
		else
		{
			generate_mipmaps(cmd, newImage._image, texWidth, texHeight, newImage._mipLevels);
		}

	}, engine->_uploadContext._commandBuffer);

	engine->_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(engine->_allocator, newImage._image, newImage._allocation);
	});

	vmaDestroyBuffer(engine->_allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	std::cout << "Texture from " << file << " loaded successfully" << std::endl;

	outImage = newImage;
	return true;
}

bool vkutil::load_cubemap_from_files(VulkanEngine* engine, const std::vector<std::string>& files,
	AllocatedImage& outImage)
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

	// hold texture data
	AllocatedBuffer stagingBuffer = engine->create_buffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
		VMA_MEMORY_USAGE_CPU_ONLY);

	// copy data to buffer
	void* data;
	vmaMapMemory(engine->_allocator, stagingBuffer._allocation, &data);

	std::memcpy(data, pPixel, static_cast<size_t>(imageSize));

	vmaUnmapMemory(engine->_allocator, stagingBuffer._allocation);

	stbi_image_free(pixels);

	vk::Extent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(texWidth);
	imageExtent.height = static_cast<uint32_t>(texHeight);
	imageExtent.depth = 1;

	vk::ImageCreateInfo imageInfo = vkinit::image_create_info(imageFormat, vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, imageExtent, outImage._mipLevels,
		vk::SampleCountFlagBits::e1, ImageType::eCubemap);

	AllocatedImage newImage;

	newImage._mipLevels = outImage._mipLevels;

	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImageCreateInfo imageInfoC = static_cast<VkImageCreateInfo>(imageInfo);
	VkImage imageC = {};

	vmaCreateImage(engine->_allocator, &imageInfoC, &imgAllocInfo, &imageC, &newImage._allocation, nullptr);

	newImage._image = imageC;

	engine->immediate_submit([&](vk::CommandBuffer cmd) {
		vk::ImageSubresourceRange range;
		range.aspectMask = vk::ImageAspectFlagBits::eColor;
		range.baseMipLevel = 0;
		range.levelCount = outImage._mipLevels;
		range.baseArrayLayer = 0;
		range.layerCount = 6;

		vk::ImageMemoryBarrier imageBarrierToTransfer = {};

		imageBarrierToTransfer.oldLayout = vk::ImageLayout::eUndefined;
		imageBarrierToTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
		imageBarrierToTransfer.image = newImage._image;
		imageBarrierToTransfer.subresourceRange = range;

		imageBarrierToTransfer.srcAccessMask = vk::AccessFlagBits::eNone;
		imageBarrierToTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

		// use barrier to prepare for writing image
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, imageBarrierToTransfer);
		
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
		
		// copy the buffer
		cmd.copyBufferToImage(stagingBuffer._buffer, newImage._image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegions);

		vk::FormatProperties formatProperties = engine->_chosenGPU.getFormatProperties(imageFormat);

		outImage._mipLevels = 1;
		// change layout to shader read optimal
		vk::ImageMemoryBarrier imageBarrierToReadable = imageBarrierToTransfer;

		imageBarrierToReadable.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		imageBarrierToReadable.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		imageBarrierToReadable.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		imageBarrierToReadable.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, imageBarrierToReadable);

	}, engine->_uploadContext._commandBuffer);

	engine->_mainDeletionQueue.push_function([=]() {
		vmaDestroyImage(engine->_allocator, newImage._image, newImage._allocation);
		});

	vmaDestroyBuffer(engine->_allocator, stagingBuffer._buffer, stagingBuffer._allocation);

	std::cout << "Cubemap texture loaded successfully" << std::endl;

	outImage = newImage;
	return true;
}

void vkutil::generate_mipmaps(vk::CommandBuffer cmd, vk::Image image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
{
	vk::ImageMemoryBarrier barrier = {};

	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	
	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;

	for (uint32_t i = 1; i < mipLevels; ++i)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
		
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);
		
		vk::ImageBlit blit = {};

		blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
		blit.srcOffsets[1] = vk::Offset3D{ mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
		blit.dstOffsets[1] = vk::Offset3D{ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		
		cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);

		if (mipWidth > 1)
		{
			mipWidth /= 2;
		}
		if (mipHeight > 1)
		{
			mipHeight /= 2;
		}
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);
}