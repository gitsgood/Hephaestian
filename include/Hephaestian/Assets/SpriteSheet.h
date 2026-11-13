#ifndef HEPHAESTIAN_SPRITESHEET_H
#define HEPHAESTIAN_SPRITESHEET_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <Vulkanised/Rendering/TextureOptions.h>

/*
* A sprite sheet: one image holding many frames, plus named animation clips over those frames. Loaded once by the
* AssetLibrary and shared (std::shared_ptr) by every sprite using it, so a thousand slimes cost one texture and one
* frame table.
*
* Two ways to describe one on disk:
*
* 1. A sidecar file, "<name>.spritesheet.json", next to the image. Either a GRID of equal frames:
*	{
*	  "image": "Slime.png",							<- relative to this file
*	  "grid": { "frameWidth": 32, "frameHeight": 32 },	<- or "columns"/"rows"; also "frameCount", "margin", "spacing"
*	  "pixelsPerUnit": 32,								<- how many pixels make one world unit (metre)
*	  "filtering": "pixelated",						<- or "smooth"
*	  "mipmaps": false,									<- off by default: neighbouring frames would bleed into each other far away
*	  "clips": [
*	    { "name": "idle", "range": [0, 3], "framesPerSecond": 6, "playback": "pingPong" },	<- or "frames": [0, 1, 2, 3]
*	    { "name": "hop",  "frames": [4, 5, 6, 7], "framesPerSecond": 10 }					<- "playback": "loop" (default), "pingPong" or "once"
*	  ]
*	}
*    ...or an Aseprite export instead of image + grid: { "aseprite": "Slime.json", "pixelsPerUnit": 32, "clips": [...] }
*    (its frames and tags are used, the sidecar adds settings and can add more clips).
*
*    A long animation can be spread over several images (PAGES), the grid cutting each one the same way, frames numbered
*    across them in order (page 1's cells, then page 2's...):
*	  "images": [ "Walk_1.png", "Walk_2.png" ],		<- instead of "image"
*	  "imageFolder": ".",								<- or: every image in that folder, ordered by the number ending its name
*    Without a "frameCount", empty cells at the end of the last page are dropped (fully transparent, or black).
*    Images without transparency can get some: "transparentColour": [0, 0, 0] (0 to 1, like tints) turns every pixel that
*    close to it ("transparentColourTolerance", default 0.1) see-through.
*    A clip with neither "frames" nor "range" plays every frame of the sheet.
*
*    "blending": "cutout" (default) shows a pixel fully or not at all (its alpha split at 50%). "additive" ADDS the art
*    onto what's behind it instead: black adds nothing and bright parts glow, so glows, fire, sparks, light painted on a
*    black background look right without any transparency in the image.
*
* 2. An Aseprite JSON export on its own (File > Export Sprite Sheet, "JSON Data" ticked, Array or Hash). Every frame keeps
*    its own rectangle and duration, every tag becomes a clip (forward, reverse, ping-pong). 32 pixels per unit, pixelated.
*
* A FOLDER can be given instead of a file: the one .spritesheet.json inside it is used.
*/
namespace Hephaestian::Assets
{

enum class SpriteBlendMode
{
	Cutout,		// Each pixel fully there or not at all (alpha below 50% disappears)
	Additive	// Added onto what's behind it: black is invisible, bright parts glow
};

struct SpriteSheetFrame
{
	glm::vec4 m_TextureCoordinateRect{ 0.0f, 0.0f, 1.0f, 1.0f };	// (offset u, offset v, size u, size v), straight into RenderInstance
	glm::vec2 m_SizeInPixels{ 0.0f };								// The sprite's world size is this / pixels per unit
	std::uint32_t m_PageIndex{ 0 };									// Which image it's on (SpriteSheet::m_PageTextureIdentifiers)
};

struct SpriteAnimationClip
{
	std::string m_ClipName;
	// What actually plays, step by step: ping-pong clips are already unrolled (0 1 2 3 2 1), reverse ones reversed
	std::vector<std::uint32_t> m_PlaybackFrameIndices;
	std::vector<float> m_PlaybackStepEndTimesInSeconds;	// When each step ends, from the start of the clip (sorted, for a binary search)
	float m_TotalDurationInSeconds{ 0.0f };
	bool m_ShouldLoop{ true };							// "once" clips stop on their last frame

	// The frame showing ElapsedTimeInSeconds into the clip (wrapped for looping clips, held at the end otherwise)
	[[nodiscard]] std::uint32_t getFrameIndexAtTime(float ElapsedTimeInSeconds) const noexcept;
};

struct SpriteSheet
{
	std::filesystem::path m_SourceFilePath;			// The file it was loaded from (absolute)
	std::vector<std::uint32_t> m_PageTextureIdentifiers;	// One texture per image (most sheets have one)
	std::uint32_t m_QuadMeshIdentifier{ 0 };		// The shared 1 x 1 sprite quad every sprite draws with
	float m_PixelsPerUnit{ 32.0f };
	SpriteBlendMode m_BlendMode{ SpriteBlendMode::Cutout };
	std::vector<SpriteSheetFrame> m_Frames;
	std::vector<SpriteAnimationClip> m_Clips;

	[[nodiscard]] std::optional<std::uint32_t> findClipIndex(std::string_view ClipName) const noexcept;
};

// Everything a sprite sheet file says, before anything is uploaded (the AssetLibrary fills in texture and mesh)
struct SpriteSheetDescription
{
	SpriteSheet m_SpriteSheet;						// Frames, clips, pixels per unit, source path
	std::vector<std::filesystem::path> m_PageImageFilePaths;	// Absolute, in page order
	Vulkanised::Rendering::TextureOptions m_TextureOptions;
	std::optional<glm::vec3> m_TransparentColour;				// sRGB 0..1: pixels this close to it lose their opacity once decoded
	float m_TransparentColourTolerance{ 0.1f };
};

// Reads either kind of file described above (or the sidecar inside a folder). std::nullopt (and logged) if it can't be
// read or makes no sense.
[[nodiscard]] std::optional<SpriteSheetDescription> readSpriteSheetFile(const std::filesystem::path& SpriteSheetFilePath);

// Decodes one page as RGBA (stbi_image_free it), with the description's transparent colour applied. Safe to call from
// several threads at once. nullptr if it couldn't be read.
[[nodiscard]] unsigned char* decodeSpriteSheetPage(const SpriteSheetDescription& Description, const std::filesystem::path& PageImageFilePath, int& WidthInPixels, int& HeightInPixels);

}

#endif // !HEPHAESTIAN_SPRITESHEET_H
