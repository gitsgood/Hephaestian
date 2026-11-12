#ifndef VULKANISED_RENDERING_FRAMESTATISTICS_H
#define VULKANISED_RENDERING_FRAMESTATISTICS_H

#include <cstdint>

namespace Vulkanised::Rendering
{

// How the last frames went, for a debug overlay. Times are smoothed over roughly the last 20 frames, so they're readable.
struct FrameStatistics
{
	// Whole frame, from one SubmitFrame to the next (so 1000 / this is the frame rate)
	float m_FrameIntervalInMilliseconds{ 0.0f };
	// CPU time SubmitFrame spent doing actual work: sorting, culling, writing instance data, recording commands, submitting
	float m_CpuSubmitTimeInMilliseconds{ 0.0f };
	// CPU time SubmitFrame spent waiting for the GPU to give a frame's resources back (high = GPU bound)
	float m_CpuWaitForGpuTimeInMilliseconds{ 0.0f };
	// GPU time between the start and the end of a frame's command buffer (measured with timestamp queries, a frame or two late)
	float m_GpuFrameTimeInMilliseconds{ 0.0f };
	bool m_IsGpuTimingAvailable{ false };	// Some queues can't write timestamps

	// Last frame, exact
	uint32_t m_SubmittedInstanceCount{ 0 };	// What the application handed to SubmitFrame
	uint32_t m_CulledInstanceCount{ 0 };	// Outside the camera's view, never sent to the GPU
	uint32_t m_DrawnInstanceCount{ 0 };
	uint32_t m_DrawCallCount{ 0 };			// Draws recorded for the scene (indirect draws count each command inside)
};

}

#endif // !VULKANISED_RENDERING_FRAMESTATISTICS_H
