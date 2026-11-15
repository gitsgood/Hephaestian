/**
 * @file AudioDecoder.h
 * @brief Audio files to 16-bit PCM samples, one interface over the vendored decoders.
 */
#ifndef HEPHAESTIAN_AUDIODECODER_H
#define HEPHAESTIAN_AUDIODECODER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace Hephaestian::Audio
{

/**
 * @brief Turns an audio file into 16-bit PCM samples, a chunk at a time (OpenAL only plays raw samples).
 *
 * One interface over the vendored single-file decoders: dr_mp3 (.mp3), dr_wav (.wav), dr_flac (.flac),
 * stb_vorbis (.ogg). Mono or stereo only, which is what OpenAL plays.
 *
 * Each decoder has its own read position, so two voices streaming the same file each open their own.
 * Not thread-safe: one decoder is used by one thread at a time (the streaming thread, or whoever loads a short sound).
 */
class AudioDecoder
{
public:
	virtual ~AudioDecoder() = default;

	/**
	 * @brief Opens an audio file with the decoder its extension calls for.
	 * @param AudioFilePath An .mp3, .wav, .flac or .ogg file (the extension's case doesn't matter).
	 * @return The decoder, positioned at the first sample. nullptr (and logged) if the format isn't supported, the file
	 * can't be opened, or it isn't mono or stereo.
	 */
	[[nodiscard]] static std::unique_ptr<AudioDecoder> openFile(const std::filesystem::path& AudioFilePath);

	/**
	 * @brief How many channels each frame has.
	 * @return 1 (mono) or 2 (stereo).
	 */
	[[nodiscard]] std::uint32_t getChannelCount() const noexcept { return m_ChannelCount; }
	/**
	 * @brief The file's sample rate.
	 * @return Frames per second, e.g. 44100 or 48000.
	 */
	[[nodiscard]] std::uint32_t getSampleRateInHertz() const noexcept { return m_SampleRateInHertz; }

	/**
	 * @brief How long the file plays.
	 *
	 * Exact where the format says so cheaply (WAV, FLAC, Ogg), estimated otherwise (MP3: from the file size, assuming
	 * 128 kbps, with no decoding). Only used to pick streaming or not, so close is enough.
	 * @return The duration in seconds.
	 */
	[[nodiscard]] virtual double estimateDurationInSeconds() const = 0;

	/**
	 * @brief Decodes the next samples.
	 * @param InterleavedSamplesToFill Where to write them, interleaved (getChannelCount() samples per frame).
	 * @return How many FRAMES were written. Fewer than asked means the end of the file was reached.
	 */
	[[nodiscard]] virtual std::uint64_t readFrames(std::span<std::int16_t> InterleavedSamplesToFill) = 0;

	/**
	 * @brief Goes back to the first sample (looping).
	 * @return False if the decoder couldn't seek.
	 */
	virtual bool rewindToStart() = 0;

protected:
	std::uint32_t m_ChannelCount{ 0 };		///< Set by each decoder once opened
	std::uint32_t m_SampleRateInHertz{ 0 };	///< Set by each decoder once opened
};

}

#endif // !HEPHAESTIAN_AUDIODECODER_H
