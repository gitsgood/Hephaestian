#ifndef VULKANISED_RENDERING_GPUMEMORY_H
#define VULKANISED_RENDERING_GPUMEMORY_H

#include <cstdint>
#include <string_view>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

/*
* Every buffer and image the renderer creates gets its memory through here, from VMA (Vulkan Memory Allocator).
* VMA packs many resources into a few big device memory blocks, picks the right memory type for what the resource is used
* for, and keeps CPU-written memory mapped for its whole life. Nobody else calls vkAllocateMemory.
*/
namespace Vulkanised::Rendering
{

// Who reads and writes the memory, which decides where VMA puts it
enum class GpuMemoryUsage
{
	GpuOnly,				// Only the GPU touches it (geometry, textures, depth). Filled through a staging buffer.
	CpuWritesGpuReads,		// Rewritten by the CPU every frame (instances, camera). Persistently mapped. Lands in fast
							// GPU memory the CPU can write to directly when the GPU has some (resizable BAR), host memory otherwise.
	StagingUpload,			// Temporary CPU-side copy that a transfer moves into GpuOnly memory. Persistently mapped.
	GpuWritesCpuReads		// Read back by the CPU (screenshots). Persistently mapped, cached for fast CPU reads.
};

struct AllocatedBuffer
{
	VkBuffer m_Buffer{ VK_NULL_HANDLE };
	VmaAllocation m_Allocation{ nullptr };
	VkDeviceSize m_SizeInBytes{ 0 };
	void* m_MappedData{ nullptr };	// Set for every usage except GpuOnly: write/read straight through this pointer

	[[nodiscard]] bool isValid() const noexcept { return m_Buffer != VK_NULL_HANDLE; }
};

struct AllocatedImage
{
	VkImage m_Image{ VK_NULL_HANDLE };
	VkImageView m_ImageView{ VK_NULL_HANDLE };	// Covers every mip level
	VmaAllocation m_Allocation{ nullptr };
	VkFormat m_Format{ VK_FORMAT_UNDEFINED };
	VkExtent2D m_Extent{ 0, 0 };
	uint32_t m_MipLevelCount{ 1 };

	[[nodiscard]] bool isValid() const noexcept { return m_Image != VK_NULL_HANDLE; }
};

// What the debug overlay shows about GPU memory
struct GpuMemoryStatistics
{
	uint64_t m_UsedBytes{ 0 };				// Actually handed out to buffers and images
	uint64_t m_ReservedBytes{ 0 };			// Device memory blocks VMA holds (used + free space inside them)
	uint64_t m_DeviceLocalBudgetBytes{ 0 };	// How much GPU memory the OS lets this process use right now
	uint32_t m_AllocationCount{ 0 };		// Buffers and images
	uint32_t m_DeviceMemoryBlockCount{ 0 };	// Real vkAllocateMemory calls behind them
};

class GpuMemoryAllocator
{
public:
	void initialise(VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice LogicalDevice, uint32_t VulkanApiVersion);
	void shutdown() noexcept;

	[[nodiscard]] AllocatedBuffer createBuffer(VkDeviceSize SizeInBytes, VkBufferUsageFlags BufferUsage, GpuMemoryUsage MemoryUsage, std::string_view DebugName);
	void destroyBuffer(AllocatedBuffer& BufferToDestroy) noexcept;

	// Makes CPU writes visible to the GPU / GPU writes visible to the CPU. Free (VMA skips it) on coherent memory, which is
	// most memory on desktop GPUs, but required by the spec whenever the memory type isn't coherent.
	void flushBufferWrites(const AllocatedBuffer& WrittenBuffer, VkDeviceSize OffsetInBytes = 0, VkDeviceSize SizeInBytes = VK_WHOLE_SIZE) const;
	void invalidateBufferForReading(const AllocatedBuffer& BufferToRead, VkDeviceSize OffsetInBytes = 0, VkDeviceSize SizeInBytes = VK_WHOLE_SIZE) const;

	// Creates the image in GPU-only memory, plus a view covering all of its mip levels
	[[nodiscard]] AllocatedImage createImage(const VkImageCreateInfo& ImageCreateInfo, VkImageAspectFlags ViewAspect, std::string_view DebugName);
	void destroyImage(AllocatedImage& ImageToDestroy) noexcept;

	[[nodiscard]] GpuMemoryStatistics getStatistics() const;
	[[nodiscard]] VmaAllocator getAllocator() const noexcept { return m_Allocator; }

private:
	VmaAllocator m_Allocator{ nullptr };
	VkDevice m_LogicalDevice{ VK_NULL_HANDLE };
};

}

#endif // !VULKANISED_RENDERING_GPUMEMORY_H
