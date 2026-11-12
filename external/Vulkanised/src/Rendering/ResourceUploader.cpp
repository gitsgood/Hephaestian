#include "Vulkanised/Rendering/ResourceUploader.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "Vulkanised/Logger.h"
#include "Vulkanised/utils/VulkanUtils.h"

namespace Vulkanised::Rendering
{

namespace
{
	constexpr VkDeviceSize c_DefaultStagingChunkSizeInBytes{ 16ull * 1024 * 1024 };	// Bigger uploads get a chunk of their own size
	constexpr VkDeviceSize c_StagingOffsetAlignmentInBytes{ 16 };						// Covers every texel size and copy alignment we use

	[[nodiscard]] constexpr VkDeviceSize alignUp(VkDeviceSize Value, VkDeviceSize Alignment) noexcept
	{
		return (Value + Alignment - 1) / Alignment * Alignment;
	}
}

void ResourceUploader::initialise(VkDevice LogicalDevice, VkQueue Queue, uint32_t QueueFamilyIndex, GpuMemoryAllocator& GpuMemory)
{
	m_LogicalDevice = LogicalDevice;
	m_Queue = Queue;
	m_GpuMemory = &GpuMemory;

	VkCommandPoolCreateInfo CommandPoolCreateInfo{};
	CommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	CommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;	// Each batch's buffer is recorded once and freed
	CommandPoolCreateInfo.queueFamilyIndex = QueueFamilyIndex;
	if (vkCreateCommandPool(m_LogicalDevice, &CommandPoolCreateInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the upload command pool");
	}
}

void ResourceUploader::shutdown() noexcept
{
	if (m_LogicalDevice == VK_NULL_HANDLE)
	{
		return;
	}

	// Anything queued but never submitted is dropped (the resources it was meant for are being destroyed anyway)
	if (m_OpenBatch.has_value())
	{
		vkEndCommandBuffer(m_OpenBatch->m_CommandBuffer);
		destroyBatch(m_OpenBatch.value());
		m_OpenBatch.reset();
	}
	releaseFinishedBatches(true);

	vkDestroyCommandPool(m_LogicalDevice, m_CommandPool, nullptr);
	m_CommandPool = VK_NULL_HANDLE;
	m_LogicalDevice = VK_NULL_HANDLE;
}

ResourceUploader::UploadBatch& ResourceUploader::getOrBeginOpenBatch()
{
	if (m_OpenBatch.has_value())
	{
		return m_OpenBatch.value();
	}

	UploadBatch NewBatch{};
	VkCommandBufferAllocateInfo CommandBufferAllocateInfo{};
	CommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	CommandBufferAllocateInfo.commandPool = m_CommandPool;
	CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	CommandBufferAllocateInfo.commandBufferCount = 1;
	if (vkAllocateCommandBuffers(m_LogicalDevice, &CommandBufferAllocateInfo, &NewBatch.m_CommandBuffer) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to allocate an upload command buffer");
	}

	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(NewBatch.m_CommandBuffer, &BeginInfo) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to begin an upload command buffer");
	}

	m_OpenBatch = std::move(NewBatch);
	return m_OpenBatch.value();
}

ResourceUploader::StagingAllocation ResourceUploader::allocateStagingSpace(UploadBatch& Batch, VkDeviceSize SizeInBytes)
{
	// Fits in the current chunk?
	if (!Batch.m_StagingChunks.empty())
	{
		StagingChunk& CurrentChunk = Batch.m_StagingChunks.back();
		const VkDeviceSize AlignedOffset = alignUp(CurrentChunk.m_UsedBytes, c_StagingOffsetAlignmentInBytes);
		if (AlignedOffset + SizeInBytes <= CurrentChunk.m_Buffer.m_SizeInBytes)
		{
			CurrentChunk.m_UsedBytes = AlignedOffset + SizeInBytes;
			return { CurrentChunk.m_Buffer.m_Buffer, AlignedOffset, static_cast<std::byte*>(CurrentChunk.m_Buffer.m_MappedData) + AlignedOffset };
		}
	}

	// A new chunk: the usual size, or exactly as big as this upload if it's bigger than that
	StagingChunk NewChunk{};
	NewChunk.m_Buffer = m_GpuMemory->createBuffer(std::max(SizeInBytes, c_DefaultStagingChunkSizeInBytes), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		GpuMemoryUsage::StagingUpload, "Upload staging");
	NewChunk.m_UsedBytes = SizeInBytes;
	Batch.m_StagingChunks.push_back(NewChunk);
	return { NewChunk.m_Buffer.m_Buffer, 0, NewChunk.m_Buffer.m_MappedData };
}

void ResourceUploader::queueBufferUpload(const void* SourceData, VkDeviceSize SizeInBytes, VkBuffer DestinationBuffer, VkDeviceSize DestinationOffsetInBytes)
{
	if (SizeInBytes == 0)
	{
		return;
	}

	UploadBatch& Batch = getOrBeginOpenBatch();
	const StagingAllocation Staging = allocateStagingSpace(Batch, SizeInBytes);
	std::memcpy(Staging.m_MappedData, SourceData, static_cast<size_t>(SizeInBytes));

	const VkBufferCopy CopyRegion{ Staging.m_OffsetInBytes, DestinationOffsetInBytes, SizeInBytes };
	vkCmdCopyBuffer(Batch.m_CommandBuffer, Staging.m_Buffer, DestinationBuffer, 1, &CopyRegion);

	Batch.m_HasBufferWrites = true;
	++Batch.m_QueuedOperationCount;
	Batch.m_UploadedBytes += SizeInBytes;
}

void ResourceUploader::queueBufferToBufferCopy(VkBuffer SourceBuffer, VkBuffer DestinationBuffer, VkDeviceSize SizeInBytes)
{
	if (SizeInBytes == 0)
	{
		return;
	}

	UploadBatch& Batch = getOrBeginOpenBatch();

	// Earlier copies in this batch may have written the source: make them land before reading it
	if (Batch.m_HasBufferWrites)
	{
		VkMemoryBarrier2 EarlierWritesBarrier{};
		EarlierWritesBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		EarlierWritesBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		EarlierWritesBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		EarlierWritesBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		EarlierWritesBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
		VkDependencyInfo DependencyInfo{};
		DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		DependencyInfo.memoryBarrierCount = 1;
		DependencyInfo.pMemoryBarriers = &EarlierWritesBarrier;
		vkCmdPipelineBarrier2(Batch.m_CommandBuffer, &DependencyInfo);
	}

	const VkBufferCopy CopyRegion{ 0, 0, SizeInBytes };
	vkCmdCopyBuffer(Batch.m_CommandBuffer, SourceBuffer, DestinationBuffer, 1, &CopyRegion);
	Batch.m_HasBufferWrites = true;
	++Batch.m_QueuedOperationCount;
}

void ResourceUploader::queueTextureUpload(const AllocatedImage& DestinationImage, const void* RgbaPixels, VkDeviceSize SizeInBytes)
{
	UploadBatch& Batch = getOrBeginOpenBatch();
	const StagingAllocation Staging = allocateStagingSpace(Batch, SizeInBytes);
	std::memcpy(Staging.m_MappedData, RgbaPixels, static_cast<size_t>(SizeInBytes));

	const VkCommandBuffer CommandBuffer = Batch.m_CommandBuffer;
	const VkImage Image = DestinationImage.m_Image;
	const uint32_t MipLevelCount = DestinationImage.m_MipLevelCount;

	// 1. Every mip level ready to be written
	Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
		.m_Image = Image, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.m_OldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .m_NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_NONE, .m_AccessesThatMustFinish = VK_ACCESS_2_NONE,
		.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, .m_AccessesThatMustWait = VK_ACCESS_2_TRANSFER_WRITE_BIT });

	// 2. The pixels into mip 0
	VkBufferImageCopy CopyRegion{};
	CopyRegion.bufferOffset = Staging.m_OffsetInBytes;
	CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	CopyRegion.imageExtent = { DestinationImage.m_Extent.width, DestinationImage.m_Extent.height, 1 };
	vkCmdCopyBufferToImage(CommandBuffer, Staging.m_Buffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &CopyRegion);

	// 3. Each further mip is the previous one shrunk by half (linear filtering): read level N-1, write level N
	int32_t SourceWidth = static_cast<int32_t>(DestinationImage.m_Extent.width);
	int32_t SourceHeight = static_cast<int32_t>(DestinationImage.m_Extent.height);
	for (uint32_t CurrentMipLevel{ 1 }; CurrentMipLevel < MipLevelCount; ++CurrentMipLevel)
	{
		Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
			.m_Image = Image, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.m_OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .m_NewLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_BLIT_BIT, .m_AccessesThatMustWait = VK_ACCESS_2_TRANSFER_READ_BIT,
			.m_FirstMipLevel = CurrentMipLevel - 1, .m_MipLevelCount = 1 });

		const int32_t DestinationWidth = std::max(SourceWidth / 2, 1);
		const int32_t DestinationHeight = std::max(SourceHeight / 2, 1);
		VkImageBlit2 BlitRegion{};
		BlitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
		BlitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, CurrentMipLevel - 1, 0, 1 };
		BlitRegion.srcOffsets[1] = { SourceWidth, SourceHeight, 1 };
		BlitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, CurrentMipLevel, 0, 1 };
		BlitRegion.dstOffsets[1] = { DestinationWidth, DestinationHeight, 1 };

		VkBlitImageInfo2 BlitInfo{};
		BlitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
		BlitInfo.srcImage = Image;
		BlitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		BlitInfo.dstImage = Image;
		BlitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		BlitInfo.regionCount = 1;
		BlitInfo.pRegions = &BlitRegion;
		BlitInfo.filter = VK_FILTER_LINEAR;
		vkCmdBlitImage2(CommandBuffer, &BlitInfo);

		SourceWidth = DestinationWidth;
		SourceHeight = DestinationHeight;
	}

	// 4. Everything readable by the fragment shaders: the levels that were blit sources are TRANSFER_SRC, the last one TRANSFER_DST
	if (MipLevelCount > 1)
	{
		Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
			.m_Image = Image, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.m_OldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .m_NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_BLIT_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_NONE,
			.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .m_AccessesThatMustWait = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			.m_FirstMipLevel = 0, .m_MipLevelCount = MipLevelCount - 1 });
	}
	Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
		.m_Image = Image, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.m_OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .m_NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_TRANSFER_WRITE_BIT,
		.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .m_AccessesThatMustWait = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
		.m_FirstMipLevel = MipLevelCount - 1, .m_MipLevelCount = 1 });

	++Batch.m_QueuedOperationCount;
	Batch.m_UploadedBytes += SizeInBytes;
}

