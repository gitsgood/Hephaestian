#ifndef HEPHAESTIAN_AUDIOENGINE_H
#define HEPHAESTIAN_AUDIOENGINE_H

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

// OpenAL's own types (AL/alc.h), so this header doesn't pull in OpenAL
struct ALCdevice;
struct ALCcontext;

/*
* Everything audio, on top of OpenAL Soft (mixing, 3D panning, HRTF on its own thread).
*
* - SOUNDS (SoundAsset): loaded once and shared. Short ones (10 s or less) are decoded whole into one OpenAL buffer that
*   every voice playing them shares; longer ones (music, ambience) are STREAMED: decoded a quarter second at a time while
*   they play, so a 4 minute track costs ~200 KB instead of ~40 MB. Picked automatically.
* - VOICES: a fixed pool of OpenAL sources, made once. play() is for sounds something owns (an AudioSourceComponent);
*   playOneShot() is for fire-and-forget sounds, which are the ones taken over when every voice is busy (the oldest one
*   that doesn't loop first; looping ones only once nothing else is left).
*   A VoiceHandle is a slot plus a reuse count, like texture identifiers: a stale one never touches someone else's sound.
* - STREAMING THREAD: keeps every streaming voice about a second ahead (4 buffers of 250 ms), loops without a gap, and
*   restarts a voice that ran dry (counted as an underrun: there should never be any).
* - PER FRAME (game thread): beginFrameUpdates(), then positions/listener/volume, then endFrameUpdates(): OpenAL applies
*   the whole frame's changes at once (AL_SOFT_deferred_updates), and finished voices go back to the pool.
*
* No audio device (or OpenAL failing to start): logged once, and everything quietly does nothing. Scenes still load.
*/
namespace Hephaestian::Audio
{

class AudioDecoder;

// A sound file, made by AudioEngine::createSound (the AssetLibrary caches them by path)
struct SoundAsset
{
	std::filesystem::path m_SourceFilePath;
	std::uint32_t m_ChannelCount{ 0 };
	std::uint32_t m_SampleRateInHertz{ 0 };
	double m_DurationInSeconds{ 0.0 };		// Estimated for MP3
	bool m_IsStreamed{ false };
	std::uint32_t m_DecodedBufferName{ 0 };	// OpenAL buffer with every sample (decoded sounds only, 0 otherwise)
};

struct VoiceHandle
{
	std::uint32_t m_Value{ 0 };	// 0 = no voice
	[[nodiscard]] bool isSet() const noexcept { return m_Value != 0; }
	[[nodiscard]] bool operator==(const VoiceHandle&) const = default;
};

enum class DistanceFalloff
{
	Inverse,	// Like real sound: halves with each doubling of distance past the minimum, never quite silent
	Linear		// Straight down from full at the minimum distance to silent at the maximum
};

struct SoundPlaybackSettings
{
	float m_Volume{ 1.0f };
	float m_Pitch{ 1.0f };						// Also speeds it up / slows it down
	bool m_ShouldLoop{ false };
	// 3D: placed at m_WorldPosition, panned and faded by distance. Otherwise the same in both ears, wherever the listener is.
	bool m_IsSpatial{ false };
	glm::vec3 m_WorldPosition{ 0.0f };
	float m_MinimumDistance{ 1.0f };			// Full volume up to here
	float m_MaximumDistance{ 50.0f };			// Inverse: stops getting quieter past here. Linear: silent from here.
	float m_Rolloff{ 1.0f };					// How quickly it fades in between
	DistanceFalloff m_Falloff{ DistanceFalloff::Inverse };
};

// HRTF: makes stereo sound come from all around (above, behind...) through headphones. Wrong on speakers.
enum class HrtfMode
{
	Automatic,	// OpenAL Soft decides: on when Windows says the output is headphones (cards that switch to headphones themselves may still say speakers)
	On,			// Always, and the output is made stereo for it
	Off
};
// "auto", "on", "off" (the settings file's names). std::nullopt for anything else.
[[nodiscard]] std::optional<HrtfMode> parseHrtfModeName(std::string_view HrtfModeName) noexcept;
[[nodiscard]] std::string_view getHrtfModeName(HrtfMode Mode) noexcept;

struct AudioStatistics
{
	bool m_IsDeviceOpen{ false };
	std::string m_DeviceName;
	bool m_IsHrtfEnabled{ false };
	std::string m_HrtfStatusDescription;	// Why it's on or off ("on, headphones detected", "off, output isn't stereo"...)
	std::string m_HrtfName;					// Which head it uses (empty while off)
	std::uint32_t m_VoiceCapacity{ 0 };
	std::uint32_t m_VoicesInUse{ 0 };
	std::uint32_t m_StreamingVoices{ 0 };
	std::uint64_t m_StreamUnderrunCount{ 0 };	// Times a stream ran dry and had to be restarted (should stay 0)
	std::uint64_t m_StolenVoiceCount{ 0 };		// One-shots cut short to make room
};

class AudioEngine
{
public:
	explicit AudioEngine(HrtfMode InitialHrtfMode = HrtfMode::Automatic);
	~AudioEngine();
	AudioEngine(const AudioEngine&) = delete;
	AudioEngine& operator=(const AudioEngine&) = delete;

	[[nodiscard]] bool isAvailable() const noexcept { return m_Device != nullptr; }

