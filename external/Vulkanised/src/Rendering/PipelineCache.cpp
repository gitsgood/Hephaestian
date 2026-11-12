#include "Vulkanised/VulkanRenderer.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "Vulkanised/utils/FileUtils.h"

/*
* PIPELINE CACHE: compiling pipelines (shaders to GPU machine code) is the slow part of creating them. The driver keeps what it
* compiled in a VkPipelineCache, which is saved next to the exe on shutdown and handed back at the next start.
* (NVIDIA and AMD also cache on their own, so the win is small on desktop, but it's free and grows with the pipeline count.)
*/

namespace
{
	[[nodiscard]] std::filesystem::path getPipelineCacheFilePath()
	{
		return Vulkanised::Utilities::File::getExecutableDirectory() / "PipelineCache.bin";
	}
}

void Vulkanised::VulkanRenderer::createPipelineCache()
{
	std::vector<char> SavedCacheData;
	if (std::ifstream SavedCacheFile{ getPipelineCacheFilePath(), std::ios::binary })
	{
		SavedCacheData.assign(std::istreambuf_iterator<char>(SavedCacheFile), std::istreambuf_iterator<char>());
	}

	// Only hand the driver data it wrote itself: same GPU vendor and model, same driver build (pipelineCacheUUID). Drivers are
	// supposed to reject anything else on their own, but not every driver has always done so gracefully.
	bool IsSavedCacheUsable = SavedCacheData.size() >= sizeof(VkPipelineCacheHeaderVersionOne);
	if (IsSavedCacheUsable)
	{
		VkPipelineCacheHeaderVersionOne SavedCacheHeader{};
		std::memcpy(&SavedCacheHeader, SavedCacheData.data(), sizeof(SavedCacheHeader));
		IsSavedCacheUsable = SavedCacheHeader.headerSize >= sizeof(VkPipelineCacheHeaderVersionOne)
			&& SavedCacheHeader.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
			&& SavedCacheHeader.vendorID == m_PhysicalDeviceProperties.vendorID
			&& SavedCacheHeader.deviceID == m_PhysicalDeviceProperties.deviceID
			&& std::memcmp(SavedCacheHeader.pipelineCacheUUID, m_PhysicalDeviceProperties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
	}

	VkPipelineCacheCreateInfo PipelineCacheCreateInfo{};
	PipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (IsSavedCacheUsable)
	{
		PipelineCacheCreateInfo.initialDataSize = SavedCacheData.size();
		PipelineCacheCreateInfo.pInitialData = SavedCacheData.data();
	}
	else if (!SavedCacheData.empty())
	{
		LOG("The saved pipeline cache was made by another GPU or driver, starting a fresh one.");
	}

	if (vkCreatePipelineCache(m_MainDevice.logicalDevice, &PipelineCacheCreateInfo, nullptr, &m_PipelineCache) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the pipeline cache");
	}
	if (IsSavedCacheUsable)
	{
		LOG("Pipeline cache loaded ({} KB).", SavedCacheData.size() / 1024);
	}
}

void Vulkanised::VulkanRenderer::saveAndDestroyPipelineCache() noexcept
{
	if (m_PipelineCache == VK_NULL_HANDLE)
	{
		return;
	}

	size_t CacheDataSize{ 0 };
	if (vkGetPipelineCacheData(m_MainDevice.logicalDevice, m_PipelineCache, &CacheDataSize, nullptr) == VK_SUCCESS && CacheDataSize > 0)
	{
		std::vector<char> CacheData(CacheDataSize);
		if (vkGetPipelineCacheData(m_MainDevice.logicalDevice, m_PipelineCache, &CacheDataSize, CacheData.data()) == VK_SUCCESS)
		{
			std::ofstream CacheFile{ getPipelineCacheFilePath(), std::ios::binary | std::ios::trunc };
			CacheFile.write(CacheData.data(), static_cast<std::streamsize>(CacheDataSize));
			if (!CacheFile)
			{
				LOGW("Couldn't save the pipeline cache to {}", getPipelineCacheFilePath().string());
			}
		}
	}

	vkDestroyPipelineCache(m_MainDevice.logicalDevice, m_PipelineCache, nullptr);
	m_PipelineCache = VK_NULL_HANDLE;
}
