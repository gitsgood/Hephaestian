#include "Hephaestian/ECS/Scene.h"

#include <string>

#include <Vulkanised/Logger.h>

#include "Hephaestian/ECS/Components.h"
#include "Hephaestian/ECS/Systems/TransformHierarchySystem.h"
#include "Hephaestian/ECS/Systems/MovementSystems.h"
#include "Hephaestian/ECS/Systems/CameraSystem.h"
#include "Hephaestian/ECS/Systems/RenderSystem.h"
#include "Hephaestian/ECS/Systems/SpriteSystems.h"
#include "Hephaestian/ECS/Systems/AudioSystems.h"
#include "Hephaestian/Settings/HephaestianSettings.h"

namespace Hephaestian
{

entt::entity Scene::createEntity(std::string_view EntityName)
{
	const entt::entity NewEntity = m_Registry.create();
	m_Registry.emplace<Components::NameComponent>(NewEntity, std::string(EntityName));
	m_Registry.emplace<Components::TransformComponent>(NewEntity);
	m_Registry.emplace<Components::WorldTransformComponent>(NewEntity);
	return NewEntity;
}

void Scene::destroyEntity(entt::entity EntityToDestroy)
{
	if (!m_Registry.valid(EntityToDestroy))
	{
		return;
	}

	// Collect first, destroy after: destroying while iterating the view would invalidate it
	std::vector<entt::entity> ChildrenOfEntityToDestroy;
	for (const auto [CurrentEntity, CurrentParent] : m_Registry.view<const Components::ParentEntityComponent>().each())
	{
		if (CurrentParent.m_ParentEntity == EntityToDestroy)
		{
			ChildrenOfEntityToDestroy.push_back(CurrentEntity);
		}
	}
	for (const entt::entity CurrentChildEntity : ChildrenOfEntityToDestroy)
	{
		destroyEntity(CurrentChildEntity);
	}

	if (EntityToDestroy == m_ActiveCameraEntity)
	{
		m_ActiveCameraEntity = entt::null;
	}

	m_Registry.destroy(EntityToDestroy);
}

bool Scene::attachEntityToParent(entt::entity ChildEntity, entt::entity ParentEntity)
{
	if (!m_Registry.valid(ChildEntity) || !m_Registry.valid(ParentEntity))
	{
		LOGW("Cannot attach entity {} to entity {}: at least one of them doesn't exist.", entt::to_integral(ChildEntity), entt::to_integral(ParentEntity));
		return false;
	}
	if (ChildEntity == ParentEntity || isEntityDescendantOf(ParentEntity, ChildEntity))
	{
		LOGW("Cannot attach entity {} to entity {}: that would create a parenting cycle.", entt::to_integral(ChildEntity), entt::to_integral(ParentEntity));
		return false;
	}
	if (!m_Registry.all_of<Components::TransformComponent, Components::WorldTransformComponent>(ParentEntity))
	{
		LOGW("Cannot attach entity {} to entity {}: the parent has no transform.", entt::to_integral(ChildEntity), entt::to_integral(ParentEntity));
		return false;
	}

	m_Registry.emplace_or_replace<Components::ParentEntityComponent>(ChildEntity, ParentEntity);
	return true;
}

void Scene::detachEntityFromParent(entt::entity ChildEntity)
{
	if (m_Registry.valid(ChildEntity))
	{
		m_Registry.remove<Components::ParentEntityComponent>(ChildEntity);
	}
}

entt::entity Scene::getParentOfEntity(entt::entity ChildEntity) const
{
	if (!m_Registry.valid(ChildEntity))
	{
		return entt::null;
	}
	const auto* ParentOfChild = m_Registry.try_get<Components::ParentEntityComponent>(ChildEntity);
	return ParentOfChild != nullptr ? ParentOfChild->m_ParentEntity : entt::entity{ entt::null };
}

bool Scene::isEntityDescendantOf(entt::entity PossibleDescendantEntity, entt::entity PossibleAncestorEntity) const
{
	// Walk up the parent chain. The chain can't contain a cycle (attachEntityToParent refuses them),
	// but bound the walk anyways in case someone emplaced ParentEntityComponent by hand.
	constexpr std::uint32_t MaximumHierarchyDepthToWalk{ 256 };

	entt::entity CurrentAncestorEntity = getParentOfEntity(PossibleDescendantEntity);
	for (std::uint32_t CurrentHierarchyDepth{ 0 }; CurrentAncestorEntity != entt::null && CurrentHierarchyDepth < MaximumHierarchyDepthToWalk; ++CurrentHierarchyDepth)
	{
		if (CurrentAncestorEntity == PossibleAncestorEntity)
		{
			return true;
		}
		CurrentAncestorEntity = getParentOfEntity(CurrentAncestorEntity);
	}
	return false;
}

bool Scene::setActiveCameraEntity(entt::entity NewActiveCameraEntity)
{
	if (!Systems::isEntityUsableAsCamera(m_Registry, NewActiveCameraEntity))
	{
		LOGW("Entity {} cannot be the active camera: it needs a CameraComponent and a WorldTransformComponent.", entt::to_integral(NewActiveCameraEntity));
		return false;
	}

	m_ActiveCameraEntity = NewActiveCameraEntity;
	m_HasWarnedAboutMissingActiveCamera = false;
	return true;
}

void Scene::updateSimulation(const InputManager& Input, float DeltaTimeInSeconds)
{
	// Order matters: everything that moves things goes first, then the hierarchy turns local transforms into world ones
	Systems::updateFreeFlyMovementControllers(m_Registry, Input, DeltaTimeInSeconds);
	Systems::updateConstantRotations(m_Registry, DeltaTimeInSeconds);
	Systems::updateSpriteAnimations(m_Registry, DeltaTimeInSeconds);

	++m_CurrentSimulationFrameNumber;
	Systems::updateWorldTransformsFromHierarchy(m_Registry, m_CurrentSimulationFrameNumber);
}

void Scene::setAudioEngine(Audio::AudioEngine& AudioEngineToUse)
{
	if (m_AudioEngine == nullptr)
	{
		// A source's voice stops with it: destroyed entity, removed component, cleared scene (loading another one)
		m_Registry.on_destroy<Components::AudioSourceComponent>().connect<&Scene::onAudioSourceDestroyed>(*this);
	}
	m_AudioEngine = &AudioEngineToUse;
}

void Scene::onAudioSourceDestroyed(entt::registry& Registry, entt::entity DestroyedEntity)
{
	if (m_AudioEngine != nullptr)
	{
		m_AudioEngine->stop(Registry.get<Components::AudioSourceComponent>(DestroyedEntity).m_Voice);
	}
}

void Scene::updateAudio(const HephaestianSettings& EngineSettings, bool IsWindowActive)
{
	if (m_AudioEngine == nullptr)
	{
		return;
	}

	// Everything in between reaches OpenAL as one batch
	// HRTF: engine-only (it's about the player's headphones, not the scene). Only a change reaches OpenAL.
	const std::optional<Audio::HrtfMode> RequestedHrtfMode = Audio::parseHrtfModeName(EngineSettings.m_AudioHrtfMode);
	if (!RequestedHrtfMode.has_value() && m_LastWarnedHrtfModeName != EngineSettings.m_AudioHrtfMode)
	{
		LOGW("Settings: unknown audioHrtf '{}' (\"auto\", \"on\" or \"off\"), using auto.", EngineSettings.m_AudioHrtfMode);
		m_LastWarnedHrtfModeName = EngineSettings.m_AudioHrtfMode;
	}
	m_AudioEngine->setHrtfMode(RequestedHrtfMode.value_or(Audio::HrtfMode::Automatic));

	m_AudioEngine->beginFrameUpdates();
	const bool IsSilenced = EngineSettings.m_IsAudioMuted || (!IsWindowActive && EngineSettings.m_ShouldMuteAudioWhenUnfocused);
	m_AudioEngine->setOutputGain(IsSilenced ? 0.0f : glm::clamp(m_EnvironmentOverrides.m_AudioVolume.value_or(EngineSettings.m_AudioVolume), 0.0f, 1.0f));
	Systems::updateAudioListener(m_Registry, *m_AudioEngine, m_ActiveCameraEntity);
	Systems::updateAudioSources(m_Registry, *m_AudioEngine);
	m_AudioEngine->endFrameUpdates();
}

void Scene::renderToRenderer(Vulkanised::VulkanRenderer& Renderer)
{
	if (!Systems::submitActiveCameraToRenderer(m_Registry, m_ActiveCameraEntity, Renderer) && !m_HasWarnedAboutMissingActiveCamera)
	{
		LOGW("Scene has no usable active camera, the renderer will keep using its previous/default view.");
		m_HasWarnedAboutMissingActiveCamera = true;
	}

	Systems::buildRenderInstancesFromModelRenderers(m_Registry, Renderer, m_RenderInstancesForCurrentFrame);
	Systems::appendRenderInstancesFromSpriteRenderers(m_Registry, m_RenderInstancesForCurrentFrame);
	Renderer.SubmitFrame(m_RenderInstancesForCurrentFrame);
}

}
