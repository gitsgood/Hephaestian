#include "Hephaestian/Assets/SpriteSheet.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <map>
#include <numeric>
#include <utility>

#include <stb_image.h>

#include "glaze/glaze.hpp"

#include <Vulkanised/Logger.h>

// THE TWO FILE FORMATS, as plain structs glaze reads straight into
namespace Hephaestian::Assets::SpriteSheetFileFormat
{
	// - Our sidecar (.spritesheet.json)
	struct GridBlock
	{
		std::uint32_t m_FrameWidthInPixels{ 0 };	// 0 = worked out from the columns
		std::uint32_t m_FrameHeightInPixels{ 0 };	// 0 = worked out from the rows
		std::uint32_t m_ColumnCount{ 0 };			// 0 = as many as fit
		std::uint32_t m_RowCount{ 0 };
		std::uint32_t m_FrameCount{ 0 };			// 0 = every cell (a last row that isn't full needs this)
		std::uint32_t m_MarginInPixels{ 0 };		// Empty border around the whole grid
		std::uint32_t m_SpacingInPixels{ 0 };		// Empty gap between cells
	};

	struct ClipBlock
	{
		std::string m_ClipName;
		std::vector<std::uint32_t> m_FrameIndices;
		std::optional<std::array<std::uint32_t, 2>> m_FrameRange;	// [first, last], both included
		float m_FramesPerSecond{ 10.0f };
		std::string m_PlaybackName{ "loop" };
	};

	struct SidecarFile
	{
		std::optional<std::string> m_ImageRelativePath;
		std::vector<std::string> m_PageImageRelativePaths;			// Several pages, in order
		std::optional<std::string> m_PageImageFolderRelativePath;	// Every image in a folder
		std::optional<std::string> m_AsepriteRelativePath;
		std::optional<GridBlock> m_Grid;
		std::optional<std::array<float, 3>> m_TransparentColour;
		float m_TransparentColourTolerance{ 0.1f };
		std::string m_FilteringName{ "pixelated" };
		std::string m_BlendingName{ "cutout" };
		bool m_ShouldGenerateMipmaps{ false };
		float m_PixelsPerUnit{ 32.0f };
		std::vector<ClipBlock> m_Clips;
	};

	// - Aseprite's JSON export (only the parts we use)
	struct AsepriteRectangle { int m_X{ 0 }; int m_Y{ 0 }; int m_Width{ 0 }; int m_Height{ 0 }; };
	struct AsepriteSize { int m_Width{ 0 }; int m_Height{ 0 }; };
	struct AsepriteFrame { std::string m_FileName; AsepriteRectangle m_FrameRectangle; int m_DurationInMilliseconds{ 100 }; };
	struct AsepriteFrameTag { std::string m_TagName; int m_FirstFrame{ 0 }; int m_LastFrame{ 0 }; std::string m_Direction{ "forward" }; };
	struct AsepriteMeta { std::string m_ImageRelativePath; AsepriteSize m_ImageSize; std::vector<AsepriteFrameTag> m_FrameTags; };
	struct AsepriteFileWithFrameArray { std::vector<AsepriteFrame> m_Frames; AsepriteMeta m_Meta; };				// "Array" export
	struct AsepriteFileWithFrameMap { std::map<std::string, AsepriteFrame> m_FramesByName; AsepriteMeta m_Meta; };	// "Hash" export
}

namespace SpriteSheetFormat = Hephaestian::Assets::SpriteSheetFileFormat;

