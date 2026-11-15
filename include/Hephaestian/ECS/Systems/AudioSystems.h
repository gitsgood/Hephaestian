/**
 * @file AudioSystems.h
 * @brief The ECS side of audio: AudioSourceComponent and AudioListenerComponent driving the AudioEngine.
 */
#ifndef HEPHAESTIAN_AUDIOSYSTEMS_H
#define HEPHAESTIAN_AUDIOSYSTEMS_H

#include <entt/entt.hpp>

#include "Hephaestian/Audio/AudioEngine.h"
#include "Hephaestian/ECS/Components.h"

/**
 * @brief The systems: free functions that update the components of a registry, called by Scene each frame.
 */
namespace Hephaestian::Systems
{

/**
 * @brief Starts the sources that play on start (once), and keeps every playing one in step with its component and entity.
 *
 * Volume and pitch, and for spatial ones, the entity's world position. A source whose sound finished gets its voice
 * handle cleared. Call after the transform hierarchy has run.
 * @param Registry The scene's entities.
 * @param AudioEngine What plays them.
 */
void updateAudioSources(entt::registry& Registry, Audio::AudioEngine& AudioEngine);

/**
 * @brief Puts the ears on the first entity with an AudioListenerComponent, or on the active camera if there is none
 * (or nowhere, at the origin facing -Z, if there's no camera either).
 *
 * The listener faces its entity's -Z, with +Y up (scale is ignored).
 * @param Registry The scene's entities.
 * @param AudioEngine What hears.
 * @param ActiveCameraEntity The scene's active camera (may be entt::null).
 */
void updateAudioListener(const entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity ActiveCameraEntity);

/**
 * @brief Starts an entity's source from the beginning (stopping it first if it was playing).
 * @param Registry The scene's entities.
 * @param AudioEngine What plays it.
 * @param SourceEntity The entity with the AudioSourceComponent.
 * @return False if it has no source or sound, or no voice was free.
 */
bool playAudioSource(entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity SourceEntity);
/**
 * @brief Stops an entity's source (does nothing if it isn't playing or has none).
 * @param Registry The scene's entities.
 * @param AudioEngine What plays it.
 * @param SourceEntity The entity with the AudioSourceComponent.
 */
void stopAudioSource(entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity SourceEntity);

/**
 * @brief The settings an AudioSourceComponent plays with.
 * @param Source The component.
 * @param WorldPosition Where its entity is.
 * @return Its volume, pitch, looping and 3D settings, placed at WorldPosition.
 */
[[nodiscard]] Audio::SoundPlaybackSettings makePlaybackSettings(const Components::AudioSourceComponent& Source, const glm::vec3& WorldPosition);

}

#endif // !HEPHAESTIAN_AUDIOSYSTEMS_H
