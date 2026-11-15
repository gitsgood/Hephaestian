/**
 * @file CameraSystem.h
 * @brief Cameras: checking an entity can be looked through, and handing the active one to the renderer.
 */
#ifndef HEPHAESTIAN_CAMERASYSTEM_H
#define HEPHAESTIAN_CAMERASYSTEM_H

#include <entt/entt.hpp>

#include <Vulkanised/VulkanRenderer.h>

namespace Hephaestian::Systems
{

/**
 * @brief Whether an entity can be looked through.
 * @param Registry The scene's entities.
 * @param CandidateCameraEntity The entity.
 * @return True if it's alive, with a CameraComponent and a WorldTransformComponent.
 */
[[nodiscard]] bool isEntityUsableAsCamera(const entt::registry& Registry, entt::entity CandidateCameraEntity);

/**
 * @brief Hands the active camera's view and lens to the renderer.
 *
 * Does nothing if the entity is not usable as a camera, in which case the renderer keeps whatever it was last given.
 * @param Registry The scene's entities.
 * @param ActiveCameraEntity The camera to look through.
 * @param Renderer Where the view and projection go.
 * @return Whether the camera was submitted.
 */
bool submitActiveCameraToRenderer(const entt::registry& Registry, entt::entity ActiveCameraEntity, Vulkanised::VulkanRenderer& Renderer);

}

#endif // !HEPHAESTIAN_CAMERASYSTEM_H