	// - Sounds
	// Opens the file and decodes it whole or prepares it for streaming. nullptr (and logged) if it can't be read.
	[[nodiscard]] std::shared_ptr<SoundAsset> createSound(const std::filesystem::path& AudioFilePath);
	// Frees a sound's samples. Only once nothing plays it anymore (voices hold the asset, so: once nothing else holds it).
	void releaseSound(SoundAsset& SoundToRelease);

	// - Voices (game thread)
	// A sound something owns and controls (never taken over for another sound). Empty handle if it couldn't start.
	VoiceHandle play(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings);
	// Fire and forget (gunshots, pickups...): may be cut short when every voice is busy. The handle can still stop it.
	VoiceHandle playOneShot(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings);
	void stop(VoiceHandle VoiceToStop);
	[[nodiscard]] bool isPlaying(VoiceHandle Voice) const;
	void setVoicePosition(VoiceHandle Voice, const glm::vec3& WorldPosition);
	void setVoiceVolumeAndPitch(VoiceHandle Voice, float Volume, float Pitch);

	// - Listener and output
	void setListener(const glm::vec3& WorldPosition, const glm::vec3& ForwardDirection, const glm::vec3& UpDirection);
	void setOutputGain(float OutputGain);	// Everything, 0 = silent (volume, mute, focus muting all end up here)
	// Switches HRTF live (OpenAL Soft resets the output, playing sounds carry on). Free to call every frame: only a change does anything.
	void setHrtfMode(HrtfMode RequestedHrtfMode);
	[[nodiscard]] HrtfMode getHrtfMode() const noexcept { return m_HrtfMode; }

	// - Per frame (game thread): everything between these two reaches OpenAL as one batch
	void beginFrameUpdates();
	void endFrameUpdates();

	[[nodiscard]] AudioStatistics getStatistics() const;

private:
	static constexpr std::uint32_t c_StreamBufferCount{ 4 };
	static constexpr double c_StreamBufferDurationInSeconds{ 0.25 };
	static constexpr double c_StreamingThresholdInSeconds{ 10.0 };	// Longer than this: streamed

	enum class StreamPhase { None, WaitingToStart, Streaming, Finished };

	// One OpenAL source. Allocation (m_IsInUse, m_ReuseCount) is only ever changed by the game thread; everything the
	// streaming thread also touches is behind m_VoiceMutex (one per voice, so decoding one stream never blocks another).
	struct Voice
	{
		std::uint32_t m_SourceName{ 0 };
		std::uint32_t m_ReuseCount{ 0 };
		bool m_IsInUse{ false };
		bool m_IsOneShot{ false };
		std::uint64_t m_StartOrder{ 0 };	// Which voice to take over first (the oldest one-shot)

		std::mutex m_VoiceMutex;
		std::shared_ptr<const SoundAsset> m_Sound;
		bool m_ShouldLoop{ false };
		StreamPhase m_StreamPhase{ StreamPhase::None };
		std::unique_ptr<AudioDecoder> m_StreamDecoder;
		std::array<std::uint32_t, c_StreamBufferCount> m_StreamBufferNames{};
		bool m_HasStreamReachedEnd{ false };
	};

	VoiceHandle startVoice(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings, bool IsOneShot);
	[[nodiscard]] Voice* findVoice(VoiceHandle Handle) noexcept;
	[[nodiscard]] const Voice* findVoice(VoiceHandle Handle) const noexcept;
	void stopVoiceWhileLocked(Voice& VoiceToStop);	// Its m_VoiceMutex is held
	void freeVoice(Voice& VoiceToFree);
	void reapFinishedVoices();
	void refreshHrtfState();	// Reads back what OpenAL Soft actually did with the HRTF request

	// - Streaming thread
	void runStreamingThread();
	void serviceStreamingVoice(Voice& StreamingVoice, std::vector<std::int16_t>& DecodeScratch);	// Its m_VoiceMutex is held
	// Decodes up to one buffer's worth (looping back to the start if the voice loops). False = nothing left to play.
	bool fillStreamBuffer(Voice& StreamingVoice, std::uint32_t BufferName, std::vector<std::int16_t>& DecodeScratch);

	ALCdevice* m_Device{ nullptr };
	ALCcontext* m_Context{ nullptr };
	std::string m_DeviceName;
	HrtfMode m_HrtfMode{ HrtfMode::Automatic };
	bool m_IsHrtfEnabled{ false };
	std::string m_HrtfStatusDescription;
	std::string m_HrtfName;
	bool m_IsHrtfExtensionSupported{ false };
	bool m_IsOutputModeExtensionSupported{ false };
	bool m_IsPerSourceDistanceModelSupported{ false };
	bool m_IsStereoSpatialisationSupported{ false };
	bool m_IsDeferredUpdatesSupported{ false };

	std::vector<std::unique_ptr<Voice>> m_Voices;	// Fixed size once started: the streaming thread can walk it safely
	std::uint64_t m_NextStartOrder{ 1 };
	std::unordered_set<std::uint32_t> m_LiveSoundBufferNames;	// Deleted at shutdown if their sounds weren't released
	bool m_HasWarnedAboutNoFreeVoice{ false };

	std::thread m_StreamingThread;
	std::mutex m_StreamingWakeMutex;
	std::condition_variable m_StreamingWakeCondition;
	std::atomic<bool> m_ShouldStreamingThreadStop{ false };
	std::atomic<std::uint64_t> m_StreamUnderrunCount{ 0 };
	std::uint64_t m_StolenVoiceCount{ 0 };
};

}

#endif // !HEPHAESTIAN_AUDIOENGINE_H
