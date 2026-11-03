#ifndef HEPHAESTIAN_SCENE_H
#define HEPHAESTIAN_SCENE_H

#include <cstdint>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Input/InputManager.h"

namespace Hephaestian
{

/*
* Owns the EnTT registry and runs the systems in the right order. Anything not covered by the helpers here can be done
* straight on the registry (getRegistry().emplace<...>(Entity, ...)), it's a perfectly normal entt::registry.
*
* Per frame:
*	Scene.updateSimulation(Input, DeltaTimeInSeconds);	// gameplay: input -> movement -> transform hierarchy
*	Scene.renderToRenderer(Renderer);					// camera + render packet -> Vulkanised
*/
class Scene
{
public:
	// Every entity made this way has a NameComponent, a TransformComponent and a WorldTransformComponent
	[[nodiscard]] entt::entity createEntity(std::string_view EntityName = "Unnamed Entity");

	// Also destroys everything parented to it (all the way down), so nothing is left pointing at a dead parent
	void destroyEntity(entt::entity EntityToDestroy);

	// - Hierarchy
	// After this, the child's TransformComponent is relative to the parent (it becomes the offset from it).
	// Returns false (and changes nothing) if either entity is invalid, or if it would create a cycle.
	bool attachEntityToParent(entt::entity ChildEntity, entt::entity ParentEntity);
	void detachEntityFromParent(entt::entity ChildEntity);
	[[nodiscard]] entt::entity getParentOfEntity(entt::entity ChildEntity) const;
	[[nodiscard]] bool isEntityDescendantOf(entt::entity PossibleDescendantEntity, entt::entity PossibleAncestorEntity) const;

	// - Camera
	// Any entity with a CameraComponent can be the active one. Returns false (and keeps the previous one) if it can't.
	bool setActiveCameraEntity(entt::entity NewActiveCameraEntity);
	[[nodiscard]] entt::entity getActiveCameraEntity() const noexcept { return m_ActiveCameraEntity; }

	// - Per frame
	void updateSimulation(const InputManager& Input, float DeltaTimeInSeconds);
	void renderToRenderer(Vulkanised::VulkanRenderer& Renderer);

	[[nodiscard]] entt::registry& getRegistry() noexcept { return m_Registry; }
	[[nodiscard]] const entt::registry& getRegistry() const noexcept { return m_Registry; }

private:
	entt::registry m_Registry;
	entt::entity m_ActiveCameraEntity{ entt::null };
	bool m_HasWarnedAboutMissingActiveCamera{ false };

	// Starts at 0 and is incremented before each hierarchy pass, so the first pass is frame 1 and never matches the components' default of 0
	std::uint64_t m_CurrentSimulationFrameNumber{ 0 };

	// Reused every frame to avoid re-allocating the render packet
	std::vector<Vulkanised::RenderInstance> m_RenderInstancesForCurrentFrame;
};

}

#endif // !HEPHAESTIAN_SCENE_H
