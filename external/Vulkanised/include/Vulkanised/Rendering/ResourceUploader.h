#ifndef VULKANISED_RENDERING_RESOURCEUPLOADER_H
#define VULKANISED_RENDERING_RESOURCEUPLOADER_H

#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include "Vulkanised/Rendering/GpuMemory.h"

namespace Vulkanised::Rendering
{

/*
* Gets data into GPU-only memory (geometry, textures) in BATCHES.
*
* Each queue* call copies the data into staging memory right away (so the caller can free its copy) and records the GPU copy
* into one shared command buffer. Everything queued is then submitted together, once per frame, WITHOUT the CPU waiting:
* the frame is submitted after the batch on the same queue, and the batch ends with barriers that make its writes visible
* to everything submitted after it. A batch's staging memory is freed once its fence says the GPU is done with it.
*
* (Every mesh used to cost 2 staging allocations, 2 submits and 2 vkQueueWaitIdle, and every texture 3 submits and 3 waits.)
*/
class ResourceUploader
{
public:
	void initialise(VkDevice LogicalDevice, VkQueue Queue, uint32_t QueueFamilyIndex, GpuMemoryAllocator& GpuMemory);
	void shutdown() noexcept;	// Waits for anything still uploading

	// Copies SizeInBytes from SourceData now; the GPU writes them at DestinationOffsetInBytes of DestinationBuffer when submitted
	void queueBufferUpload(const void* SourceData, VkDeviceSize SizeInBytes, VkBuffer DestinationBuffer, VkDeviceSize DestinationOffsetInBytes);

	// GPU-side buffer to buffer copy, ordered with everything else in the batch
	void queueBufferToBufferCopy(VkBuffer SourceBuffer, VkBuffer DestinationBuffer, VkDeviceSize SizeInBytes);

	// Fills mip 0 with tightly packed 8-bit RGBA pixels, builds the rest of the mip chain (if the image has one) by repeatedly
	// halving it, and leaves every mip ready to be sampled (SHADER_READ_ONLY_OPTIMAL)
	void queueTextureUpload(const AllocatedImage& DestinationImage, const void* RgbaPixels, VkDeviceSize SizeInBytes);

	// Sends everything queued to the GPU without waiting. Anything submitted to the same queue afterwards sees the results.
	void submitPendingUploads();
	// Same, then blocks until the GPU has finished them (loading screens, or before destroying something they reference)
	void submitPendingUploadsAndWait();

	[[nodiscard]] bool hasPendingUploads() const noexcept { return m_OpenBatch.has_value(); }

private:
	struct StagingChunk
	{
		AllocatedBuffer m_Buffer;
		VkDeviceSize m_UsedBytes{ 0 };
	};

	struct UploadBatch
	{
		VkCommandBuffer m_CommandBuffer{ VK_NULL_HANDLE };
		VkFence m_CompletionFence{ VK_NULL_HANDLE };
		std::vector<StagingChunk> m_StagingChunks;	// Freed once the fence signals
		uint32_t m_QueuedOperationCount{ 0 };
		VkDeviceSize m_UploadedBytes{ 0 };
		bool m_HasBufferWrites{ false };			// Buffers need one barrier at the end of the batch, images get their own
	};

	UploadBatch& getOrBeginOpenBatch();
	// Space for SizeInBytes in the open batch's staging memory. Returns the buffer and the offset to use, and where to write.
	struct StagingAllocation
	{
		VkBuffer m_Buffer{ VK_NULL_HANDLE };
		VkDeviceSize m_OffsetInBytes{ 0 };
		void* m_MappedData{ nullptr };
	};
	StagingAllocation allocateStagingSpace(UploadBatch& Batch, VkDeviceSize SizeInBytes);
	void releaseFinishedBatches(bool ShouldWaitForAll);
	void destroyBatch(UploadBatch& Batch) noexcept;

	VkDevice m_LogicalDevice{ VK_NULL_HANDLE };
	VkQueue m_Queue{ VK_NULL_HANDLE };
	VkCommandPool m_CommandPool{ VK_NULL_HANDLE };
	GpuMemoryAllocator* m_GpuMemory{ nullptr };

	std::optional<UploadBatch> m_OpenBatch;			// Being recorded into
	std::vector<UploadBatch> m_SubmittedBatches;	// On the GPU, waiting for their fence
};

}

#endif // !VULKANISED_RENDERING_RESOURCEUPLOADER_H