void ResourceUploader::submitPendingUploads()
{
	releaseFinishedBatches(false);	// Cheap fence checks: free the staging memory of batches the GPU is done with
	if (!m_OpenBatch.has_value())
	{
		return;
	}

	UploadBatch& Batch = m_OpenBatch.value();

	// Buffer writes (geometry, grown buffers) become visible to every later use of them in one go
	if (Batch.m_HasBufferWrites)
	{
		VkMemoryBarrier2 BufferWritesBarrier{};
		BufferWritesBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		BufferWritesBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		BufferWritesBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		BufferWritesBarrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT
			| VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
		BufferWritesBarrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT
			| VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
		VkDependencyInfo DependencyInfo{};
		DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		DependencyInfo.memoryBarrierCount = 1;
		DependencyInfo.pMemoryBarriers = &BufferWritesBarrier;
		vkCmdPipelineBarrier2(Batch.m_CommandBuffer, &DependencyInfo);
	}

	if (vkEndCommandBuffer(Batch.m_CommandBuffer) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to end an upload command buffer");
	}

	// Staging memory is CPU-written: make sure the GPU sees it (free on coherent memory)
	for (const StagingChunk& CurrentChunk : Batch.m_StagingChunks)
	{
		m_GpuMemory->flushBufferWrites(CurrentChunk.m_Buffer, 0, CurrentChunk.m_UsedBytes);
	}

	VkFenceCreateInfo FenceCreateInfo{};
	FenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vkCreateFence(m_LogicalDevice, &FenceCreateInfo, nullptr, &Batch.m_CompletionFence) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create an upload fence");
	}

	VkCommandBufferSubmitInfo CommandBufferSubmitInfo{};
	CommandBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	CommandBufferSubmitInfo.commandBuffer = Batch.m_CommandBuffer;
	VkSubmitInfo2 SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	SubmitInfo.commandBufferInfoCount = 1;
	SubmitInfo.pCommandBufferInfos = &CommandBufferSubmitInfo;
	if (vkQueueSubmit2(m_Queue, 1, &SubmitInfo, Batch.m_CompletionFence) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to submit an upload batch");
	}

	LOG("Uploaded {} resources ({:.2f} MB) in one batch.", Batch.m_QueuedOperationCount, static_cast<double>(Batch.m_UploadedBytes) / (1024.0 * 1024.0));
	m_SubmittedBatches.push_back(std::move(Batch));
	m_OpenBatch.reset();
}

