#ifndef VULKANISED_RENDERING_GPUINSTANCEDATA_H
#define VULKANISED_RENDERING_GPUINSTANCEDATA_H

#include <cstdint>

#include <glm/glm.hpp>

namespace Vulkanised::Rendering
{

// One drawn instance, exactly as shader.vert reads it from the instance buffer (std430 layout: keep the two in sync).
// Indexed with gl_InstanceIndex, which includes each indirect draw's firstInstance.
struct GpuInstanceData
{
	glm::mat4 m_ModelMatrix{ 1.0f };
	glm::vec4 m_TextureCoordinateRect{ 0.0f, 0.0f, 1.0f, 1.0f };	// uv = vertexUv * zw + xy
	glm::vec4 m_Tint{ 1.0f };
	uint32_t m_TextureIndex{ 0 };									// Slot in the bindless texture array
	uint32_t m_Flags{ 0 };											// RenderInstanceFlags
	glm::vec2 m_PivotOffset{ 0.0f };
};
static_assert(sizeof(GpuInstanceData) == 112, "GpuInstanceData must match the shader's std430 InstanceData struct");

}

#endif // !VULKANISED_RENDERING_GPUINSTANCEDATA_H
