#ifndef HEPHAESTIAN_CAMERASYSTEM_H
#define HEPHAESTIAN_CAMERASYSTEM_H

#include <entt/entt.hpp>

#include <Vulkanised/VulkanRenderer.h>

namespace Hephaestian::Systems
{

// True if this entity can be looked through (alive, with a CameraComponent and a WorldTransformComponent)
[[nodiscard]] bool isEntityUsableAsCamera(const entt::registry& Registry, entt::entity CandidateCameraEntity);

// Hands the active camera's view and lens to the renderer. Does nothing if the entity is not usable as a camera,
// in which case the renderer keeps whatever it was last given.
// Returns whether the camera was submitted.
bool submitActiveCameraToRenderer(const entt::registry& Registry, entt::entity ActiveCameraEntity, Vulkanised::VulkanRenderer& Renderer);

}

#endif // !HEPHAESTIAN_CAMERASYSTEM_H
