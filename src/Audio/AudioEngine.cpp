#include "Hephaestian/Audio/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <span>
#include <utility>

#define AL_ALEXT_PROTOTYPES	// Declares the extension functions OpenAL Soft exports (alDeferUpdatesSOFT...)
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <Vulkanised/Logger.h>

#include "Hephaestian/Audio/AudioDecoder.h"

namespace Hephaestian::Audio
{

namespace
{
	constexpr std::uint32_t c_MaximumVoiceCount{ 256 };
	constexpr std::uint32_t c_VoiceSlotBitCount{ 16 };
	constexpr std::uint32_t c_VoiceSlotMask{ (1u << c_VoiceSlotBitCount) - 1 };
	constexpr std::uint32_t c_VoiceReuseCountLimit{ 1u << 15 };	// Handles stay below 2^31
	constexpr auto c_StreamingThreadInterval{ std::chrono::milliseconds(20) };

	[[nodiscard]] ALenum getSixteenBitFormat(std::uint32_t ChannelCount) noexcept
	{
		return ChannelCount == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	}

	[[nodiscard]] VoiceHandle makeVoiceHandle(std::uint32_t VoiceSlot, std::uint32_t ReuseCount) noexcept
	{
		return { ((ReuseCount << c_VoiceSlotBitCount) | VoiceSlot) + 1 };
	}

	// The context/reset attributes for an HRTF mode (zero terminated). "On" also asks for stereo HRTF output, or a card set to
	// 5.1/7.1 in Windows would keep its surround output and refuse HRTF ("output isn't stereo").
	[[nodiscard]] std::array<ALCint, 5> makeHrtfAttributes(HrtfMode Mode, bool IsHrtfExtensionSupported, bool IsOutputModeExtensionSupported) noexcept
	{
		std::array<ALCint, 5> Attributes{};
		std::size_t NextAttributeIndex{ 0 };
		if (IsHrtfExtensionSupported)
		{
			Attributes[NextAttributeIndex++] = ALC_HRTF_SOFT;
			Attributes[NextAttributeIndex++] = Mode == HrtfMode::On ? ALC_TRUE : Mode == HrtfMode::Off ? ALC_FALSE : ALC_DONT_CARE_SOFT;
		}
		if (Mode == HrtfMode::On && IsOutputModeExtensionSupported)
		{
			Attributes[NextAttributeIndex++] = ALC_OUTPUT_MODE_SOFT;
			Attributes[NextAttributeIndex++] = ALC_STEREO_HRTF_SOFT;
		}
		return Attributes;
	}

