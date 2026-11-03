#ifndef HEPHAESTIAN_RENDERSYSTEM_H
#define HEPHAESTIAN_RENDERSYSTEM_H

#include <vector>

#include <entt/entt.hpp>

#include <Vulkanised/VulkanRenderer.h>

namespace Hephaestian::Systems
{

// Turns every visible ModelRendererComponent into one RenderInstance per submesh of its model.
// The vector is cleared first, and reused across frames so it stops allocating once it's big enough.
void buildRenderInstancesFromModelRenderers(const entt::registry& Registry, const Vulkanised::VulkanRenderer& Renderer, std::vector<Vulkanised::RenderInstance>& RenderInstancesToFill);

}

#endif // !HEPHAESTIAN_RENDERSYSTEM_H
