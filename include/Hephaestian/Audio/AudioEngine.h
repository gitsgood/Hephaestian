/**
 * @file AudioEngine.h
 * @brief Everything audio, on top of OpenAL Soft (mixing, 3D panning and HRTF run on its own thread).
 *
 * - **Sounds** (SoundAsset): loaded once and shared. Short ones (10 s or less) are decoded whole into one OpenAL
 *   buffer that every voice playing them shares; longer ones (music, ambience) are **streamed**: decoded a quarter
 *   second at a time while they play, so a 4 minute track costs ~200 KB instead of ~40 MB. Picked automatically.
 * - **Voices**: a fixed pool of OpenAL sources, made once. AudioEngine::play is for sounds something owns (an
 *   AudioSourceComponent); AudioEngine::playOneShot is for fire-and-forget sounds, which are the ones taken over when
 *   every voice is busy (the oldest one that doesn't loop first; looping ones only once nothing else is left).
 *   A VoiceHandle is a slot plus a reuse count, like texture identifiers: a stale one never touches someone else's sound.
 * - **Streaming thread**: keeps every streaming voice about a second ahead (4 buffers of 250 ms), loops without a gap,
 *   and restarts a voice that ran dry (counted as an underrun: there should never be any).
 * - **Per frame** (game thread): beginFrameUpdates(), then positions, listener and volume, then endFrameUpdates():
 *   OpenAL applies the whole frame's changes at once (AL_SOFT_deferred_updates), and finished voices go back to the pool.
 *
 * No audio device (or OpenAL failing to start): logged once, and everything quietly does nothing. Scenes still load.
 */
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
#include <vector>

#include <glm/glm.hpp>

#include "Hephaestian/Core/HashMap.h"

// OpenAL's own types (AL/alc.h), so this header doesn't pull in OpenAL
struct ALCdevice;
struct ALCcontext;

/**
 * @brief Sound: decoding, playback, 3D placement and output settings.
 */
namespace Hephaestian::Audio
{

class AudioDecoder;

/**
 * @brief A sound file, made by AudioEngine::createSound (the AssetLibrary caches them by path).
 */
struct SoundAsset
{
	std::filesystem::path m_SourceFilePath;		///< The file it was made from (streamed sounds reopen it for every voice)
	std::uint32_t m_ChannelCount{ 0 };			///< 1 (mono) or 2 (stereo)
	std::uint32_t m_SampleRateInHertz{ 0 };		///< The file's sample rate
	double m_DurationInSeconds{ 0.0 };			///< Estimated for MP3
	bool m_IsStreamed{ false };					///< True for sounds longer than 10 s: decoded while playing instead of up front
	std::uint32_t m_DecodedBufferName{ 0 };		///< OpenAL buffer with every sample (decoded sounds only, 0 otherwise)
};

/**
 * @brief Refers to one playing sound. A slot plus a reuse count: once that sound is over, the handle simply stops
 * working, even if its voice is playing something else by then.
 */
struct VoiceHandle
{
	std::uint32_t m_Value{ 0 };	///< 0 = no voice