template <> struct glz::meta<SpriteSheetFormat::GridBlock>
{
	using T = SpriteSheetFormat::GridBlock;
	static constexpr auto value = glz::object("frameWidth", &T::m_FrameWidthInPixels, "frameHeight", &T::m_FrameHeightInPixels, "columns", &T::m_ColumnCount,
		"rows", &T::m_RowCount, "frameCount", &T::m_FrameCount, "margin", &T::m_MarginInPixels, "spacing", &T::m_SpacingInPixels);
};
template <> struct glz::meta<SpriteSheetFormat::ClipBlock>
{
	using T = SpriteSheetFormat::ClipBlock;
	static constexpr auto value = glz::object("name", &T::m_ClipName, "frames", &T::m_FrameIndices, "range", &T::m_FrameRange,
		"framesPerSecond", &T::m_FramesPerSecond, "playback", &T::m_PlaybackName);
};
template <> struct glz::meta<SpriteSheetFormat::SidecarFile>
{
	using T = SpriteSheetFormat::SidecarFile;
	static constexpr auto value = glz::object("image", &T::m_ImageRelativePath, "images", &T::m_PageImageRelativePaths, "imageFolder", &T::m_PageImageFolderRelativePath,
		"aseprite", &T::m_AsepriteRelativePath, "grid", &T::m_Grid, "transparentColour", &T::m_TransparentColour, "transparentColourTolerance", &T::m_TransparentColourTolerance,
		"filtering", &T::m_FilteringName, "blending", &T::m_BlendingName, "mipmaps", &T::m_ShouldGenerateMipmaps, "pixelsPerUnit", &T::m_PixelsPerUnit, "clips", &T::m_Clips);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteRectangle>
{
	using T = SpriteSheetFormat::AsepriteRectangle;
	static constexpr auto value = glz::object("x", &T::m_X, "y", &T::m_Y, "w", &T::m_Width, "h", &T::m_Height);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteSize>
{
	using T = SpriteSheetFormat::AsepriteSize;
	static constexpr auto value = glz::object("w", &T::m_Width, "h", &T::m_Height);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteFrame>
{
	using T = SpriteSheetFormat::AsepriteFrame;
	static constexpr auto value = glz::object("filename", &T::m_FileName, "frame", &T::m_FrameRectangle, "duration", &T::m_DurationInMilliseconds);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteFrameTag>
{
	using T = SpriteSheetFormat::AsepriteFrameTag;
	static constexpr auto value = glz::object("name", &T::m_TagName, "from", &T::m_FirstFrame, "to", &T::m_LastFrame, "direction", &T::m_Direction);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteMeta>
{
	using T = SpriteSheetFormat::AsepriteMeta;
	static constexpr auto value = glz::object("image", &T::m_ImageRelativePath, "size", &T::m_ImageSize, "frameTags", &T::m_FrameTags);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteFileWithFrameArray>
{
	using T = SpriteSheetFormat::AsepriteFileWithFrameArray;
	static constexpr auto value = glz::object("frames", &T::m_Frames, "meta", &T::m_Meta);
};
template <> struct glz::meta<SpriteSheetFormat::AsepriteFileWithFrameMap>
{
	using T = SpriteSheetFormat::AsepriteFileWithFrameMap;
	static constexpr auto value = glz::object("frames", &T::m_FramesByName, "meta", &T::m_Meta);
};

namespace Hephaestian::Assets
{

namespace
{
	constexpr glz::opts c_LenientJsonOptions{ .error_on_unknown_keys = false };

	enum class ClipPlayback { Loop, PingPong, Once };

	// Frames + one duration per frame -> the playback sequence (ping-pong unrolled), with cumulative step end times
	[[nodiscard]] SpriteAnimationClip buildAnimationClip(std::string ClipName, std::vector<std::uint32_t> FrameIndices, std::vector<float> FrameDurationsInSeconds, ClipPlayback Playback)
	{
		SpriteAnimationClip NewClip{};
		NewClip.m_ClipName = std::move(ClipName);
		NewClip.m_ShouldLoop = Playback != ClipPlayback::Once;

		// Ping-pong: 0 1 2 3 then back 2 1 (the ends aren't repeated, or they'd show for twice as long)
		if (Playback == ClipPlayback::PingPong && FrameIndices.size() > 2)
		{
			for (std::size_t CurrentStep = FrameIndices.size() - 2; CurrentStep > 0; --CurrentStep)
			{
				FrameIndices.push_back(FrameIndices[CurrentStep]);
				FrameDurationsInSeconds.push_back(FrameDurationsInSeconds[CurrentStep]);
			}
		}

		float StepEndTimeInSeconds{ 0.0f };
		for (std::size_t CurrentStep{ 0 }; CurrentStep < FrameIndices.size(); ++CurrentStep)
		{
			StepEndTimeInSeconds += std::max(FrameDurationsInSeconds[CurrentStep], 0.001f);
			NewClip.m_PlaybackFrameIndices.push_back(FrameIndices[CurrentStep]);
			NewClip.m_PlaybackStepEndTimesInSeconds.push_back(StepEndTimeInSeconds);
		}
		NewClip.m_TotalDurationInSeconds = StepEndTimeInSeconds;
		return NewClip;
	}

	[[nodiscard]] std::optional<ClipPlayback> parseClipPlaybackName(std::string_view PlaybackName)
	{
		if (PlaybackName == "loop") { return ClipPlayback::Loop; }
		if (PlaybackName == "pingPong" || PlaybackName == "pingpong") { return ClipPlayback::PingPong; }
		if (PlaybackName == "once") { return ClipPlayback::Once; }
		return std::nullopt;
	}

	// Aseprite's "Hash" export names frames "Slime 0.aseprite", "Slime 1.aseprite"... and a std::map sorts those as text
	// (so "10" before "2"): the number in the name gives the real order back
	[[nodiscard]] long long getTrailingFrameNumber(const std::string& FrameName)
	{
		const std::size_t ExtensionStart = FrameName.find_last_of('.');
		const std::string_view NameWithoutExtension = std::string_view(FrameName).substr(0, ExtensionStart == std::string::npos ? FrameName.size() : ExtensionStart);
		std::size_t DigitsStart = NameWithoutExtension.size();
		while (DigitsStart > 0 && std::isdigit(static_cast<unsigned char>(NameWithoutExtension[DigitsStart - 1])))
		{
			--DigitsStart;
		}
		if (DigitsStart == NameWithoutExtension.size())
		{
			return -1;
		}
		return std::stoll(std::string(NameWithoutExtension.substr(DigitsStart)));
	}

	// Tries both Aseprite layouts. Frames come out in Aseprite's frame order.
	[[nodiscard]] std::optional<std::pair<std::vector<SpriteSheetFormat::AsepriteFrame>, SpriteSheetFormat::AsepriteMeta>> readAsepriteFile(const std::string& FileContents)
	{
		SpriteSheetFormat::AsepriteFileWithFrameArray FileWithArray{};
		if (!glz::read<c_LenientJsonOptions>(FileWithArray, FileContents) && !FileWithArray.m_Frames.empty())
		{
			return std::make_pair(std::move(FileWithArray.m_Frames), std::move(FileWithArray.m_Meta));
		}

		SpriteSheetFormat::AsepriteFileWithFrameMap FileWithMap{};
		if (!glz::read<c_LenientJsonOptions>(FileWithMap, FileContents) && !FileWithMap.m_FramesByName.empty())
		{
			std::vector<std::pair<std::string, SpriteSheetFormat::AsepriteFrame>> NamedFrames(FileWithMap.m_FramesByName.begin(), FileWithMap.m_FramesByName.end());
			std::ranges::stable_sort(NamedFrames, [](const auto& FirstFrame, const auto& SecondFrame) { return getTrailingFrameNumber(FirstFrame.first) < getTrailingFrameNumber(SecondFrame.first); });
			std::vector<SpriteSheetFormat::AsepriteFrame> OrderedFrames;
			for (auto& [FrameName, Frame] : NamedFrames)
			{
				OrderedFrames.push_back(std::move(Frame));
			}
			return std::make_pair(std::move(OrderedFrames), std::move(FileWithMap.m_Meta));
		}
		return std::nullopt;
	}

	// Pixel rectangle -> texture coordinates. Pulled in a little from the edges, so filtering never picks up the
	// neighbouring frame: a hair for pixelated sheets, half a pixel for smooth ones.
	[[nodiscard]] glm::vec4 makeTextureCoordinateRect(float X, float Y, float Width, float Height, glm::vec2 ImageSize, bool IsSmooth)
	{
		const float EdgeInsetInPixels = IsSmooth ? 0.5f : 0.01f;
		return { (X + EdgeInsetInPixels) / ImageSize.x, (Y + EdgeInsetInPixels) / ImageSize.y,
			(Width - 2.0f * EdgeInsetInPixels) / ImageSize.x, (Height - 2.0f * EdgeInsetInPixels) / ImageSize.y };
	}

	// "imageFolder": every image in it, ordered by the number ending each name (so "frame_9" comes before "frame_10"), then by name
	[[nodiscard]] std::vector<std::filesystem::path> listPageImagesInFolder(const std::filesystem::path& FolderPath)
	{
		constexpr std::array<std::string_view, 6> c_ImageFileExtensions{ ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".psd" };
		std::vector<std::filesystem::path> PageImageFilePaths;
		std::error_code FolderListingError;
		for (const std::filesystem::directory_entry& CurrentEntry : std::filesystem::directory_iterator(FolderPath, FolderListingError))
		{
			std::string Extension = CurrentEntry.path().extension().string();
			std::ranges::transform(Extension, Extension.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
			if (CurrentEntry.is_regular_file() && std::ranges::find(c_ImageFileExtensions, Extension) != c_ImageFileExtensions.end())
			{
				PageImageFilePaths.push_back(CurrentEntry.path());
			}
		}
		std::ranges::sort(PageImageFilePaths, [](const std::filesystem::path& FirstPath, const std::filesystem::path& SecondPath)
		{
			const std::string FirstName = FirstPath.filename().string();
			const std::string SecondName = SecondPath.filename().string();
			return std::make_pair(getTrailingFrameNumber(FirstName), FirstName) < std::make_pair(getTrailingFrameNumber(SecondName), SecondName);
		});
		return PageImageFilePaths;
	}

	// A folder given as the sprite sheet: the one sidecar inside it
	[[nodiscard]] std::optional<std::filesystem::path> findSidecarInFolder(const std::filesystem::path& FolderPath)
	{
		std::vector<std::filesystem::path> SidecarFilePaths;
		std::error_code FolderListingError;
		for (const std::filesystem::directory_entry& CurrentEntry : std::filesystem::directory_iterator(FolderPath, FolderListingError))
		{
			if (CurrentEntry.is_regular_file() && CurrentEntry.path().filename().string().ends_with(".spritesheet.json"))
			{
				SidecarFilePaths.push_back(CurrentEntry.path());
			}
		}
		if (SidecarFilePaths.size() != 1)
		{
			LOGE("Sprite sheet folder {} should hold exactly one .spritesheet.json describing its images, it has {}.", FolderPath.string(), SidecarFilePaths.size());
			return std::nullopt;
		}
		return SidecarFilePaths.front();
	}

	// An empty cell: nothing in it would show. Every pixel see-through, or black (images without transparency leave their
	// unused cells black; a little noise from compression is tolerated).
	[[nodiscard]] bool isPixelRectangleEmpty(const unsigned char* RgbaPixels, int ImageWidth, const glm::uvec4& PixelRectangle)
	{
		constexpr unsigned char c_HighestBlackChannelValue{ 16 };
		for (std::uint32_t CurrentRow = PixelRectangle.y; CurrentRow < PixelRectangle.y + PixelRectangle.w; ++CurrentRow)
		{
			const unsigned char* CurrentPixel = RgbaPixels + (static_cast<std::size_t>(CurrentRow) * static_cast<std::size_t>(ImageWidth) + PixelRectangle.x) * 4;
			for (std::uint32_t CurrentColumn{ 0 }; CurrentColumn < PixelRectangle.z; ++CurrentColumn, CurrentPixel += 4)
			{
				const bool IsSeeThrough = CurrentPixel[3] < 128;
				const bool IsBlack = std::max({ CurrentPixel[0], CurrentPixel[1], CurrentPixel[2] }) <= c_HighestBlackChannelValue;
				if (!IsSeeThrough && !IsBlack)
				{
					return false;
				}
			}
		}
		return true;
	}
}

unsigned char* decodeSpriteSheetPage(const SpriteSheetDescription& Description, const std::filesystem::path& PageImageFilePath, int& WidthInPixels, int& HeightInPixels)
{
	int OriginalChannelCount{ 0 };
	unsigned char* RgbaPixels = stbi_load(PageImageFilePath.string().c_str(), &WidthInPixels, &HeightInPixels, &OriginalChannelCount, 4);
	if (RgbaPixels == nullptr || !Description.m_TransparentColour.has_value())
	{
		return RgbaPixels;
	}

	// Colour key: every channel within the tolerance of the transparent colour -> fully see-through
	const glm::ivec3 TransparentColour = glm::ivec3(glm::round(glm::clamp(Description.m_TransparentColour.value(), 0.0f, 1.0f) * 255.0f));
	const int ToleranceInChannelSteps = static_cast<int>(std::round(std::clamp(Description.m_TransparentColourTolerance, 0.0f, 1.0f) * 255.0f));
	const std::size_t PixelCount = static_cast<std::size_t>(WidthInPixels) * static_cast<std::size_t>(HeightInPixels);
	for (unsigned char* CurrentPixel = RgbaPixels; CurrentPixel != RgbaPixels + PixelCount * 4; CurrentPixel += 4)
	{
		if (std::abs(CurrentPixel[0] - TransparentColour.r) <= ToleranceInChannelSteps
			&& std::abs(CurrentPixel[1] - TransparentColour.g) <= ToleranceInChannelSteps
			&& std::abs(CurrentPixel[2] - TransparentColour.b) <= ToleranceInChannelSteps)
		{
			CurrentPixel[3] = 0;
		}
	}
	return RgbaPixels;
}

std::uint32_t SpriteAnimationClip::getFrameIndexAtTime(float ElapsedTimeInSeconds) const noexcept
{
	if (m_PlaybackFrameIndices.empty())
	{
		return 0;
	}
	float TimeIntoClip = std::max(ElapsedTimeInSeconds, 0.0f);
	if (m_ShouldLoop)
	{
		TimeIntoClip = std::fmod(TimeIntoClip, m_TotalDurationInSeconds);
	}
	// The first step that ends after this time is the one showing (past the end: the last one)
	const auto ShowingStep = std::ranges::upper_bound(m_PlaybackStepEndTimesInSeconds, TimeIntoClip);
	const std::size_t ShowingStepIndex = std::min<std::size_t>(static_cast<std::size_t>(ShowingStep - m_PlaybackStepEndTimesInSeconds.begin()), m_PlaybackFrameIndices.size() - 1);
	return m_PlaybackFrameIndices[ShowingStepIndex];
}

std::optional<std::uint32_t> SpriteSheet::findClipIndex(std::string_view ClipName) const noexcept
{
	for (std::size_t CurrentClipIndex{ 0 }; CurrentClipIndex < m_Clips.size(); ++CurrentClipIndex)
	{
		if (m_Clips[CurrentClipIndex].m_ClipName == ClipName)
		{
			return static_cast<std::uint32_t>(CurrentClipIndex);
		}
	}
	return std::nullopt;
}

std::optional<SpriteSheetDescription> readSpriteSheetFile(const std::filesystem::path& GivenSpriteSheetPath)
{
	std::filesystem::path SpriteSheetFilePath = GivenSpriteSheetPath;
	std::error_code DirectoryCheckError;
	if (std::filesystem::is_directory(GivenSpriteSheetPath, DirectoryCheckError))
	{
		const std::optional<std::filesystem::path> SidecarInFolder = findSidecarInFolder(GivenSpriteSheetPath);
		if (!SidecarInFolder.has_value())
		{
			return std::nullopt;
		}
		SpriteSheetFilePath = SidecarInFolder.value();
	}

	const std::string FileNameForLogging = SpriteSheetFilePath.filename().string();
	std::string FileContents;
	if (glz::file_to_buffer(FileContents, SpriteSheetFilePath.string()) != glz::error_code::none)
	{
		LOGE("Sprite sheet {} couldn't be read.", SpriteSheetFilePath.string());
		return std::nullopt;
	}

	SpriteSheetDescription Description{};
	Description.m_SpriteSheet.m_SourceFilePath = SpriteSheetFilePath;
	Description.m_TextureOptions.m_ShouldGenerateMipmaps = false;
	Description.m_TextureOptions.m_Filtering = Vulkanised::Rendering::TextureFiltering::Pixelated;
	SpriteSheet& Sheet = Description.m_SpriteSheet;

	// 1. WHICH FORMAT: our sidecar names an image or an Aseprite file; otherwise it should be an Aseprite export itself
	// (Unknown keys are skipped, so a valid Aseprite export reads fine as a sidecar with nothing set: a failure here means
	// the file isn't valid JSON, or a value has the wrong type)
	SpriteSheetFormat::SidecarFile Sidecar{};
	const glz::error_ctx SidecarReadError = glz::read<c_LenientJsonOptions>(Sidecar, FileContents);
	if (SidecarReadError)
	{
		LOGE("Sprite sheet {} isn't valid:\n{}", FileNameForLogging, glz::format_error(SidecarReadError, FileContents));
		return std::nullopt;
	}
	const bool IsSidecar = Sidecar.m_ImageRelativePath.has_value() || !Sidecar.m_PageImageRelativePaths.empty()
		|| Sidecar.m_PageImageFolderRelativePath.has_value() || Sidecar.m_AsepriteRelativePath.has_value();
	std::string AsepriteContents;
	std::filesystem::path AsepriteFilePath;
	if (IsSidecar)
	{
		Sheet.m_PixelsPerUnit = Sidecar.m_PixelsPerUnit > 0.0f ? Sidecar.m_PixelsPerUnit : 32.0f;
		if (Sidecar.m_TransparentColour.has_value())
		{
			const auto [Red, Green, Blue] = Sidecar.m_TransparentColour.value();
			Description.m_TransparentColour = glm::vec3(Red, Green, Blue);
			Description.m_TransparentColourTolerance = Sidecar.m_TransparentColourTolerance;
		}
		Description.m_TextureOptions.m_ShouldGenerateMipmaps = Sidecar.m_ShouldGenerateMipmaps;
		if (Sidecar.m_FilteringName == "smooth")
		{
			Description.m_TextureOptions.m_Filtering = Vulkanised::Rendering::TextureFiltering::Smooth;
		}
		else if (Sidecar.m_FilteringName != "pixelated")
		{
			LOGW("Sprite sheet {}: unknown filtering '{}' (pixelated or smooth), using pixelated.", FileNameForLogging, Sidecar.m_FilteringName);
		}
		if (Sidecar.m_BlendingName == "additive")
		{
			Sheet.m_BlendMode = SpriteBlendMode::Additive;
		}
		else if (Sidecar.m_BlendingName != "cutout")
		{
			LOGW("Sprite sheet {}: unknown blending '{}' (cutout or additive), using cutout.", FileNameForLogging, Sidecar.m_BlendingName);
		}
		if (Sidecar.m_AsepriteRelativePath.has_value())
		{
			AsepriteFilePath = SpriteSheetFilePath.parent_path() / Sidecar.m_AsepriteRelativePath.value();
			if (glz::file_to_buffer(AsepriteContents, AsepriteFilePath.string()) != glz::error_code::none)
			{
				LOGE("Sprite sheet {}: its Aseprite file {} couldn't be read.", FileNameForLogging, AsepriteFilePath.string());
				return std::nullopt;
			}
		}
	}
	else
	{
		AsepriteFilePath = SpriteSheetFilePath;
		AsepriteContents = std::move(FileContents);
	}
	const bool IsSmooth = Description.m_TextureOptions.m_Filtering == Vulkanised::Rendering::TextureFiltering::Smooth;

	// 2. FRAMES (and Aseprite's tags as clips)
	if (!AsepriteContents.empty())
	{
		auto AsepriteData = readAsepriteFile(AsepriteContents);
		if (!AsepriteData.has_value())
		{
			LOGE("Sprite sheet {}: neither an image + grid nor an Aseprite JSON export with frames.", AsepriteFilePath.filename().string());
			return std::nullopt;
		}
		const auto& [AsepriteFrames, AsepriteMetaData] = AsepriteData.value();
		Description.m_PageImageFilePaths = { AsepriteFilePath.parent_path() / AsepriteMetaData.m_ImageRelativePath };
		const glm::vec2 ImageSize{ static_cast<float>(AsepriteMetaData.m_ImageSize.m_Width), static_cast<float>(AsepriteMetaData.m_ImageSize.m_Height) };
		if (ImageSize.x <= 0.0f || ImageSize.y <= 0.0f)
		{
			LOGE("Sprite sheet {}: the Aseprite export has no image size (meta.size).", AsepriteFilePath.filename().string());
			return std::nullopt;
		}
		for (const SpriteSheetFormat::AsepriteFrame& CurrentFrame : AsepriteFrames)
		{
			const SpriteSheetFormat::AsepriteRectangle& Rectangle = CurrentFrame.m_FrameRectangle;
			Sheet.m_Frames.push_back({ makeTextureCoordinateRect(static_cast<float>(Rectangle.m_X), static_cast<float>(Rectangle.m_Y), static_cast<float>(Rectangle.m_Width), static_cast<float>(Rectangle.m_Height), ImageSize, IsSmooth),
				glm::vec2(static_cast<float>(Rectangle.m_Width), static_cast<float>(Rectangle.m_Height)) });
		}
		for (const SpriteSheetFormat::AsepriteFrameTag& CurrentTag : AsepriteMetaData.m_FrameTags)
		{
			const int FirstFrame = std::clamp(std::min(CurrentTag.m_FirstFrame, CurrentTag.m_LastFrame), 0, static_cast<int>(AsepriteFrames.size()) - 1);
			const int LastFrame = std::clamp(std::max(CurrentTag.m_FirstFrame, CurrentTag.m_LastFrame), 0, static_cast<int>(AsepriteFrames.size()) - 1);
			std::vector<std::uint32_t> TagFrames;
			std::vector<float> TagDurations;
			for (int CurrentFrame = FirstFrame; CurrentFrame <= LastFrame; ++CurrentFrame)
			{
				TagFrames.push_back(static_cast<std::uint32_t>(CurrentFrame));
				TagDurations.push_back(static_cast<float>(AsepriteFrames[static_cast<std::size_t>(CurrentFrame)].m_DurationInMilliseconds) / 1000.0f);
			}
			const bool IsReversed = CurrentTag.m_Direction == "reverse" || CurrentTag.m_Direction == "pingpong_reverse";
			if (IsReversed)
			{
				std::ranges::reverse(TagFrames);
				std::ranges::reverse(TagDurations);
			}
			const bool IsPingPong = CurrentTag.m_Direction == "pingpong" || CurrentTag.m_Direction == "pingpong_reverse";
			Sheet.m_Clips.push_back(buildAnimationClip(CurrentTag.m_TagName, std::move(TagFrames), std::move(TagDurations), IsPingPong ? ClipPlayback::PingPong : ClipPlayback::Loop));
		}
	}
	else
	{
		// A grid over each page, in page order. Sizes come from the image files' headers (nothing decoded yet).
		const std::filesystem::path SidecarFolder = SpriteSheetFilePath.parent_path();
		if (Sidecar.m_ImageRelativePath.has_value())
		{
			Description.m_PageImageFilePaths.push_back(SidecarFolder / Sidecar.m_ImageRelativePath.value());
		}
		for (const std::string& CurrentPageRelativePath : Sidecar.m_PageImageRelativePaths)
		{
			Description.m_PageImageFilePaths.push_back(SidecarFolder / CurrentPageRelativePath);
		}
		if (Sidecar.m_PageImageFolderRelativePath.has_value())
		{
			const std::filesystem::path PageImageFolder = SidecarFolder / Sidecar.m_PageImageFolderRelativePath.value();
			std::ranges::move(listPageImagesInFolder(PageImageFolder), std::back_inserter(Description.m_PageImageFilePaths));
			if (Description.m_PageImageFilePaths.empty())
			{
				LOGE("Sprite sheet {}: its image folder {} has no images in it.", FileNameForLogging, PageImageFolder.string());
				return std::nullopt;
			}
		}

		const SpriteSheetFormat::GridBlock Grid = Sidecar.m_Grid.value_or(SpriteSheetFormat::GridBlock{});
		std::vector<glm::uvec4> FramePixelRectangles;	// (x, y, width, height), for finding empty cells
		std::uint32_t LastPageFirstFrameIndex{ 0 };
		for (std::uint32_t CurrentPageIndex{ 0 }; CurrentPageIndex < Description.m_PageImageFilePaths.size(); ++CurrentPageIndex)
		{
			const std::filesystem::path& PageImageFilePath = Description.m_PageImageFilePaths[CurrentPageIndex];
			int ImageWidth{ 0 };
			int ImageHeight{ 0 };
			int ImageChannelCount{ 0 };
			if (stbi_info(PageImageFilePath.string().c_str(), &ImageWidth, &ImageHeight, &ImageChannelCount) == 0)
			{
				LOGE("Sprite sheet {}: its image {} couldn't be read.", FileNameForLogging, PageImageFilePath.string());
				return std::nullopt;
			}

			const std::uint32_t UsableWidth = static_cast<std::uint32_t>(ImageWidth) - std::min(2 * Grid.m_MarginInPixels, static_cast<std::uint32_t>(ImageWidth));
			const std::uint32_t UsableHeight = static_cast<std::uint32_t>(ImageHeight) - std::min(2 * Grid.m_MarginInPixels, static_cast<std::uint32_t>(ImageHeight));
			auto resolveCellSize = [&Grid](std::uint32_t GivenCellSize, std::uint32_t GivenCellCount, std::uint32_t UsableSize) -> std::uint32_t
			{
				if (GivenCellSize > 0) { return GivenCellSize; }
				const std::uint32_t CellCount = std::max<std::uint32_t>(GivenCellCount, 1);
				return (UsableSize - (CellCount - 1) * Grid.m_SpacingInPixels) / CellCount;	// No size or count at all: one cell, the whole image
			};
			const std::uint32_t FrameWidth = resolveCellSize(Grid.m_FrameWidthInPixels, Grid.m_ColumnCount, UsableWidth);
			const std::uint32_t FrameHeight = resolveCellSize(Grid.m_FrameHeightInPixels, Grid.m_RowCount, UsableHeight);
			if (FrameWidth == 0 || FrameHeight == 0 || FrameWidth > UsableWidth || FrameHeight > UsableHeight)
			{
				LOGE("Sprite sheet {}: the grid doesn't fit {} (frames 0 pixels wide or high, or bigger than the image).", FileNameForLogging, PageImageFilePath.filename().string());
				return std::nullopt;
			}
			const std::uint32_t ColumnCount = Grid.m_ColumnCount > 0 ? Grid.m_ColumnCount : (UsableWidth + Grid.m_SpacingInPixels) / (FrameWidth + Grid.m_SpacingInPixels);
			const std::uint32_t RowCount = Grid.m_RowCount > 0 ? Grid.m_RowCount : (UsableHeight + Grid.m_SpacingInPixels) / (FrameHeight + Grid.m_SpacingInPixels);
			std::uint32_t PageFrameCount = ColumnCount * RowCount;
			if (Grid.m_FrameCount > 0)	// The total, over every page
			{
				PageFrameCount = std::min(PageFrameCount, Grid.m_FrameCount - std::min(Grid.m_FrameCount, static_cast<std::uint32_t>(Sheet.m_Frames.size())));
			}

			// Row by row, left to right, from the top-left corner
			LastPageFirstFrameIndex = static_cast<std::uint32_t>(Sheet.m_Frames.size());
			const glm::vec2 ImageSize{ static_cast<float>(ImageWidth), static_cast<float>(ImageHeight) };
			for (std::uint32_t CurrentCellIndex{ 0 }; CurrentCellIndex < PageFrameCount; ++CurrentCellIndex)
			{
				const std::uint32_t Column = CurrentCellIndex % ColumnCount;
				const std::uint32_t Row = CurrentCellIndex / ColumnCount;
				const std::uint32_t FrameX = Grid.m_MarginInPixels + Column * (FrameWidth + Grid.m_SpacingInPixels);
				const std::uint32_t FrameY = Grid.m_MarginInPixels + Row * (FrameHeight + Grid.m_SpacingInPixels);
				Sheet.m_Frames.push_back({ makeTextureCoordinateRect(static_cast<float>(FrameX), static_cast<float>(FrameY), static_cast<float>(FrameWidth), static_cast<float>(FrameHeight), ImageSize, IsSmooth),
					glm::vec2(static_cast<float>(FrameWidth), static_cast<float>(FrameHeight)), CurrentPageIndex });
				FramePixelRectangles.emplace_back(FrameX, FrameY, FrameWidth, FrameHeight);
			}
			if (Grid.m_FrameCount > 0 && Sheet.m_Frames.size() >= Grid.m_FrameCount && CurrentPageIndex + 1 < Description.m_PageImageFilePaths.size())
			{
				LOGW("Sprite sheet {}: its {} frames are used up by page {}, the {} pages after it are left out.", FileNameForLogging, Grid.m_FrameCount,
					PageImageFilePath.filename().string(), Description.m_PageImageFilePaths.size() - CurrentPageIndex - 1);
				Description.m_PageImageFilePaths.resize(CurrentPageIndex + 1);
				break;
			}
		}

		// No frame count given: the last page's unused cells at the end are dropped (they'd flash empty at the end of every loop)
		if (Grid.m_FrameCount == 0 && !Sheet.m_Frames.empty())
		{
			int LastPageWidth{ 0 };
			int LastPageHeight{ 0 };
			unsigned char* LastPagePixels = decodeSpriteSheetPage(Description, Description.m_PageImageFilePaths.back(), LastPageWidth, LastPageHeight);
			if (LastPagePixels != nullptr)
			{
				const std::size_t FrameCountBeforeTrimming = Sheet.m_Frames.size();
				while (Sheet.m_Frames.size() > LastPageFirstFrameIndex && isPixelRectangleEmpty(LastPagePixels, LastPageWidth, FramePixelRectangles[Sheet.m_Frames.size() - 1]))
				{
					Sheet.m_Frames.pop_back();
				}
				stbi_image_free(LastPagePixels);
				if (Sheet.m_Frames.size() != FrameCountBeforeTrimming)
				{
					LOG("Sprite sheet {}: {} empty cells at the end of {} left out.", FileNameForLogging, FrameCountBeforeTrimming - Sheet.m_Frames.size(),
						Description.m_PageImageFilePaths.back().filename().string());
				}
			}
			// The last page holding nothing at all: it doesn't need uploading
			if (Sheet.m_Frames.size() == LastPageFirstFrameIndex && Description.m_PageImageFilePaths.size() > 1)
			{
				Description.m_PageImageFilePaths.pop_back();
			}
		}
	}

	if (Sheet.m_Frames.empty())
	{
		LOGE("Sprite sheet {} has no frames.", FileNameForLogging);
		return std::nullopt;
	}

	// 3. CLIPS from the sidecar (on top of any Aseprite tags). Frames that don't exist are dropped, with a warning.
	for (const SpriteSheetFormat::ClipBlock& CurrentClip : Sidecar.m_Clips)
	{
		std::vector<std::uint32_t> ClipFrames = CurrentClip.m_FrameIndices;
		if (CurrentClip.m_FrameRange.has_value())
		{
			const auto [FirstFrame, LastFrame] = CurrentClip.m_FrameRange.value();
			for (std::uint32_t CurrentFrame = std::min(FirstFrame, LastFrame); CurrentFrame <= std::max(FirstFrame, LastFrame); ++CurrentFrame)
			{
				ClipFrames.push_back(CurrentFrame);
			}
			if (FirstFrame > LastFrame)
			{
				std::ranges::reverse(ClipFrames);
			}
		}
		else if (CurrentClip.m_FrameIndices.empty())	// Neither: the whole sheet, in order
		{
			ClipFrames.resize(Sheet.m_Frames.size());
			std::iota(ClipFrames.begin(), ClipFrames.end(), 0u);
		}
		const std::size_t FrameCountBeforeCheck = ClipFrames.size();
		std::erase_if(ClipFrames, [&Sheet](std::uint32_t FrameIndex) { return FrameIndex >= Sheet.m_Frames.size(); });
		if (ClipFrames.size() != FrameCountBeforeCheck)
		{
			LOGW("Sprite sheet {}: clip '{}' uses frames past the last one ({}), they're skipped.", FileNameForLogging, CurrentClip.m_ClipName, Sheet.m_Frames.size() - 1);
		}
		if (ClipFrames.empty())
		{
			LOGW("Sprite sheet {}: clip '{}' has no frames, it's skipped.", FileNameForLogging, CurrentClip.m_ClipName);
			continue;
		}

		const std::optional<ClipPlayback> Playback = parseClipPlaybackName(CurrentClip.m_PlaybackName);
		if (!Playback.has_value())
		{
			LOGW("Sprite sheet {}: clip '{}' has an unknown playback '{}' (loop, pingPong or once), it loops.", FileNameForLogging, CurrentClip.m_ClipName, CurrentClip.m_PlaybackName);
		}
		const float FrameDuration = 1.0f / std::max(CurrentClip.m_FramesPerSecond, 0.001f);
		std::vector<float> ClipDurations(ClipFrames.size(), FrameDuration);
		Sheet.m_Clips.push_back(buildAnimationClip(CurrentClip.m_ClipName, std::move(ClipFrames), std::move(ClipDurations), Playback.value_or(ClipPlayback::Loop)));
	}

	return Description;
}

}
