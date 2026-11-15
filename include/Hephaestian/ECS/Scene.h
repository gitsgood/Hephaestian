/**
 * @file Scene.h
 * @brief The scene: the EnTT registry, the systems run in order each frame, scene files, environment and audio.
 */
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

/**
 * @brief The Hephaestian engine.
 */
namespace Hephaestian
{

namespace Assets { class AssetLibrary; }
struct HephaestianSettings;

/**
 * @brief What a scene can override in HephaestianSettings while it's loaded. Unset = the engine setting applies.
 */
struct SceneEnvironmentOverrides
{
	std::optional<std::array<float, 3>> m_ClearColour;			///< "clearColour": RGB, 0 to 1
	std::optional<std::array<float, 3>> m_LightWorldPosition;	///< "lightPosition"
	std::optional<std::array<float, 3>> m_LightColour;			///< "lightColour": RGB, 0 to 1
	std::optional<float> m_AudioVolume;							///< "audioVolume": 0 to 1 (mute and focus muting stay the player's choice: engine only)
	std::optional<std::string> m_CameraControlScheme;			///< "cameraControlScheme": "worldCoordinates" or "relative" (see Systems::parseCameraControlSchemeName)
};

/**
 * @brief Owns the EnTT registry and runs the systems in the right order.
 *
 * Anything not covered by the helpers here can be done straight on the registry
 * (getRegistry().emplace<...>(Entity, ...)): it's a perfectly normal entt::registry.
 *
 * Per frame:
 * @code
 * Scene.updateSimulation(Input, DeltaTimeInSeconds);	// gameplay: input -> movement -> animation -> transform hierarchy
 * Scene.updateAudio(Settings, IsWindowActive);		// sources follow their entities, listener, volume -> OpenAL
 * Scene.renderToRenderer(Renderer);					// camera + render packet -> Vulkanised
 * @endcode
 */
class Scene
{
public:
	/**
	 * @brief Makes an entity with a NameComponent, a TransformComponent and a WorldTransformComponent.
	 * @param EntityName Its name (doesn't have to be unique).
	 * @return The new entity, at the origin.
	 */
	[[nodiscard]] entt::entity createEntity(std::string_view EntityName = "Unnamed Entity");

	/**
	 * @brief Destroys an entity, and everything parented to it (all the way down), so nothing is left pointing at a dead
	 * parent.
	 * @param EntityToDestroy The entity (ignored if it no longer exists).
	 */
	void destroyEntity(entt::entity EntityToDestroy);

	// - Hierarchy
	/**
	 * @brief Parents one entity to another: the child's TransformComponent becomes the offset from the parent.
	 * @param ChildEntity The entity to attach.
	 * @param ParentEntity What it follows from now on.
	 * @return False (and nothing changes) if either entity is invalid, the parent has no transform, or it would create
	 * a cycle.
	 */
	bool attachEntityToParent(entt::entity ChildEntity, entt::entity ParentEntity);
	/**
	 * @brief Makes an entity stand on its own again (its local transform becomes relative to the world).
	 * @param ChildEntity The entity.
	 */
	void detachEntityFromParent(entt::entity ChildEntity);
	/**
	 * @brief An entity's parent.
	 * @param ChildEntity The entity.
	 * @return Its parent, or entt::null if it has none.
	 */
	[[nodiscard]] entt::entity getParentOfEntity(entt::entity ChildEntity) const;
	/**
	 * @brief Whether one entity is somewhere below another in the hierarchy.
	 * @param PossibleDescendantEntity The entity that may be below.
	 * @param PossibleAncestorEntity The entity that may be above.
	 * @return True if following parents up from the first reaches the second.
	 */
	[[nodiscard]] bool isEntityDescendantOf(entt::entity PossibleDescendantEntity, entt::entity PossibleAncestorEntity) const;

	// - Camera
	/**
	 * @brief Picks the camera the scene is seen through.
	 * @param NewActiveCameraEntity Any entity with a CameraComponent and a WorldTransformComponent.
	 * @return False (and the previous one is kept) if it can't be one.
	 */
	bool setActiveCameraEntity(entt::entity NewActiveCameraEntity);
	/**
	 * @brief The camera the scene is seen through.
	 * @return The entity, or entt::null if none was set.
	 */
	[[nodiscard]] entt::entity getActiveCameraEntity() const noexcept { return m_ActiveCameraEntity; }

	// - Scene files (assets/Scenes/*.json, layout in SceneFile.h)
	/**
	 * @brief Replaces everything in the scene with a scene file's contents.
	 *
	 * Assets are loaded (or reused) first, then entities, parents and the active camera. Problems (missing assets,
	 * unknown parents...) are logged and skipped. Once loaded, assets the new scene doesn't use are freed
	 * (AssetLibrary::releaseAssetsUnusedByScene).
	 * @param SceneFilePath The scene file.
	 * @param AssetLibraryToLoadFrom Where its assets come from.
	 * @return False only for a file that can't be read or parsed at all, and then the current scene is left alone.
	 */
	bool loadFromJsonFile(const std::filesystem::path& SceneFilePath, Assets::AssetLibrary& AssetLibraryToLoadFrom);
	/**
	 * @brief Writes the scene as it is now (entities, components, asset table, environment overrides) in the same layout.
	 * @param SceneFilePath Where to write it.
	 * @return False if the file couldn't be written (logged).
	 */
	bool saveToJsonFile(const std::filesystem::path& SceneFilePath) const;
	/**
	 * @brief "Save scene": writes over the file the scene was loaded from.
	 *
	 * Debug builds load scenes from the repository's assets folder, so that's the file written. A scene loaded from the
	 * copy next to the exe instead also gets its repository copy (`<repo>/assets/...`) written, since that's the one the
	 * build deploys, so the change survives the next build.
	 * @return False if no file was loaded, or it couldn't be written.
	 */
	bool saveOverLoadedSceneFile() const;
	/**
	 * @brief The file the scene came from.
	 * @return Its path, or an empty path if the scene wasn't loaded from a file.
	 */
	[[nodiscard]] const std::filesystem::path& getLoadedSceneFilePath() const noexcept { return m_LoadedSceneFilePath; }