	/**
	 * @brief Whether this refers to a voice at all.
	 * @return False for an empty handle (which every AudioEngine function accepts and ignores).
	 */
	[[nodiscard]] bool isSet() const noexcept { return m_Value != 0; }
	/**
	 * @brief Compares two handles.
	 * @return True if both refer to the same playback.
	 */
	[[nodiscard]] bool operator==(const VoiceHandle&) const = default;
};

/**
 * @brief How a 3D sound gets quieter with distance.
 */
enum class DistanceFalloff
{
	Inverse,	///< Like real sound: halves with each doubling of distance past the minimum, never quite silent
	Linear		///< Straight down from full at the minimum distance to silent at the maximum
};

/**
 * @brief How a voice plays its sound.
 */
struct SoundPlaybackSettings
{
	float m_Volume{ 1.0f };		///< 0 to 1 (OpenAL caps it at 1)
	float m_Pitch{ 1.0f };		///< Also speeds it up / slows it down
	bool m_ShouldLoop{ false };	///< Starts again at the end, without a gap (streamed sounds too)
	/// 3D: placed at m_WorldPosition, panned and faded by distance. Otherwise the same in both ears, wherever the listener is.
	bool m_IsSpatial{ false };
	glm::vec3 m_WorldPosition{ 0.0f };	///< Where a 3D sound is (ignored otherwise)
	float m_MinimumDistance{ 1.0f };	///< Full volume up to here
	float m_MaximumDistance{ 50.0f };	///< Inverse: stops getting quieter past here. Linear: silent from here.
	float m_Rolloff{ 1.0f };			///< How quickly it fades in between
	DistanceFalloff m_Falloff{ DistanceFalloff::Inverse };	///< The shape of the fade
};

/**
 * @brief HRTF: makes stereo sound come from all around (above, behind...) through headphones. Wrong on speakers.
 */
enum class HrtfMode
{
	Automatic,	///< OpenAL Soft decides: on when Windows says the output is headphones (cards that switch to headphones themselves may still say speakers)
	On,			///< Always, and the output is made stereo for it
	Off			///< Plain stereo panning
};

/**
 * @brief Reads an HRTF mode from its settings file name.
 * @param HrtfModeName "auto", "on" or "off".
 * @return The mode, or std::nullopt for anything else.
 */
[[nodiscard]] std::optional<HrtfMode> parseHrtfModeName(std::string_view HrtfModeName) noexcept;
/**
 * @brief An HRTF mode's settings file name.
 * @param Mode The mode.
 * @return "auto", "on" or "off".
 */
[[nodiscard]] std::string_view getHrtfModeName(HrtfMode Mode) noexcept;

/**
 * @brief What the audio is doing, for the settings window. Made by AudioEngine::getStatistics.
 */
struct AudioStatistics
{
	bool m_IsDeviceOpen{ false };				///< False when running silent
	std::string m_DeviceName;					///< The output device, as OpenAL names it
	bool m_IsHrtfEnabled{ false };				///< Whether HRTF is actually on
	std::string m_HrtfStatusDescription;		///< Why it's on or off ("on, headphones detected", "off, output isn't stereo"...)
	std::string m_HrtfName;						///< Which head it uses (empty while off)
	std::uint32_t m_VoiceCapacity{ 0 };			///< How many sounds can play at once
	std::uint32_t m_VoicesInUse{ 0 };			///< How many are playing
	std::uint32_t m_StreamingVoices{ 0 };		///< How many of those are streamed
	std::uint64_t m_StreamUnderrunCount{ 0 };	///< Times a stream ran dry and had to be restarted (should stay 0)
	std::uint64_t m_StolenVoiceCount{ 0 };		///< One-shots cut short to make room
};

/**
 * @brief The audio engine: one per program, made before anything that holds sounds (see the file description).
 *
 * Everything except the streaming thread runs on the game thread.
 */
class AudioEngine
{
public:
	/**
	 * @brief Opens the default output device, makes the voice pool and starts the streaming thread.
	 * @param InitialHrtfMode The HRTF choice to start with (so the output isn't rebuilt on the first frame).
	 */
	explicit AudioEngine(HrtfMode InitialHrtfMode = HrtfMode::Automatic);
	/** @brief Stops the streaming thread and every voice, frees what's left, and closes the device. */
	~AudioEngine();
	AudioEngine(const AudioEngine&) = delete;
	AudioEngine& operator=(const AudioEngine&) = delete;

	/**
	 * @brief Whether there's a device to play on.
	 * @return False when running silent (everything still works, it just doesn't make a sound).
	 */
	[[nodiscard]] bool isAvailable() const noexcept { return m_Device != nullptr; }

	// - Sounds
	/**
	 * @brief Opens a sound file, and decodes it whole or prepares it for streaming (depending on its length).
	 * @param AudioFilePath An .mp3, .wav, .flac or .ogg file.
	 * @return The sound, or nullptr (and logged) if it can't be read.
	 */
	[[nodiscard]] std::shared_ptr<SoundAsset> createSound(const std::filesystem::path& AudioFilePath);
	/**
	 * @brief Frees a sound's samples.
	 *
	 * Only once nothing plays it anymore: voices hold the asset, so once nothing else holds it.
	 * @param SoundToRelease The sound to free (it stays a valid object, with no samples).
	 */
	void releaseSound(SoundAsset& SoundToRelease);

	// - Voices (game thread)
	/**
	 * @brief Plays a sound something owns and controls (never taken over for another sound).
	 * @param SoundToPlay The sound (the voice holds on to it while playing).
	 * @param PlaybackSettings Volume, pitch, looping and 3D placement.
	 * @return The voice, or an empty handle if it couldn't start (no device, or every voice taken by owned sounds).
	 */
	VoiceHandle play(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings);
	/**
	 * @brief Fire and forget (gunshots, pickups...): may be cut short when every voice is busy.
	 * @param SoundToPlay The sound (the voice holds on to it while playing).
	 * @param PlaybackSettings Volume, pitch, looping and 3D placement.
	 * @return The voice (it can still be stopped with it), or an empty handle if it couldn't start.
	 */
	VoiceHandle playOneShot(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings);
	/**
	 * @brief Stops a voice and gives it back to the pool.
	 * @param VoiceToStop The voice (empty or stale handles are ignored).
	 */
	void stop(VoiceHandle VoiceToStop);
	/**
	 * @brief Whether a voice is still playing its sound.
	 * @param Voice The voice to ask about.
	 * @return False once it finished, was stopped or taken over (or for an empty handle).
	 */
	[[nodiscard]] bool isPlaying(VoiceHandle Voice) const;
	/**
	 * @brief Moves a 3D voice.
	 * @param Voice The voice (ignored if it's no longer playing).
	 * @param WorldPosition Where it is now.
	 */
	void setVoicePosition(VoiceHandle Voice, const glm::vec3& WorldPosition);
	/**
	 * @brief Changes a voice's volume and pitch while it plays.
	 * @param Voice The voice (ignored if it's no longer playing).
	 * @param Volume 0 to 1.
	 * @param Pitch 1 is normal.
	 */
	void setVoiceVolumeAndPitch(VoiceHandle Voice, float Volume, float Pitch);