	[[nodiscard]] std::string_view describeHrtfStatus(ALCint HrtfStatus) noexcept
	{
		switch (HrtfStatus)
		{
		case ALC_HRTF_ENABLED_SOFT: return "on";
		case ALC_HRTF_HEADPHONES_DETECTED_SOFT: return "on, headphones detected";
		case ALC_HRTF_REQUIRED_SOFT: return "on, required by alsoft.ini";
		case ALC_HRTF_DENIED_SOFT: return "off, blocked by alsoft.ini";
		case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT: return "off, output isn't stereo";
		default: return "off";
		}
	}
}

std::optional<HrtfMode> parseHrtfModeName(std::string_view HrtfModeName) noexcept
{
	if (HrtfModeName == "auto") { return HrtfMode::Automatic; }
	if (HrtfModeName == "on") { return HrtfMode::On; }
	if (HrtfModeName == "off") { return HrtfMode::Off; }
	return std::nullopt;
}

std::string_view getHrtfModeName(HrtfMode Mode) noexcept
{
	switch (Mode)
	{
	case HrtfMode::On: return "on";
	case HrtfMode::Off: return "off";
	default: return "auto";
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// STARTING AND STOPPING
// ---------------------------------------------------------------------------------------------------------------------
AudioEngine::AudioEngine(HrtfMode InitialHrtfMode)
	: m_HrtfMode(InitialHrtfMode)
{
	// The system's default output (OpenAL Soft follows it if it changes, on most backends)
	m_Device = alcOpenDevice(nullptr);
	if (m_Device == nullptr)
	{
		LOGW("No audio device could be opened: running without sound.");
		return;
	}
	// HRTF picked at creation, so the output isn't reset straight after
	m_IsHrtfExtensionSupported = alcIsExtensionPresent(m_Device, "ALC_SOFT_HRTF") == ALC_TRUE;
	m_IsOutputModeExtensionSupported = alcIsExtensionPresent(m_Device, "ALC_SOFT_output_mode") == ALC_TRUE;
	const std::array<ALCint, 5> HrtfAttributes = makeHrtfAttributes(m_HrtfMode, m_IsHrtfExtensionSupported, m_IsOutputModeExtensionSupported);
	m_Context = alcCreateContext(m_Device, HrtfAttributes.data());
	if (m_Context == nullptr || alcMakeContextCurrent(m_Context) == ALC_FALSE)
	{
		LOGE("OpenAL couldn't create its context: running without sound.");
		if (m_Context != nullptr)
		{
			alcDestroyContext(m_Context);
			m_Context = nullptr;
		}
		alcCloseDevice(m_Device);
		m_Device = nullptr;
		return;
	}

	const ALCchar* DeviceName = alcIsExtensionPresent(m_Device, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE
		? alcGetString(m_Device, ALC_ALL_DEVICES_SPECIFIER) : alcGetString(m_Device, ALC_DEVICE_SPECIFIER);
	m_DeviceName = DeviceName != nullptr ? DeviceName : "Unknown device";
	refreshHrtfState();
	m_IsPerSourceDistanceModelSupported = alIsExtensionPresent("AL_EXT_source_distance_model") == AL_TRUE;
	m_IsStereoSpatialisationSupported = alIsExtensionPresent("AL_SOFT_source_spatialize") == AL_TRUE;
	m_IsDeferredUpdatesSupported = alIsExtensionPresent("AL_SOFT_deferred_updates") == AL_TRUE;

	// Each source picks its own falloff (inverse or linear); no doppler (nothing reports velocities)
	if (m_IsPerSourceDistanceModelSupported)
	{
		alEnable(AL_SOURCE_DISTANCE_MODEL);
	}
	alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
	alDopplerFactor(0.0f);

	// THE VOICE POOL: as many sources as the device mixes at once (capped), made once and never again
	ALCint MonoSourceLimit{ 0 };
	alcGetIntegerv(m_Device, ALC_MONO_SOURCES, 1, &MonoSourceLimit);
	const std::uint32_t WantedVoiceCount = MonoSourceLimit > 0 ? std::min(static_cast<std::uint32_t>(MonoSourceLimit), c_MaximumVoiceCount) : c_MaximumVoiceCount;
	for (std::uint32_t CurrentVoiceIndex{ 0 }; CurrentVoiceIndex < WantedVoiceCount; ++CurrentVoiceIndex)
	{
		ALuint NewSourceName{ 0 };
		alGetError();
		alGenSources(1, &NewSourceName);
		if (alGetError() != AL_NO_ERROR)
		{
			break;	// That's all the device gives
		}
		auto NewVoice = std::make_unique<Voice>();
		NewVoice->m_SourceName = NewSourceName;
		m_Voices.push_back(std::move(NewVoice));
	}

	LOG("Audio: {} ({} voices, HRTF {}: {}).", m_DeviceName, m_Voices.size(), getHrtfModeName(m_HrtfMode), m_HrtfStatusDescription);
	m_StreamingThread = std::thread(&AudioEngine::runStreamingThread, this);
}

AudioEngine::~AudioEngine()
{
	if (m_Device == nullptr)
	{
		return;
	}

	m_ShouldStreamingThreadStop = true;
	m_StreamingWakeCondition.notify_all();
	if (m_StreamingThread.joinable())
	{
		m_StreamingThread.join();
	}
	LOG("Audio: shutting down. Over the session: {} stream underrun(s), {} one-shot(s) cut short for room.", m_StreamUnderrunCount.load(), m_StolenVoiceCount);

	for (const std::unique_ptr<Voice>& CurrentVoice : m_Voices)
	{
		{
			const std::scoped_lock VoiceLock(CurrentVoice->m_VoiceMutex);
			stopVoiceWhileLocked(*CurrentVoice);
		}
		alDeleteSources(1, &CurrentVoice->m_SourceName);
		alDeleteBuffers(static_cast<ALsizei>(c_StreamBufferCount), CurrentVoice->m_StreamBufferNames.data());	// Zeros (never made) are skipped
	}
	m_Voices.clear();
	for (const std::uint32_t RemainingBufferName : m_LiveSoundBufferNames)
	{
		alDeleteBuffers(1, &RemainingBufferName);
	}

	alcMakeContextCurrent(nullptr);
	alcDestroyContext(m_Context);
	alcCloseDevice(m_Device);
}

// ---------------------------------------------------------------------------------------------------------------------
// SOUNDS
// ---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<SoundAsset> AudioEngine::createSound(const std::filesystem::path& AudioFilePath)
{
	std::unique_ptr<AudioDecoder> Decoder = AudioDecoder::openFile(AudioFilePath);	// Logs its own problems
	if (Decoder == nullptr)
	{
		return nullptr;
	}

	auto NewSound = std::make_shared<SoundAsset>();
	NewSound->m_SourceFilePath = AudioFilePath;
	NewSound->m_ChannelCount = Decoder->getChannelCount();
	NewSound->m_SampleRateInHertz = Decoder->getSampleRateInHertz();
	NewSound->m_DurationInSeconds = Decoder->estimateDurationInSeconds();
	NewSound->m_IsStreamed = NewSound->m_DurationInSeconds > c_StreamingThresholdInSeconds;
	if (NewSound->m_IsStreamed || !isAvailable())
	{
		return NewSound;	// Streamed: each voice playing it opens its own decoder. No device: nothing to decode for.
	}

	// Short: decoded whole, once, into one buffer every voice shares
	std::vector<std::int16_t> DecodedSamples;
	std::vector<std::int16_t> DecodeChunk(static_cast<std::size_t>(NewSound->m_SampleRateInHertz) * NewSound->m_ChannelCount);	// A second at a time
	while (true)
	{
		const std::uint64_t DecodedFrameCount = Decoder->readFrames(DecodeChunk);
		DecodedSamples.insert(DecodedSamples.end(), DecodeChunk.begin(), DecodeChunk.begin() + static_cast<std::ptrdiff_t>(DecodedFrameCount * NewSound->m_ChannelCount));
		if (DecodedFrameCount * NewSound->m_ChannelCount < DecodeChunk.size())
		{
			break;
		}
	}
	if (DecodedSamples.empty())
	{
		LOGE("Audio file {} has no samples in it.", AudioFilePath.string());
		return nullptr;
	}

	ALuint DecodedBufferName{ 0 };
	alGenBuffers(1, &DecodedBufferName);
	alBufferData(DecodedBufferName, getSixteenBitFormat(NewSound->m_ChannelCount), DecodedSamples.data(),
		static_cast<ALsizei>(DecodedSamples.size() * sizeof(std::int16_t)), static_cast<ALsizei>(NewSound->m_SampleRateInHertz));
	NewSound->m_DecodedBufferName = DecodedBufferName;
	NewSound->m_DurationInSeconds = static_cast<double>(DecodedSamples.size() / NewSound->m_ChannelCount) / NewSound->m_SampleRateInHertz;	// Exact now
	m_LiveSoundBufferNames.insert(DecodedBufferName);
	return NewSound;
}

void AudioEngine::releaseSound(SoundAsset& SoundToRelease)
{
	if (SoundToRelease.m_DecodedBufferName != 0 && isAvailable())
	{
		alDeleteBuffers(1, &SoundToRelease.m_DecodedBufferName);
		m_LiveSoundBufferNames.erase(SoundToRelease.m_DecodedBufferName);
		SoundToRelease.m_DecodedBufferName = 0;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// VOICES
// ---------------------------------------------------------------------------------------------------------------------
VoiceHandle AudioEngine::play(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings)
{
	return startVoice(std::move(SoundToPlay), PlaybackSettings, false);
}

VoiceHandle AudioEngine::playOneShot(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings)
{
	return startVoice(std::move(SoundToPlay), PlaybackSettings, true);
}

VoiceHandle AudioEngine::startVoice(std::shared_ptr<const SoundAsset> SoundToPlay, const SoundPlaybackSettings& PlaybackSettings, bool IsOneShot)
{
	if (!isAvailable() || SoundToPlay == nullptr || m_Voices.empty())
	{
		return {};
	}

	// 1. A FREE VOICE, or else the oldest one-shot that doesn't loop (a looping one, ambience say, only as a last resort).
	//    Never a component's sound. (m_ShouldLoop is only ever written by this thread: reading it unlocked is fine.)
	Voice* ChosenVoice{ nullptr };
	for (const std::unique_ptr<Voice>& CurrentVoice : m_Voices)
	{
		if (!CurrentVoice->m_IsInUse)
		{
			ChosenVoice = CurrentVoice.get();
			break;
		}
	}
	if (ChosenVoice == nullptr)
	{
		auto isBetterVoiceToTake = [](const Voice& Candidate, const Voice& CurrentChoice)
		{
			if (Candidate.m_ShouldLoop != CurrentChoice.m_ShouldLoop)
			{
				return !Candidate.m_ShouldLoop;
			}
			return Candidate.m_StartOrder < CurrentChoice.m_StartOrder;
		};
		for (const std::unique_ptr<Voice>& CurrentVoice : m_Voices)
		{
			if (CurrentVoice->m_IsOneShot && (ChosenVoice == nullptr || isBetterVoiceToTake(*CurrentVoice, *ChosenVoice)))
			{
				ChosenVoice = CurrentVoice.get();
			}
		}
		if (ChosenVoice == nullptr)
		{
			if (!m_HasWarnedAboutNoFreeVoice)
			{
				LOGW("Every one of the {} voices is playing a sound something owns: {} can't start (warned once).", m_Voices.size(), SoundToPlay->m_SourceFilePath.filename().string());
				m_HasWarnedAboutNoFreeVoice = true;
			}
			return {};
		}
		const std::scoped_lock VoiceLock(ChosenVoice->m_VoiceMutex);
		stopVoiceWhileLocked(*ChosenVoice);
		freeVoice(*ChosenVoice);
		++m_StolenVoiceCount;
	}

	// 2. HOW IT SOUNDS: in the world, or the same in both ears
	const ALuint SourceName = ChosenVoice->m_SourceName;
	alSourcef(SourceName, AL_GAIN, std::max(PlaybackSettings.m_Volume, 0.0f));
	alSourcef(SourceName, AL_PITCH, std::clamp(PlaybackSettings.m_Pitch, 0.01f, 16.0f));
	if (PlaybackSettings.m_IsSpatial)
	{
		alSourcei(SourceName, AL_SOURCE_RELATIVE, AL_FALSE);
		alSource3f(SourceName, AL_POSITION, PlaybackSettings.m_WorldPosition.x, PlaybackSettings.m_WorldPosition.y, PlaybackSettings.m_WorldPosition.z);
		alSourcef(SourceName, AL_REFERENCE_DISTANCE, std::max(PlaybackSettings.m_MinimumDistance, 0.001f));
		alSourcef(SourceName, AL_MAX_DISTANCE, std::max(PlaybackSettings.m_MaximumDistance, PlaybackSettings.m_MinimumDistance));
		alSourcef(SourceName, AL_ROLLOFF_FACTOR, std::max(PlaybackSettings.m_Rolloff, 0.0f));
		if (m_IsPerSourceDistanceModelSupported)
		{
			alSourcei(SourceName, AL_DISTANCE_MODEL, PlaybackSettings.m_Falloff == DistanceFalloff::Linear ? AL_LINEAR_DISTANCE_CLAMPED : AL_INVERSE_DISTANCE_CLAMPED);
		}
	}
	else
	{
		// Glued to the listener, no fading: plain stereo (or centred mono)
		alSourcei(SourceName, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(SourceName, AL_POSITION, 0.0f, 0.0f, 0.0f);
		alSourcef(SourceName, AL_ROLLOFF_FACTOR, 0.0f);
	}
	if (m_IsStereoSpatialisationSupported)
	{
		// Stereo files are only placed in the world when asked to (they'd otherwise ignore their position entirely)
		alSourcei(SourceName, AL_SOURCE_SPATIALIZE_SOFT, PlaybackSettings.m_IsSpatial ? AL_TRUE : AL_FALSE);
	}

	// 3. START: a decoded sound right away; a streamed one is opened and filled by the streaming thread (no decoding here)
	ChosenVoice->m_IsInUse = true;
	ChosenVoice->m_IsOneShot = IsOneShot;
	ChosenVoice->m_StartOrder = m_NextStartOrder++;
	const bool IsStreamed = SoundToPlay->m_IsStreamed;
	{
		const std::scoped_lock VoiceLock(ChosenVoice->m_VoiceMutex);
		ChosenVoice->m_ShouldLoop = PlaybackSettings.m_ShouldLoop;
		if (IsStreamed)
		{
			alSourcei(SourceName, AL_LOOPING, AL_FALSE);	// Streams loop by rewinding their decoder instead
			ChosenVoice->m_StreamPhase = StreamPhase::WaitingToStart;
			ChosenVoice->m_HasStreamReachedEnd = false;
		}
		else
		{
			alSourcei(SourceName, AL_LOOPING, PlaybackSettings.m_ShouldLoop ? AL_TRUE : AL_FALSE);
			alSourcei(SourceName, AL_BUFFER, static_cast<ALint>(SoundToPlay->m_DecodedBufferName));
			alSourcePlay(SourceName);
		}
		ChosenVoice->m_Sound = std::move(SoundToPlay);
	}
	if (IsStreamed)
	{
		m_StreamingWakeCondition.notify_one();
	}

	const std::uint32_t VoiceSlot = static_cast<std::uint32_t>(std::ranges::find_if(m_Voices, [ChosenVoice](const std::unique_ptr<Voice>& CurrentVoice) { return CurrentVoice.get() == ChosenVoice; }) - m_Voices.begin());
	return makeVoiceHandle(VoiceSlot, ChosenVoice->m_ReuseCount);
}

AudioEngine::Voice* AudioEngine::findVoice(VoiceHandle Handle) noexcept
{
	return const_cast<Voice*>(std::as_const(*this).findVoice(Handle));
}

const AudioEngine::Voice* AudioEngine::findVoice(VoiceHandle Handle) const noexcept
{
	if (!Handle.isSet())
	{
		return nullptr;
	}
	const std::uint32_t HandleValue = Handle.m_Value - 1;
	const std::uint32_t VoiceSlot = HandleValue & c_VoiceSlotMask;
	if (VoiceSlot >= m_Voices.size())
	{
		return nullptr;
	}
	const Voice& FoundVoice = *m_Voices[VoiceSlot];
	return FoundVoice.m_IsInUse && FoundVoice.m_ReuseCount == (HandleValue >> c_VoiceSlotBitCount) ? &FoundVoice : nullptr;
}

void AudioEngine::stop(VoiceHandle VoiceToStop)
{
	if (Voice* FoundVoice = findVoice(VoiceToStop))
	{
		{
			const std::scoped_lock VoiceLock(FoundVoice->m_VoiceMutex);
			stopVoiceWhileLocked(*FoundVoice);
		}
		freeVoice(*FoundVoice);
	}
}

bool AudioEngine::isPlaying(VoiceHandle Voice) const
{
	return findVoice(Voice) != nullptr;	// Finished voices are freed every frame, so a live handle is a playing sound
}

void AudioEngine::setVoicePosition(VoiceHandle Voice, const glm::vec3& WorldPosition)
{
	if (const AudioEngine::Voice* FoundVoice = findVoice(Voice))
	{
		alSource3f(FoundVoice->m_SourceName, AL_POSITION, WorldPosition.x, WorldPosition.y, WorldPosition.z);
	}
}

void AudioEngine::setVoiceVolumeAndPitch(VoiceHandle Voice, float Volume, float Pitch)
{
	if (const AudioEngine::Voice* FoundVoice = findVoice(Voice))
	{
		alSourcef(FoundVoice->m_SourceName, AL_GAIN, std::max(Volume, 0.0f));
		alSourcef(FoundVoice->m_SourceName, AL_PITCH, std::clamp(Pitch, 0.01f, 16.0f));
	}
}

void AudioEngine::stopVoiceWhileLocked(Voice& VoiceToStop)
{
	alSourceStop(VoiceToStop.m_SourceName);
	alSourcei(VoiceToStop.m_SourceName, AL_BUFFER, 0);	// Detaches the buffer, or empties the stream's queue
	VoiceToStop.m_Sound.reset();
	VoiceToStop.m_StreamDecoder.reset();
	VoiceToStop.m_StreamPhase = StreamPhase::None;
}

void AudioEngine::freeVoice(Voice& VoiceToFree)
{
	VoiceToFree.m_IsInUse = false;
	VoiceToFree.m_ReuseCount = (VoiceToFree.m_ReuseCount + 1) % c_VoiceReuseCountLimit;	// Every handle to it dies now
}

void AudioEngine::reapFinishedVoices()
{
	for (const std::unique_ptr<Voice>& CurrentVoice : m_Voices)
	{
		if (!CurrentVoice->m_IsInUse)
		{
			continue;
		}
		// The streaming thread may be busy decoding this one: then it's simply checked again next frame
		std::unique_lock VoiceLock(CurrentVoice->m_VoiceMutex, std::try_to_lock);
		if (!VoiceLock.owns_lock())
		{
			continue;
		}

		bool HasFinished{ false };
		if (CurrentVoice->m_StreamPhase == StreamPhase::None)
		{
			ALint SourceState{ AL_INITIAL };
			alGetSourcei(CurrentVoice->m_SourceName, AL_SOURCE_STATE, &SourceState);
			HasFinished = SourceState == AL_STOPPED;
		}
		else
		{
			HasFinished = CurrentVoice->m_StreamPhase == StreamPhase::Finished;
		}
		if (HasFinished)
		{
			stopVoiceWhileLocked(*CurrentVoice);
			VoiceLock.unlock();
			freeVoice(*CurrentVoice);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// LISTENER, OUTPUT, PER FRAME
// ---------------------------------------------------------------------------------------------------------------------
void AudioEngine::setListener(const glm::vec3& WorldPosition, const glm::vec3& ForwardDirection, const glm::vec3& UpDirection)
{
	if (!isAvailable())
	{
		return;
	}
	const std::array<ALfloat, 6> Orientation{ ForwardDirection.x, ForwardDirection.y, ForwardDirection.z, UpDirection.x, UpDirection.y, UpDirection.z };
	alListener3f(AL_POSITION, WorldPosition.x, WorldPosition.y, WorldPosition.z);
	alListenerfv(AL_ORIENTATION, Orientation.data());
}

void AudioEngine::setOutputGain(float OutputGain)
{
	if (isAvailable())
	{
		alListenerf(AL_GAIN, std::max(OutputGain, 0.0f));
	}
}

void AudioEngine::setHrtfMode(HrtfMode RequestedHrtfMode)
{
	if (RequestedHrtfMode == m_HrtfMode)
	{
		return;
	}
	m_HrtfMode = RequestedHrtfMode;
	if (!isAvailable() || !m_IsHrtfExtensionSupported)
	{
		return;
	}

	// Rebuilds the output in place: sources, buffers and streams all survive (the mixer just pauses for a moment)
	const std::array<ALCint, 5> HrtfAttributes = makeHrtfAttributes(m_HrtfMode, m_IsHrtfExtensionSupported, m_IsOutputModeExtensionSupported);
	if (alcResetDeviceSOFT(m_Device, HrtfAttributes.data()) == ALC_FALSE)
	{
		LOGW("Audio: couldn't switch HRTF to {} (keeping the previous output).", getHrtfModeName(m_HrtfMode));
	}
	refreshHrtfState();
	LOG("Audio: HRTF {}: {}{}{}.", getHrtfModeName(m_HrtfMode), m_HrtfStatusDescription, m_HrtfName.empty() ? "" : ", ", m_HrtfName);
}

void AudioEngine::refreshHrtfState()
{
	if (!m_IsHrtfExtensionSupported)
	{
		m_IsHrtfEnabled = false;
		m_HrtfStatusDescription = "off, not supported";
		m_HrtfName.clear();
		return;
	}
	ALCint HrtfState{ ALC_FALSE };
	ALCint HrtfStatus{ ALC_HRTF_DISABLED_SOFT };
	alcGetIntegerv(m_Device, ALC_HRTF_SOFT, 1, &HrtfState);
	alcGetIntegerv(m_Device, ALC_HRTF_STATUS_SOFT, 1, &HrtfStatus);
	m_IsHrtfEnabled = HrtfState == ALC_TRUE;
	m_HrtfStatusDescription = describeHrtfStatus(HrtfStatus);
	const ALCchar* HrtfName = m_IsHrtfEnabled ? alcGetString(m_Device, ALC_HRTF_SPECIFIER_SOFT) : nullptr;
	m_HrtfName = HrtfName != nullptr ? HrtfName : "";
}

void AudioEngine::beginFrameUpdates()
{
	if (isAvailable() && m_IsDeferredUpdatesSupported)
	{
		alDeferUpdatesSOFT();
	}
}

void AudioEngine::endFrameUpdates()
{
	if (!isAvailable())
	{
		return;
	}
	if (m_IsDeferredUpdatesSupported)
	{
		alProcessUpdatesSOFT();
	}
	reapFinishedVoices();
}

AudioStatistics AudioEngine::getStatistics() const
{
	AudioStatistics Statistics{};
	Statistics.m_IsDeviceOpen = isAvailable();
	Statistics.m_DeviceName = m_DeviceName;
	Statistics.m_IsHrtfEnabled = m_IsHrtfEnabled;
	Statistics.m_HrtfStatusDescription = m_HrtfStatusDescription;
	Statistics.m_HrtfName = m_HrtfName;
	Statistics.m_VoiceCapacity = static_cast<std::uint32_t>(m_Voices.size());
	for (const std::unique_ptr<Voice>& CurrentVoice : m_Voices)
	{
		if (CurrentVoice->m_IsInUse)
		{
			++Statistics.m_VoicesInUse;
			if (CurrentVoice->m_Sound != nullptr && CurrentVoice->m_Sound->m_IsStreamed)	// (A glance: only shown in the UI)
			{
				++Statistics.m_StreamingVoices;
			}
		}
	}
	Statistics.m_StreamUnderrunCount = m_StreamUnderrunCount.load();
	Statistics.m_StolenVoiceCount = m_StolenVoiceCount;
	return Statistics;
}

// ---------------------------------------------------------------------------------------------------------------------
// STREAMING THREAD
// ---------------------------------------------------------------------------------------------------------------------
void AudioEngine::runStreamingThread()
{
	std::vector<std::int16_t> DecodeScratch;	// Reused for every buffer, never shrinks
	while (!m_ShouldStreamingThreadStop)
	{
		for (const std::unique_ptr<Voice>& CurrentVoice : m_Voices)
		{
			const std::scoped_lock VoiceLock(CurrentVoice->m_VoiceMutex);
			if (CurrentVoice->m_StreamPhase == StreamPhase::WaitingToStart || CurrentVoice->m_StreamPhase == StreamPhase::Streaming)
			{
				serviceStreamingVoice(*CurrentVoice, DecodeScratch);
			}
		}

		// Sleep until the next round, or until play() has a new stream to start
		std::unique_lock WakeLock(m_StreamingWakeMutex);
		m_StreamingWakeCondition.wait_for(WakeLock, c_StreamingThreadInterval);
	}
}

void AudioEngine::serviceStreamingVoice(Voice& StreamingVoice, std::vector<std::int16_t>& DecodeScratch)
{
	const ALuint SourceName = StreamingVoice.m_SourceName;

	// 1. A NEW STREAM: its own decoder, its buffers filled, then playing
	if (StreamingVoice.m_StreamPhase == StreamPhase::WaitingToStart)
	{
		StreamingVoice.m_StreamDecoder = AudioDecoder::openFile(StreamingVoice.m_Sound->m_SourceFilePath);
		if (StreamingVoice.m_StreamDecoder == nullptr)
		{
			StreamingVoice.m_StreamPhase = StreamPhase::Finished;	// Logged by openFile; the game thread frees the voice
			return;
		}
		if (StreamingVoice.m_StreamBufferNames[0] == 0)
		{
			alGenBuffers(static_cast<ALsizei>(c_StreamBufferCount), StreamingVoice.m_StreamBufferNames.data());	// Once per voice, then reused
		}
		for (const ALuint CurrentBufferName : StreamingVoice.m_StreamBufferNames)
		{
			if (!fillStreamBuffer(StreamingVoice, CurrentBufferName, DecodeScratch))
			{
				break;
			}
			alSourceQueueBuffers(SourceName, 1, &CurrentBufferName);
		}
		alSourcePlay(SourceName);
		StreamingVoice.m_StreamPhase = StreamPhase::Streaming;
		return;
	}

	// 2. REFILL whatever the mixer has finished playing, and queue it again behind the rest
	ALint ProcessedBufferCount{ 0 };
	alGetSourcei(SourceName, AL_BUFFERS_PROCESSED, &ProcessedBufferCount);
	while (ProcessedBufferCount-- > 0)
	{
		ALuint FinishedBufferName{ 0 };
		alSourceUnqueueBuffers(SourceName, 1, &FinishedBufferName);
		if (!StreamingVoice.m_HasStreamReachedEnd && fillStreamBuffer(StreamingVoice, FinishedBufferName, DecodeScratch))
		{
			alSourceQueueBuffers(SourceName, 1, &FinishedBufferName);
		}
	}

	// 3. STOPPED: either the end of a non-looping sound, or it ran dry (a long stall) and needs restarting
	ALint SourceState{ AL_PLAYING };
	alGetSourcei(SourceName, AL_SOURCE_STATE, &SourceState);
	if (SourceState == AL_STOPPED)
	{
		ALint QueuedBufferCount{ 0 };
		alGetSourcei(SourceName, AL_BUFFERS_QUEUED, &QueuedBufferCount);
		if (QueuedBufferCount == 0)
		{
			StreamingVoice.m_StreamPhase = StreamPhase::Finished;
		}
		else
		{
			alSourcePlay(SourceName);
			++m_StreamUnderrunCount;
		}
	}
}

bool AudioEngine::fillStreamBuffer(Voice& StreamingVoice, std::uint32_t BufferName, std::vector<std::int16_t>& DecodeScratch)
{
	AudioDecoder& Decoder = *StreamingVoice.m_StreamDecoder;
	const std::uint32_t ChannelCount = Decoder.getChannelCount();
	const std::size_t FramesPerBuffer = static_cast<std::size_t>(Decoder.getSampleRateInHertz() * c_StreamBufferDurationInSeconds);
	DecodeScratch.resize(FramesPerBuffer * ChannelCount);

	std::size_t FilledFrameCount{ 0 };
	bool HasJustRewound{ false };
	while (FilledFrameCount < FramesPerBuffer)
	{
		const std::uint64_t DecodedFrameCount = Decoder.readFrames(std::span(DecodeScratch).subspan(FilledFrameCount * ChannelCount));
		FilledFrameCount += static_cast<std::size_t>(DecodedFrameCount);
		if (FilledFrameCount == FramesPerBuffer)
		{
			break;
		}
		// The end of the file: a looping voice carries straight on from the start, inside the same buffer (no gap)
		const bool IsEmptyAfterRewind = HasJustRewound && DecodedFrameCount == 0;	// An empty file must not loop forever
		if (!StreamingVoice.m_ShouldLoop || IsEmptyAfterRewind || !Decoder.rewindToStart())
		{
			StreamingVoice.m_HasStreamReachedEnd = true;
			break;
		}
		HasJustRewound = true;
	}

	if (FilledFrameCount == 0)
	{
		return false;
	}
	alBufferData(BufferName, getSixteenBitFormat(ChannelCount), DecodeScratch.data(),
		static_cast<ALsizei>(FilledFrameCount * ChannelCount * sizeof(std::int16_t)), static_cast<ALsizei>(Decoder.getSampleRateInHertz()));
	return true;
}

}