	/** @brief Destroys every entity and forgets the loaded file, asset table and environment overrides. */
	void clear();

	// - Environment: what the scene overrides, and applying "scene override, else engine setting" to the renderer
	/**
	 * @brief What the scene overrides (editable: the settings widget edits these while a scene overrides them).
	 * @return The overrides, saved with the scene.
	 */
	[[nodiscard]] SceneEnvironmentOverrides& getEnvironmentOverrides() noexcept { return m_EnvironmentOverrides; }
	/**
	 * @brief What the scene overrides.
	 * @return The overrides.
	 */
	[[nodiscard]] const SceneEnvironmentOverrides& getEnvironmentOverrides() const noexcept { return m_EnvironmentOverrides; }
	/**
	 * @brief Gives the renderer its clear colour and light: the scene's value where it sets one, otherwise the engine's.
	 * @param Renderer Where they go.
	 * @param EngineSettings The engine's own values.
	 */
	void applyEnvironmentToRenderer(Vulkanised::VulkanRenderer& Renderer, const HephaestianSettings& EngineSettings) const;

	// - Audio
	/**
	 * @brief Gives the scene its audio engine. Set once, before the first scene is loaded: audio sources need it (and
	 * stop when their entity is destroyed, removed, or the scene is cleared).
	 * @param AudioEngineToUse The engine (it must outlive the scene).
	 */
	void setAudioEngine(Audio::AudioEngine& AudioEngineToUse);
	/**
	 * @brief The scene's audio engine.
	 * @return It, or nullptr before setAudioEngine.
	 */
	[[nodiscard]] Audio::AudioEngine* getAudioEngine() const noexcept { return m_AudioEngine; }

	// - Per frame
	/**
	 * @brief Gameplay: free-fly controllers, constant rotations and sprite animations, then the transform hierarchy.
	 * @param Input This frame's keyboard and mouse state.
	 * @param EngineSettings The engine settings (the camera control scheme, scene can override the engine settings).
	 * @param DeltaTimeInSeconds Time since the last frame.
	 */
	void updateSimulation(const InputManager& Input, const HephaestianSettings& EngineSettings, float DeltaTimeInSeconds);
	/**
	 * @brief Audio, after updateSimulation (sources follow their entity's world transform).
	 *
	 * Applies the HRTF setting, starts and moves sources, puts the listener on its entity (or the active camera), and
	 * sets the output volume: the scene's audioVolume, else the engine setting; silent when muted, or when
	 * IsWindowActive is false and the settings ask to mute in the background. All of it reaches OpenAL as one batch.
	 * @param EngineSettings The engine settings (volume, mute, focus muting, HRTF).
	 * @param IsWindowActive False while the window is in the background or minimised.
	 */
	void updateAudio(const HephaestianSettings& EngineSettings, bool IsWindowActive);
	/**
	 * @brief Hands the active camera and every model and sprite to the renderer, and draws the frame.
	 * @param Renderer The renderer.
	 */
	void renderToRenderer(Vulkanised::VulkanRenderer& Renderer);

	/**
	 * @brief The EnTT registry, for anything the helpers here don't cover.
	 * @return The scene's registry.
	 */
	[[nodiscard]] entt::registry& getRegistry() noexcept { return m_Registry; }
	/**
	 * @brief The EnTT registry.
	 * @return The scene's registry.
	 */
	[[nodiscard]] const entt::registry& getRegistry() const noexcept { return m_Registry; }

private:
	entt::registry m_Registry;							///< Every entity and component
	entt::entity m_ActiveCameraEntity{ entt::null };	///< See setActiveCameraEntity
	bool m_HasWarnedAboutMissingActiveCamera{ false };	///< So a missing camera is warned about once
	std::string m_LastWarnedHrtfModeName;				///< So a bad "audioHrtf" is warned about once, not every frame

	/// Starts at 0 and is incremented before each hierarchy pass, so the first pass is frame 1 and never matches the components' default of 0
	std::uint64_t m_CurrentSimulationFrameNumber{ 0 };

	/// Reused every frame to avoid re-allocating the render packet
	std::vector<Vulkanised::RenderInstance> m_RenderInstancesForCurrentFrame;

	// From the loaded scene file, kept so it can be written back out
	std::filesystem::path m_LoadedSceneFilePath;		///< See getLoadedSceneFilePath
	SceneFile::SceneFileAssetTable m_SceneAssetTable;	///< The file's asset names and paths
	SceneEnvironmentOverrides m_EnvironmentOverrides;	///< See getEnvironmentOverrides

	Audio::AudioEngine* m_AudioEngine{ nullptr };	///< Set once by setAudioEngine: sources need it to stop when their entity goes
	/**
	 * @brief EnTT's on_destroy hook for AudioSourceComponent: stops the source's voice.
	 * @param Registry The registry the component is leaving.
	 * @param DestroyedEntity The entity losing its AudioSourceComponent.
	 */
	void onAudioSourceDestroyed(entt::registry& Registry, entt::entity DestroyedEntity);
};

}

#endif // !HEPHAESTIAN_SCENE_H
