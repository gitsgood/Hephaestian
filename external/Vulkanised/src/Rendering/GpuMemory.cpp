#include "Vulkanised/Rendering/GpuMemory.h"

#include <string>
#include <vector>

#include "Vulkanised/Logger.h"

namespace Vulkanised::Rendering
{

void GpuMemoryAllocator::initialise(VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice LogicalDevice, uint32_t VulkanApiVersion)
{
	VmaAllocatorCreateInfo AllocatorCreateInfo{};
	AllocatorCreateInfo.instance = Instance;
	AllocatorCreateInfo.physicalDevice = PhysicalDevice;
	AllocatorCreateInfo.device = LogicalDevice;
	AllocatorCreateInfo.vulkanApiVersion = VulkanApiVersion;
	AllocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;	// Real budget numbers from the driver (core-promoted, always present on 1.3 desktop drivers)

	if (vmaCreateAllocator(&AllocatorCreateInfo, &m_Allocator) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the GPU memory allocator (VMA)");
	}
	m_LogicalDevice = LogicalDevice;
}

void GpuMemoryAllocator::shutdown() noexcept
{
	if (m_Allocator == nullptr)
	{
		return;
	}

	// Anything still alive here is a leak: VMA asserts on it in Debug, so say which ones first
	const GpuMemoryStatistics StatisticsAtShutdown = getStatistics();
	if (StatisticsAtShutdown.m_AllocationCount > 0)
	{
		LOGE("{} GPU allocations ({} bytes) were never destroyed before shutdown.", StatisticsAtShutdown.m_AllocationCount, StatisticsAtShutdown.m_UsedBytes);
	}

	vmaDestroyAllocator(m_Allocator);
	m_Allocator = nullptr;
	m_LogicalDevice = VK_NULL_HANDLE;
}

AllocatedBuffer GpuMemoryAllocator::createBuffer(VkDeviceSize SizeInBytes, VkBufferUsageFlags BufferUsage, GpuMemoryUsage MemoryUsage, std::string_view DebugName)
{
	VkBufferCreateInfo BufferCreateInfo{};
	BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	BufferCreateInfo.size = SizeInBytes;
	BufferCreateInfo.usage = BufferUsage;
	BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo AllocationCreateInfo{};
	switch (MemoryUsage)
	{
	case GpuMemoryUsage::GpuOnly:
		AllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		break;
	case GpuMemoryUsage::CpuWritesGpuReads:
		AllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		AllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		break;
	case GpuMemoryUsage::StagingUpload:
		AllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		AllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		break;
	case GpuMemoryUsage::GpuWritesCpuReads:
		AllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		AllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		break;
	}

	AllocatedBuffer CreatedBuffer{};
	VmaAllocationInfo AllocationInfo{};
	const VkResult CreationResult = vmaCreateBuffer(m_Allocator, &BufferCreateInfo, &AllocationCreateInfo, &CreatedBuffer.m_Buffer, &CreatedBuffer.m_Allocation, &AllocationInfo);
	if (CreationResult != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the '{}' buffer ({} bytes, VkResult {})", DebugName, SizeInBytes, static_cast<int>(CreationResult));
	}

	CreatedBuffer.m_SizeInBytes = SizeInBytes;
	CreatedBuffer.m_MappedData = AllocationInfo.pMappedData;
	vmaSetAllocationName(m_Allocator, CreatedBuffer.m_Allocation, std::string(DebugName).c_str());
	return CreatedBuffer;
}

void GpuMemoryAllocator::destroyBuffer(AllocatedBuffer& BufferToDestroy) noexcept
{
	if (BufferToDestroy.m_Buffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(m_Allocator, BufferToDestroy.m_Buffer, BufferToDestroy.m_Allocation);	// Also unmaps it
	}
	BufferToDestroy = {};
}

void GpuMemoryAllocator::flushBufferWrites(const AllocatedBuffer& WrittenBuffer, VkDeviceSize OffsetInBytes, VkDeviceSize SizeInBytes) const
{
	vmaFlushAllocation(m_Allocator, WrittenBuffer.m_Allocation, OffsetInBytes, SizeInBytes);
}

void GpuMemoryAllocator::invalidateBufferForReading(const AllocatedBuffer& BufferToRead, VkDeviceSize OffsetInBytes, VkDeviceSize SizeInBytes) const
{
	vmaInvalidateAllocation(m_Allocator, BufferToRead.m_Allocation, OffsetInBytes, SizeInBytes);
}

AllocatedImage GpuMemoryAllocator::createImage(const VkImageCreateInfo& ImageCreateInfo, VkImageAspectFlags ViewAspect, std::string_view DebugName)
{
	VmaAllocationCreateInfo AllocationCreateInfo{};
	AllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	// Render targets get their own dedicated block: drivers like that for attachments, and they're recreated on resize anyway
	const bool IsRenderTarget = (ImageCreateInfo.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0;
	if (IsRenderTarget)
	{
		AllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	}

	AllocatedImage CreatedImage{};
	const VkResult CreationResult = vmaCreateImage(m_Allocator, &ImageCreateInfo, &AllocationCreateInfo, &CreatedImage.m_Image, &CreatedImage.m_Allocation, nullptr);
	if (CreationResult != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the '{}' image ({}x{}, VkResult {})", DebugName, ImageCreateInfo.extent.width, ImageCreateInfo.extent.height, static_cast<int>(CreationResult));
	}
	vmaSetAllocationName(m_Allocator, CreatedImage.m_Allocation, std::string(DebugName).c_str());

	CreatedImage.m_Format = ImageCreateInfo.format;
	CreatedImage.m_Extent = { ImageCreateInfo.extent.width, ImageCreateInfo.extent.height };
	CreatedImage.m_MipLevelCount = ImageCreateInfo.mipLevels;

	VkImageViewCreateInfo ViewCreateInfo{};
	ViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ViewCreateInfo.image = CreatedImage.m_Image;
	ViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ViewCreateInfo.format = ImageCreateInfo.format;
	ViewCreateInfo.subresourceRange = { ViewAspect, 0, ImageCreateInfo.mipLevels, 0, 1 };
	if (vkCreateImageView(m_LogicalDevice, &ViewCreateInfo, nullptr, &CreatedImage.m_ImageView) != VK_SUCCESS)
	{
		vmaDestroyImage(m_Allocator, CreatedImage.m_Image, CreatedImage.m_Allocation);
		LOGNTHROW("Failed to create the view of the '{}' image", DebugName);
	}
	return CreatedImage;
}

void GpuMemoryAllocator::destroyImage(AllocatedImage& ImageToDestroy) noexcept
{
	if (ImageToDestroy.m_ImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(m_LogicalDevice, ImageToDestroy.m_ImageView, nullptr);
	}
	if (ImageToDestroy.m_Image != VK_NULL_HANDLE)
	{
		vmaDestroyImage(m_Allocator, ImageToDestroy.m_Image, ImageToDestroy.m_Allocation);
	}
	ImageToDestroy = {};
}

GpuMemoryStatistics GpuMemoryAllocator::getStatistics() const
{
	GpuMemoryStatistics Statistics{};
	if (m_Allocator == nullptr)
	{
		return Statistics;
	}

	const VkPhysicalDeviceMemoryProperties* MemoryProperties{ nullptr };
	vmaGetMemoryProperties(m_Allocator, &MemoryProperties);

	std::vector<VmaBudget> HeapBudgets(MemoryProperties->memoryHeapCount);
	vmaGetHeapBudgets(m_Allocator, HeapBudgets.data());
	for (uint32_t CurrentHeapIndex{ 0 }; CurrentHeapIndex < MemoryProperties->memoryHeapCount; ++CurrentHeapIndex)
	{
		const VmaBudget& CurrentHeapBudget = HeapBudgets[CurrentHeapIndex];
		Statistics.m_UsedBytes += CurrentHeapBudget.statistics.allocationBytes;
		Statistics.m_ReservedBytes += CurrentHeapBudget.statistics.blockBytes;
		Statistics.m_AllocationCount += CurrentHeapBudget.statistics.allocationCount;
		Statistics.m_DeviceMemoryBlockCount += CurrentHeapBudget.statistics.blockCount;
		if ((MemoryProperties->memoryHeaps[CurrentHeapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
		{
			Statistics.m_DeviceLocalBudgetBytes += CurrentHeapBudget.budget;
		}
	}
	return Statistics;
}

}
