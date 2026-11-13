#include "Hephaestian/Audio/AudioDecoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <Vulkanised/Logger.h>

namespace Hephaestian::Audio
{

namespace
{
	// - MP3 (dr_mp3)
	class Mp3Decoder final : public AudioDecoder
	{
	public:
		bool open(const std::filesystem::path& AudioFilePath)
		{
#if defined(_WIN32)
			m_IsOpen = drmp3_init_file_w(&m_Mp3, AudioFilePath.wstring().c_str(), nullptr) == DRMP3_TRUE;	// Wide: any path Windows allows
#else
			m_IsOpen = drmp3_init_file(&m_Mp3, AudioFilePath.string().c_str(), nullptr) == DRMP3_TRUE;
#endif
			if (m_IsOpen)
			{
				m_ChannelCount = m_Mp3.channels;
				m_SampleRateInHertz = m_Mp3.sampleRate;
				std::error_code FileSizeError;
				m_FileSizeInBytes = std::filesystem::file_size(AudioFilePath, FileSizeError);
			}
			return m_IsOpen;
		}
		~Mp3Decoder() override
		{
			if (m_IsOpen)
			{
				drmp3_uninit(&m_Mp3);
			}
		}
		[[nodiscard]] double estimateDurationInSeconds() const override
		{
			// Counting MP3 frames means reading the whole file: assume a typical 128 kbps instead (only picks streaming or not)
			constexpr double c_AssumedBitsPerSecond{ 128000.0 };
			return static_cast<double>(m_FileSizeInBytes) * 8.0 / c_AssumedBitsPerSecond;
		}
		[[nodiscard]] std::uint64_t readFrames(std::span<std::int16_t> InterleavedSamplesToFill) override
		{
			return drmp3_read_pcm_frames_s16(&m_Mp3, InterleavedSamplesToFill.size() / m_ChannelCount, InterleavedSamplesToFill.data());
		}
		bool rewindToStart() override { return drmp3_seek_to_pcm_frame(&m_Mp3, 0) == DRMP3_TRUE; }

	private:
		drmp3 m_Mp3{};
		bool m_IsOpen{ false };
		std::uintmax_t m_FileSizeInBytes{ 0 };
	};

	// - WAV (dr_wav)
	class WavDecoder final : public AudioDecoder
	{
	public:
		bool open(const std::filesystem::path& AudioFilePath)
		{
#if defined(_WIN32)
			m_IsOpen = drwav_init_file_w(&m_Wav, AudioFilePath.wstring().c_str(), nullptr) == DRWAV_TRUE;
#else
			m_IsOpen = drwav_init_file(&m_Wav, AudioFilePath.string().c_str(), nullptr) == DRWAV_TRUE;
#endif
			if (m_IsOpen)
			{
				m_ChannelCount = m_Wav.channels;
				m_SampleRateInHertz = m_Wav.sampleRate;
			}
			return m_IsOpen;
		}
		~WavDecoder() override
		{
			if (m_IsOpen)
			{
				drwav_uninit(&m_Wav);
			}
		}
		[[nodiscard]] double estimateDurationInSeconds() const override
		{
			return m_SampleRateInHertz > 0 ? static_cast<double>(m_Wav.totalPCMFrameCount) / m_SampleRateInHertz : 0.0;
		}
		[[nodiscard]] std::uint64_t readFrames(std::span<std::int16_t> InterleavedSamplesToFill) override
		{
			return drwav_read_pcm_frames_s16(&m_Wav, InterleavedSamplesToFill.size() / m_ChannelCount, InterleavedSamplesToFill.data());
		}
		bool rewindToStart() override { return drwav_seek_to_pcm_frame(&m_Wav, 0) == DRWAV_TRUE; }

	private:
		drwav m_Wav{};
		bool m_IsOpen{ false };
	};

	// - FLAC (dr_flac)
	class FlacDecoder final : public AudioDecoder
	{
	public:
		bool open(const std::filesystem::path& AudioFilePath)
		{
#if defined(_WIN32)
			m_Flac = drflac_open_file_w(AudioFilePath.wstring().c_str(), nullptr);
#else
			m_Flac = drflac_open_file(AudioFilePath.string().c_str(), nullptr);
#endif
			if (m_Flac != nullptr)
			{
				m_ChannelCount = m_Flac->channels;
				m_SampleRateInHertz = m_Flac->sampleRate;
			}
			return m_Flac != nullptr;
		}
		~FlacDecoder() override
		{
			drflac_close(m_Flac);	// Fine with nullptr
		}
		[[nodiscard]] double estimateDurationInSeconds() const override
		{
			return m_SampleRateInHertz > 0 ? static_cast<double>(m_Flac->totalPCMFrameCount) / m_SampleRateInHertz : 0.0;
		}
		[[nodiscard]] std::uint64_t readFrames(std::span<std::int16_t> InterleavedSamplesToFill) override
		{
			return drflac_read_pcm_frames_s16(m_Flac, InterleavedSamplesToFill.size() / m_ChannelCount, InterleavedSamplesToFill.data());
		}
		bool rewindToStart() override { return drflac_seek_to_pcm_frame(m_Flac, 0) == DRFLAC_TRUE; }