void ResourceUploader::submitPendingUploadsAndWait()
{
	submitPendingUploads();
	releaseFinishedBatches(true);
}

void ResourceUploader::releaseFinishedBatches(bool ShouldWaitForAll)
{
	std::erase_if(m_SubmittedBatches, [this, ShouldWaitForAll](UploadBatch& CurrentBatch)
	{
		if (ShouldWaitForAll)
		{
			vkWaitForFences(m_LogicalDevice, 1, &CurrentBatch.m_CompletionFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		}
		else if (vkGetFenceStatus(m_LogicalDevice, CurrentBatch.m_CompletionFence) != VK_SUCCESS)
		{
			return false;	// Still on the GPU
		}
		destroyBatch(CurrentBatch);
		return true;
	});
}

void ResourceUploader::destroyBatch(UploadBatch& Batch) noexcept
{
	for (StagingChunk& CurrentChunk : Batch.m_StagingChunks)
	{
		m_GpuMemory->destroyBuffer(CurrentChunk.m_Buffer);
	}
	Batch.m_StagingChunks.clear();
	if (Batch.m_CompletionFence != VK_NULL_HANDLE)
	{
		vkDestroyFence(m_LogicalDevice, Batch.m_CompletionFence, nullptr);
	}
	vkFreeCommandBuffers(m_LogicalDevice, m_CommandPool, 1, &Batch.m_CommandBuffer);
	Batch = {};
}

}