	// - Listener and output
	/**
	 * @brief Places the ears every 3D sound is heard from.
	 * @param WorldPosition Where the listener is.
	 * @param ForwardDirection Where it faces (normalised).
	 * @param UpDirection Its up (normalised).
	 */
	void setListener(const glm::vec3& WorldPosition, const glm::vec3& ForwardDirection, const glm::vec3& UpDirection);
	/**
	 * @brief The volume of everything.
	 * @param OutputGain 0 = silent. Volume, mute and focus muting all end up here.
	 */
	void setOutputGain(float OutputGain);
	/**
	 * @brief Switches HRTF live: OpenAL Soft resets the output, and playing sounds carry on.
	 *
	 * Free to call every frame: only a change does anything.
	 * @param RequestedHrtfMode The mode to use from now on.
	 */
	void setHrtfMode(HrtfMode RequestedHrtfMode);
	/**
	 * @brief The HRTF mode asked for (getStatistics says whether it's actually on).
	 * @return The last mode passed to setHrtfMode or the constructor.
	 */
	[[nodiscard]] HrtfMode getHrtfMode() const noexcept { return m_HrtfMode; }

	// - Per frame (game thread): everything between these two reaches OpenAL as one batch
	/** @brief Starts holding this frame's changes back, so OpenAL applies them all at once. */
	void beginFrameUpdates();
	/** @brief Applies the frame's changes in one go, and gives finished voices back to the pool. */
	void endFrameUpdates();

	/**
	 * @brief What the audio is doing.
	 * @return Device, HRTF, voice and streaming figures.
	 */
	[[nodiscard]] AudioStatistics getStatistics() const;

private:
	static constexpr std::uint32_t c_StreamBufferCount{ 4 };				///< Buffers queued per streaming voice
	static constexpr double c_StreamBufferDurationInSeconds{ 0.25 };		///< Each one's length (so about a second ahead)
	static constexpr double c_StreamingThresholdInSeconds{ 10.0 };			///< Longer than this: streamed

	/**
	 * @brief Where a streaming voice is.
	 */
	enum class StreamPhase
	{
		None,			///< Not a streaming voice
		WaitingToStart,	///< Started by the game thread: the streaming thread fills its buffers and starts it
		Streaming,		///< Playing, kept topped up
		Finished		///< Played to the end (or the file couldn't be opened): the game thread frees the voice
	};

	/**
	 * @brief One OpenAL source.
	 *
	 * Allocation (m_IsInUse, m_ReuseCount) is only ever changed by the game thread; everything the streaming thread also
	 * touches is behind m_VoiceMutex (one per voice, so decoding one stream never blocks another).
	 */
	struct Voice
	{
		std::uint32_t m_SourceName{ 0 };	///< The OpenAL source
		std::uint32_t m_ReuseCount{ 0 };	///< Bumped every time the voice is freed, so old handles stop working
		bool m_IsInUse{ false };			///< Playing (or about to)
		bool m_IsOneShot{ false };			///< May be taken over when every voice is busy
		std::uint64_t m_StartOrder{ 0 };	///< Which voice to take over first (the oldest one-shot)

		std::mutex m_VoiceMutex;					///< Guards everything below (shared with the streaming thread)
		std::shared_ptr<const SoundAsset> m_Sound;	///< What it's playing (kept alive while it plays)
		bool m_ShouldLoop{ false };					///< Loops (streamed sounds rewind their decoder)
		StreamPhase m_StreamPhase{ StreamPhase::None };	///< See StreamPhase
		std::unique_ptr<AudioDecoder> m_StreamDecoder;	///< This voice's own read position in a streamed file
		std::array<std::uint32_t, c_StreamBufferCount> m_StreamBufferNames{};	///< The OpenAL buffers it cycles through
		bool m_HasStreamReachedEnd{ false };		///< The decoder has nothing left (not looping)
	};

