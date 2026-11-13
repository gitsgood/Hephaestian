#include "Hephaestian/ECS/Systems/AudioSystems.h"

namespace Hephaestian::Systems
{

namespace
{
	[[nodiscard]] glm::vec3 getWorldPosition(const entt::registry& Registry, entt::entity Entity)
	{
		const auto* WorldTransform = Registry.try_get<Components::WorldTransformComponent>(Entity);
		return WorldTransform != nullptr ? glm::vec3(WorldTransform->m_WorldTransformMatrix[3]) : glm::vec3(0.0f);
	}
}

Audio::SoundPlaybackSettings makePlaybackSettings(const Components::AudioSourceComponent& Source, const glm::vec3& WorldPosition)
{
	Audio::SoundPlaybackSettings PlaybackSettings{};
	PlaybackSettings.m_Volume = Source.m_Volume;
	PlaybackSettings.m_Pitch = Source.m_Pitch;
	PlaybackSettings.m_ShouldLoop = Source.m_ShouldLoop;
	PlaybackSettings.m_IsSpatial = Source.m_IsSpatial;
	PlaybackSettings.m_WorldPosition = WorldPosition;
	PlaybackSettings.m_MinimumDistance = Source.m_MinimumDistance;
	PlaybackSettings.m_MaximumDistance = Source.m_MaximumDistance;
	PlaybackSettings.m_Rolloff = Source.m_Rolloff;
	PlaybackSettings.m_Falloff = Source.m_Falloff;
	return PlaybackSettings;
}

void updateAudioSources(entt::registry& Registry, Audio::AudioEngine& AudioEngine)
{
	auto Sources = Registry.view<Components::AudioSourceComponent>();
	for (const entt::entity SourceEntity : Sources)
	{
		Components::AudioSourceComponent& Source = Sources.get<Components::AudioSourceComponent>(SourceEntity);
		if (!Source.m_HasBeenStarted && Source.m_ShouldPlayOnStart && Source.m_Sound != nullptr)
		{
			Source.m_HasBeenStarted = true;	// Even if no voice was free: play on start means once, not "keep trying"
			Source.m_Voice = AudioEngine.play(Source.m_Sound, makePlaybackSettings(Source, getWorldPosition(Registry, SourceEntity)));
			continue;
		}
		if (!Source.m_Voice.isSet())
		{
			continue;
		}
		if (!AudioEngine.isPlaying(Source.m_Voice))
		{
			Source.m_Voice = {};	// It finished on its own
			continue;
		}

		// A handful of OpenAL calls per playing source, applied with the rest of the frame's in one batch
		AudioEngine.setVoiceVolumeAndPitch(Source.m_Voice, Source.m_Volume, Source.m_Pitch);
		if (Source.m_IsSpatial)
		{
			AudioEngine.setVoicePosition(Source.m_Voice, getWorldPosition(Registry, SourceEntity));
		}
	}
}

void updateAudioListener(const entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity ActiveCameraEntity)
{
	// An explicit listener wins; otherwise the camera's ears
	const auto Listeners = Registry.view<Components::AudioListenerComponent, Components::WorldTransformComponent>();
	entt::entity ListenerEntity = Listeners.begin() != Listeners.end() ? *Listeners.begin() : entt::entity{ entt::null };
	if (ListenerEntity == entt::null && Registry.valid(ActiveCameraEntity) && Registry.all_of<Components::WorldTransformComponent>(ActiveCameraEntity))
	{
		ListenerEntity = ActiveCameraEntity;
	}
	if (ListenerEntity == entt::null)
	{
		AudioEngine.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		return;
	}

	// Looks down its -Z, +Y up (scale doesn't matter, so the directions are normalised)
	const glm::mat4& WorldTransformMatrix = Registry.get<Components::WorldTransformComponent>(ListenerEntity).m_WorldTransformMatrix;
	const glm::vec3 ForwardDirection = glm::normalize(-glm::vec3(WorldTransformMatrix[2]));
	const glm::vec3 UpDirection = glm::normalize(glm::vec3(WorldTransformMatrix[1]));
	AudioEngine.setListener(glm::vec3(WorldTransformMatrix[3]), ForwardDirection, UpDirection);
}

bool playAudioSource(entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity SourceEntity)
{
	auto* Source = Registry.try_get<Components::AudioSourceComponent>(SourceEntity);
	if (Source == nullptr || Source->m_Sound == nullptr)
	{
		return false;
	}
	AudioEngine.stop(Source->m_Voice);
	Source->m_HasBeenStarted = true;
	Source->m_Voice = AudioEngine.play(Source->m_Sound, makePlaybackSettings(*Source, getWorldPosition(Registry, SourceEntity)));
	return Source->m_Voice.isSet();
}

void stopAudioSource(entt::registry& Registry, Audio::AudioEngine& AudioEngine, entt::entity SourceEntity)
{
	if (auto* Source = Registry.try_get<Components::AudioSourceComponent>(SourceEntity))
	{
		AudioEngine.stop(Source->m_Voice);
		Source->m_Voice = {};
	}
}

}
