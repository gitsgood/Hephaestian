#ifndef HEPHAESTIAN_AUDIOSYSTEMS_H
#define HEPHAESTIAN_AUDIOSYSTEMS_H

#include <entt/entt.hpp>

#include "Hephaestian/Audio/AudioEngine.h"
#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

// Starts the sources that play on start (once), and keeps every playing one in step with its component and entity:
// volume and pitch, and for spatial ones, the entity's world position. Call after the transform hierarchy has run.
void updateAudioSources(entt::registry& Registry, Audio::AudioEngine& AudioEngine);

// Puts the ears on the first entity with an AudioListenerComponent, or on the active camera if there is none
// (or nowhere, at the origin facing -Z, if there's no camera either).
void updateAudioListener(const entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity ActiveCameraEntity);

// Starts the entity's source from the beginning (stopping it first if it was playing). False if it has no source or sound,
// or no voice was free.
bool playAudioSource(entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity SourceEntity);
void stopAudioSource(entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity SourceEntity);

// The settings an AudioSourceComponent plays with (at the given world position)
[[nodiscard]] Audio::SoundPlaybackSettings makePlaybackSettings(const Components::AudioSourceComponent& Source, const glm::vec3& WorldPosition);

}

#endif // !HEPHAESTIAN_AUDIOSYSTEMS_H
