#ifndef VULKANISED_RENDERING_FRAMERESOURCES_H
#define VULKANISED_RENDERING_FRAMERESOURCES_H

#include <vulkan/vulkan.h>

#include "Vulkanised/Rendering/GpuMemory.h"

namespace Vulkanised::Rendering
{

/*
* Everything one frame in flight owns. The CPU records and writes frame N+1 while the GPU still renders frame N, so anything
* rewritten every frame needs one copy per frame in flight, and is only touched again once that frame's fence has signalled.
*
* (These used to be indexed by SWAPCHAIN IMAGE instead, while the fences were per frame in flight: with 3+ swapchain images
* and 2 frames in flight, the CPU could rewrite a buffer, or re-record a command buffer, the GPU was still using.)
*/
struct FrameResources
{
	VkCommandPool m_CommandPool{ VK_NULL_HANDLE };					// Reset as a whole at the start of the frame (cheaper than per buffer)
	VkCommandBuffer m_CommandBuffer{ VK_NULL_HANDLE };
	VkFence m_InFlightFence{ VK_NULL_HANDLE };						// Signalled when the GPU is done with everything below
	VkSemaphore m_SwapchainImageAvailableSemaphore{ VK_NULL_HANDLE };	// Signalled by vkAcquireNextImageKHR

	AllocatedBuffer m_InstanceBuffer;								// Per-instance data (transforms), persistently mapped
	AllocatedBuffer m_CameraUniformBuffer;							// Camera and light, persistently mapped
	VkDescriptorSet m_FrameDescriptorSet{ VK_NULL_HANDLE };		// Set 0: the two buffers above

	VkQueryPool m_TimestampQueryPool{ VK_NULL_HANDLE };			// [0] start of the frame's commands, [1] end
	bool m_HasTimestampQueriesToRead{ false };
};

}

#endif // !VULKANISED_RENDERING_FRAMERESOURCES_H