	/**
	 * @brief Takes a free voice (or the one to take over) and starts the sound on it.
	 * @param SoundToPlay The sound.
	 * @param PlaybackSettings How to play it.
	 * @param IsOneShot Whether this voice may be taken over later.
	 * @return The voice, or an empty handle if none could be had.
	 */
	VoiceHandle startVoice(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings, bool IsOneShot);
	/**
	 * @brief Finds the voice a handle refers to.
	 * @param Handle The handle.
	 * @return The voice, or nullptr if the handle is empty or stale.
	 */
	[[nodiscard]] Voice* findVoice(VoiceHandle Handle) noexcept;
	/**
	 * @brief Finds the voice a handle refers to.
	 * @param Handle The handle.
	 * @return The voice, or nullptr if the handle is empty or stale.
	 */
	[[nodiscard]] const Voice* findVoice(VoiceHandle Handle) const noexcept;
	/**
	 * @brief Stops the source and unqueues its buffers. Its m_VoiceMutex must be held.
	 * @param VoiceToStop The voice.
	 */
	void stopVoiceWhileLocked(Voice& VoiceToStop);
	/**
	 * @brief Stops a voice and returns it to the pool (bumping its reuse count).
	 * @param VoiceToFree The voice.
	 */
	void freeVoice(Voice& VoiceToFree);
	/** @brief Gives voices whose sound has finished back to the pool. */
	void reapFinishedVoices();
	/** @brief Reads back what OpenAL Soft actually did with the HRTF request. */
	void refreshHrtfState();

	// - Streaming thread
	/** @brief The streaming thread: services every streaming voice, then waits about 20 ms (or until woken). */
	void runStreamingThread();
	/**
	 * @brief Starts a waiting stream, refills the buffers it has played, and restarts it if it ran dry. Its
	 * m_VoiceMutex must be held.
	 * @param StreamingVoice The voice.
	 * @param DecodeScratch Reused memory to decode into.
	 */
	void serviceStreamingVoice(Voice& StreamingVoice, std::vector<std::int16_t>& DecodeScratch);
	/**
	 * @brief Decodes up to one buffer's worth (looping back to the start if the voice loops).
	 * @param StreamingVoice The voice.
	 * @param BufferName The OpenAL buffer to fill.
	 * @param DecodeScratch Reused memory to decode into.
	 * @return False if there's nothing left to play.
	 */
	bool fillStreamBuffer(Voice& StreamingVoice, std::uint32_t BufferName, std::vector<std::int16_t>& DecodeScratch);

	ALCdevice* m_Device{ nullptr };		///< The output device (null when running silent)
	ALCcontext* m_Context{ nullptr };	///< OpenAL's context on it
	std::string m_DeviceName;			///< As OpenAL names it
	HrtfMode m_HrtfMode{ HrtfMode::Automatic };	///< What was asked for
	bool m_IsHrtfEnabled{ false };				///< What OpenAL Soft did
	std::string m_HrtfStatusDescription;		///< Why it's on or off
	std::string m_HrtfName;						///< Which HRTF (empty while off)
	bool m_IsHrtfExtensionSupported{ false };			///< ALC_SOFT_HRTF
	bool m_IsOutputModeExtensionSupported{ false };		///< ALC_SOFT_output_mode (forcing stereo for HRTF)
	bool m_IsPerSourceDistanceModelSupported{ false };	///< AL_EXT_source_distance_model (inverse or linear per voice)
	bool m_IsStereoSpatialisationSupported{ false };	///< AL_SOFT_source_spatialize (3D stereo sounds)
	bool m_IsDeferredUpdatesSupported{ false };			///< AL_SOFT_deferred_updates (one batch per frame)

	std::vector<std::unique_ptr<Voice>> m_Voices;	///< Fixed size once started: the streaming thread can walk it safely
	std::uint64_t m_NextStartOrder{ 1 };			///< Given to each voice as it starts
	HashSet<std::uint32_t> m_LiveSoundBufferNames;	///< Deleted at shutdown if their sounds weren't released
	bool m_HasWarnedAboutNoFreeVoice{ false };		///< So running out of voices is logged once

	std::thread m_StreamingThread;						///< See runStreamingThread
	std::mutex m_StreamingWakeMutex;					///< For m_StreamingWakeCondition
	std::condition_variable m_StreamingWakeCondition;	///< Wakes the streaming thread early (a stream started, or shutdown)
	std::atomic<bool> m_ShouldStreamingThreadStop{ false };	///< Set at shutdown
	std::atomic<std::uint64_t> m_StreamUnderrunCount{ 0 };	///< See AudioStatistics::m_StreamUnderrunCount
	std::uint64_t m_StolenVoiceCount{ 0 };					///< See AudioStatistics::m_StolenVoiceCount
};

}

#endif // !HEPHAESTIAN_AUDIOENGINE_H
