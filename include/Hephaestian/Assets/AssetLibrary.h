#ifndef HEPHAESTIAN_ASSETLIBRARY_H
#define HEPHAESTIAN_ASSETLIBRARY_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Assets/ModelImporter.h"
#include "Hephaestian/Assets/SpriteSheet.h"

namespace Hephaestian::Assets
{

// Shapes the engine can make without any file. The names are what scene files use ("primitive": "cube").
enum class PrimitiveShape : int
{
	Quad = 0,	// 1 x 1 m, lying flat on the XZ plane, facing up (+y): a floor tile
	Cube,		// 1 x 1 x 1 m, centred on the origin
	Sphere,		// 1 m across (radius 0.5), centred on the origin
	Sprite,		// 1 x 1 m, standing up in the XY plane, facing +z (towards an unrotated camera): what every sprite draws with
	Count
};

[[nodiscard]] std::optional<PrimitiveShape> parsePrimitiveShapeName(std::string_view PrimitiveShapeName);
[[nodiscard]] std::string_view getPrimitiveShapeName(PrimitiveShape Shape);

/*
* Every model, texture and primitive the engine has loaded, each loaded only ONCE no matter how many scenes (or how many
* times one scene) ask for it. Created once and shared: Vulkanised can't free GPU memory yet, so reloading the same file
* for every scene load would just keep eating it.
*
* Paths are relative to the runtime assets folder (<exe folder>/assets), e.g. "Models/Duck.glb" or "resources/Hephaestos.png".
*/
class AssetLibrary
{
public:
	explicit AssetLibrary(Vulkanised::VulkanRenderer& RendererToUploadInto);

	// std::nullopt (and logged) if the file couldn't be loaded
	[[nodiscard]] std::optional<ImportedModel> getOrLoadModel(const std::filesystem::path& ModelRelativePath);
	[[nodiscard]] std::optional<std::uint32_t> getOrLoadTexture(const std::filesystem::path& TextureRelativePath);

	// Built on first use, then shared. Untextured (white), so a texture override gives them their look.
	[[nodiscard]] std::uint32_t getOrCreatePrimitiveModel(PrimitiveShape Shape);

	// A sprite sheet (sidecar .spritesheet.json or Aseprite JSON export, see SpriteSheet.h): its texture uploaded, frames and
	// clips ready. Shared by every sprite using it. nullptr (and logged) if it couldn't be loaded.
	[[nodiscard]] std::shared_ptr<const SpriteSheet> getOrLoadSpriteSheet(const std::filesystem::path& SpriteSheetRelativePath);

private:
	Vulkanised::VulkanRenderer& m_Renderer;
	ModelImporter m_ModelImporter;	// Caches models by path itself
	std::unordered_map<std::string, std::uint32_t> m_TextureIdentifiersByAbsolutePath;
	std::array<std::optional<std::uint32_t>, static_cast<std::size_t>(PrimitiveShape::Count)> m_PrimitiveModelIdentifiers;
	std::unordered_map<std::string, std::shared_ptr<const SpriteSheet>> m_SpriteSheetsByAbsolutePath;
};

}

#endif // !HEPHAESTIAN_ASSETLIBRARY_H