	private:
		drflac* m_Flac{ nullptr };
	};

	// - Ogg Vorbis (stb_vorbis)
	class VorbisDecoder final : public AudioDecoder
	{
	public:
		bool open(const std::filesystem::path& AudioFilePath)
		{
			// stb_vorbis only takes narrow file names: opening the FILE ourselves keeps any Windows path working
#if defined(_WIN32)
			FILE* OpenedFile{ nullptr };
			if (_wfopen_s(&OpenedFile, AudioFilePath.wstring().c_str(), L"rb") != 0)
			{
				OpenedFile = nullptr;
			}
#else
			FILE* OpenedFile = std::fopen(AudioFilePath.string().c_str(), "rb");
#endif
			if (OpenedFile == nullptr)
			{
				return false;
			}
			int VorbisError{ 0 };
			m_Vorbis = stb_vorbis_open_file(OpenedFile, 1, &VorbisError, nullptr);	// 1: closes the file when closed itself
			if (m_Vorbis == nullptr)
			{
				std::fclose(OpenedFile);
				return false;
			}
			const stb_vorbis_info VorbisInfo = stb_vorbis_get_info(m_Vorbis);
			m_ChannelCount = static_cast<std::uint32_t>(VorbisInfo.channels);
			m_SampleRateInHertz = VorbisInfo.sample_rate;
			return true;
		}
		~VorbisDecoder() override
		{
			if (m_Vorbis != nullptr)
			{
				stb_vorbis_close(m_Vorbis);
			}
		}
		[[nodiscard]] double estimateDurationInSeconds() const override
		{
			return m_SampleRateInHertz > 0 ? static_cast<double>(stb_vorbis_stream_length_in_samples(m_Vorbis)) / m_SampleRateInHertz : 0.0;
		}
		[[nodiscard]] std::uint64_t readFrames(std::span<std::int16_t> InterleavedSamplesToFill) override
		{
			// stb_vorbis hands out at most one Vorbis packet per call: loop until full or at the end
			std::uint64_t WrittenFrameCount{ 0 };
			const std::uint64_t RequestedFrameCount = InterleavedSamplesToFill.size() / m_ChannelCount;
			while (WrittenFrameCount < RequestedFrameCount)
			{
				const std::span<std::int16_t> RemainingSamples = InterleavedSamplesToFill.subspan(WrittenFrameCount * m_ChannelCount);
				const int DecodedFrameCount = stb_vorbis_get_samples_short_interleaved(m_Vorbis, static_cast<int>(m_ChannelCount), RemainingSamples.data(), static_cast<int>(RemainingSamples.size()));
				if (DecodedFrameCount <= 0)
				{
					break;
				}
				WrittenFrameCount += static_cast<std::uint64_t>(DecodedFrameCount);
			}
			return WrittenFrameCount;
		}
		bool rewindToStart() override { return stb_vorbis_seek_start(m_Vorbis) != 0; }

	private:
		stb_vorbis* m_Vorbis{ nullptr };
	};

	template <typename DecoderType>
	[[nodiscard]] std::unique_ptr<AudioDecoder> openWith(const std::filesystem::path& AudioFilePath)
	{
		auto NewDecoder = std::make_unique<DecoderType>();
		if (!NewDecoder->open(AudioFilePath))
		{
			return nullptr;
		}
		return NewDecoder;
	}
}

std::unique_ptr<AudioDecoder> AudioDecoder::openFile(const std::filesystem::path& AudioFilePath)
{
	std::string Extension = AudioFilePath.extension().string();
	std::ranges::transform(Extension, Extension.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

	std::unique_ptr<AudioDecoder> OpenedDecoder;
	if (Extension == ".mp3") { OpenedDecoder = openWith<Mp3Decoder>(AudioFilePath); }
	else if (Extension == ".wav") { OpenedDecoder = openWith<WavDecoder>(AudioFilePath); }
	else if (Extension == ".flac") { OpenedDecoder = openWith<FlacDecoder>(AudioFilePath); }
	else if (Extension == ".ogg") { OpenedDecoder = openWith<VorbisDecoder>(AudioFilePath); }
	else
	{
		LOGE("Audio file {}: unsupported format '{}' (.mp3, .wav, .flac or .ogg).", AudioFilePath.string(), Extension);
		return nullptr;
	}

	if (OpenedDecoder == nullptr)
	{
		LOGE("Audio file {} couldn't be opened (missing, or not a valid {} file).", AudioFilePath.string(), Extension);
		return nullptr;
	}
	if (OpenedDecoder->getChannelCount() < 1 || OpenedDecoder->getChannelCount() > 2 || OpenedDecoder->getSampleRateInHertz() == 0)
	{
		LOGE("Audio file {} has {} channels: only mono and stereo are supported.", AudioFilePath.string(), OpenedDecoder->getChannelCount());
		return nullptr;
	}
	return OpenedDecoder;
}

}
