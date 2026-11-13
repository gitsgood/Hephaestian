#ifndef HEPHAESTIAN_SCENE_H
#define HEPHAESTIAN_SCENE_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Audio/AudioEngine.h"
#include "Hephaestian/ECS/SceneFile.h"
#include "Hephaestian/Input/InputManager.h"

namespace Hephaestian
{

namespace Assets { class AssetLibrary; }
struct HephaestianSettings;

// What a scene can override in HephaestianSettings while it's loaded. Unset = the engine setting applies.
struct SceneEnvironmentOverrides
{
	std::optional<std::array<float, 3>> m_ClearColour;
	std::optional<std::array<float, 3>> m_LightWorldPosition;
	std::optional<std::array<float, 3>> m_LightColour;
	std::optional<float> m_AudioVolume;					// 0 to 1 (mute and focus muting stay the player's choice: engine only)
};

/*
* Owns the EnTT registry and runs the systems in the right order. Anything not covered by the helpers here can be done
* straight on the registry (getRegistry().emplace<...>(Entity, ...)), it's a perfectly normal entt::registry.
*
* Per frame:
*	Scene.updateSimulation(Input, DeltaTimeInSeconds);	// gameplay: input -> movement -> transform hierarchy
*	Scene.updateAudio(Settings, IsWindowActive);		// sources follow their entities, listener, volume -> OpenAL
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

	// - Scene files (assets/Scenes/*.json, layout in SceneFile.h)
	// Replaces everything in the scene with the file's contents. Problems (missing assets, unknown parents...) are logged
	// and skipped; only a file that can't be read or parsed at all returns false, and then the current scene is left alone.
	bool loadFromJsonFile(const std::filesystem::path& SceneFilePath, Assets::AssetLibrary& AssetLibraryToLoadFrom);
	// Writes the scene as it is now (entities, components, asset table, environment overrides) in the same layout
	bool saveToJsonFile(const std::filesystem::path& SceneFilePath) const;
	// "Save scene": writes over the file the scene was loaded from. Debug builds ALSO write the repository's copy
	// (<repo>/assets/...), which is the one the build deploys, so the change survives the next build.
	bool saveOverLoadedSceneFile() const;
	[[nodiscard]] const std::filesystem::path& getLoadedSceneFilePath() const noexcept { return m_LoadedSceneFilePath; }

	// Destroys every entity and forgets the loaded file, asset table and environment overrides
	void clear();

	// - Environment: what the scene overrides, and applying "scene override, else engine setting" to the renderer
	[[nodiscard]] SceneEnvironmentOverrides& getEnvironmentOverrides() noexcept { return m_EnvironmentOverrides; }
	[[nodiscard]] const SceneEnvironmentOverrides& getEnvironmentOverrides() const noexcept { return m_EnvironmentOverrides; }
	void applyEnvironmentToRenderer(Vulkanised::VulkanRenderer& Renderer, const HephaestianSettings& EngineSettings) const;

	// - Audio. Set once, before the first scene is loaded: audio sources need it (and stop when their entity is destroyed).
	void setAudioEngine(Audio::AudioEngine& AudioEngineToUse);
	[[nodiscard]] Audio::AudioEngine* getAudioEngine() const noexcept { return m_AudioEngine; }

	// - Per frame
	void updateSimulation(const InputManager& Input, float DeltaTimeInSeconds);
	// After updateSimulation (sources follow their entity's world transform): starts and moves sources, puts the listener
	// on its entity (or the active camera), and sets the output volume: the scene's audioVolume, else the engine setting;
	// silent when muted, or when IsWindowActive is false and the settings ask to mute in the background.
	void updateAudio(const HephaestianSettings& EngineSettings, bool IsWindowActive);
	void renderToRenderer(Vulkanised::VulkanRenderer& Renderer);

	[[nodiscard]] entt::registry& getRegistry() noexcept { return m_Registry; }
	[[nodiscard]] const entt::registry& getRegistry() const noexcept { return m_Registry; }

private:
	entt::registry m_Registry;
	entt::entity m_ActiveCameraEntity{ entt::null };
	bool m_HasWarnedAboutMissingActiveCamera{ false };
	std::string m_LastWarnedHrtfModeName;	// So a bad "audioHrtf" is warned about once, not every frame

	// Starts at 0 and is incremented before each hierarchy pass, so the first pass is frame 1 and never matches the components' default of 0
	std::uint64_t m_CurrentSimulationFrameNumber{ 0 };

	// Reused every frame to avoid re-allocating the render packet
	std::vector<Vulkanised::RenderInstance> m_RenderInstancesForCurrentFrame;

	// From the loaded scene file, kept so it can be written back out
	std::filesystem::path m_LoadedSceneFilePath;
	SceneFile::SceneFileAssetTable m_SceneAssetTable;
	SceneEnvironmentOverrides m_EnvironmentOverrides;

	// Set once by setAudioEngine: sources need it to stop when their entity goes
	Audio::AudioEngine* m_AudioEngine{ nullptr };
	void onAudioSourceDestroyed(entt::registry& Registry, entt::entity DestroyedEntity);
};

}

#endif // !HEPHAESTIAN_SCENE_H
