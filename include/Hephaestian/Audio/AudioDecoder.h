#ifndef HEPHAESTIAN_AUDIODECODER_H
#define HEPHAESTIAN_AUDIODECODER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

/*
* Turns an audio file into 16-bit PCM samples, a chunk at a time (OpenAL only plays raw samples). One interface over the
* vendored single-file decoders: dr_mp3 (.mp3), dr_wav (.wav), dr_flac (.flac), stb_vorbis (.ogg).
* Mono or stereo only, which is what OpenAL plays.
*
* Each decoder has its own read position, so two voices streaming the same file each open their own.
* Not thread-safe: one decoder is used by one thread at a time (the streaming thread, or whoever loads a short sound).
*/
namespace Hephaestian::Audio
{

class AudioDecoder
{
public:
	virtual ~AudioDecoder() = default;

	// Picks the decoder from the file's extension. nullptr (and logged) if it can't be opened or isn't mono/stereo.
	[[nodiscard]] static std::unique_ptr<AudioDecoder> openFile(const std::filesystem::path& AudioFilePath);

	[[nodiscard]] std::uint32_t getChannelCount() const noexcept { return m_ChannelCount; }
	[[nodiscard]] std::uint32_t getSampleRateInHertz() const noexcept { return m_SampleRateInHertz; }

	// How long the file plays: exact where the format says so cheaply (WAV, FLAC, Ogg), estimated otherwise (MP3: from
	// the file size and the first frame's bitrate, no decoding). Only used to pick streaming or not, so close is enough.
	[[nodiscard]] virtual double estimateDurationInSeconds() const = 0;

	// Fills the span with interleaved samples (channel count per frame); returns how many FRAMES were written.
	// Fewer than asked = the end of the file was reached.
	[[nodiscard]] virtual std::uint64_t readFrames(std::span<std::int16_t> InterleavedSamplesToFill) = 0;

	// Back to the first sample (looping). False if the decoder can't.
	virtual bool rewindToStart() = 0;

protected:
	std::uint32_t m_ChannelCount{ 0 };
	std::uint32_t m_SampleRateInHertz{ 0 };
};

}

#endif // !HEPHAESTIAN_AUDIODECODER_H
