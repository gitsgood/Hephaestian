/**
 * @file AssetLibrary.h
 * @brief The one place assets are loaded from: models, textures, primitives, sprite sheets and sounds, each loaded once.
 */
#ifndef HEPHAESTIAN_ASSETLIBRARY_H
#define HEPHAESTIAN_ASSETLIBRARY_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Assets/ModelImporter.h"
#include "Hephaestian/Core/HashMap.h"
#include "Hephaestian/Assets/SpriteSheet.h"
#include "Hephaestian/Audio/AudioEngine.h"

namespace Hephaestian::Assets
{

/**
 * @brief Shapes the engine can make without any file. The names are what scene files use ("primitive": "cube").
 */
enum class PrimitiveShape : int
{
	Quad = 0,	///< "quad": 1 x 1 m, lying flat on the XZ plane, facing up (+y): a floor tile
	Cube,		///< "cube": 1 x 1 x 1 m, centred on the origin
	Sphere,		///< "sphere": 1 m across (radius 0.5), centred on the origin
	Sprite,		///< "sprite": 1 x 1 m, standing up in the XY plane, facing +z (towards an unrotated camera): what every sprite draws with
	Count		///< How many shapes there are (not a shape)
};

/**
 * @brief Reads a primitive's scene file name.
 * @param PrimitiveShapeName "quad", "cube", "sphere" or "sprite".
 * @return The shape, or std::nullopt for anything else.
 */
[[nodiscard]] std::optional<PrimitiveShape> parsePrimitiveShapeName(std::string_view PrimitiveShapeName);
/**
 * @brief A primitive's scene file name.
 * @param Shape The shape.
 * @return "quad", "cube", "sphere" or "sprite".
 */
[[nodiscard]] std::string_view getPrimitiveShapeName(PrimitiveShape Shape);

/**
 * @brief Every model, texture, primitive, sprite sheet and sound the engine has loaded, each loaded only ONCE no matter
 * how many scenes (or how many times one scene) ask for it.
 *
 * What a new scene no longer uses is let go when it loads (releaseAssetsUnusedByScene).
 *
 * Paths are relative to the assets folder, e.g. "Models/Duck.glb" or "resources/Hephaestos.png": `<exe folder>/assets`,
 * or in Debug builds the repository's assets folder first (see resolveAssetPath).
 */
class AssetLibrary
{
public:
	/**
	 * @brief An empty library.
	 * @param RendererToUploadInto Where models, textures and sprite sheets are uploaded.
	 * @param AudioEngineToLoadInto What sounds are made with.
	 */
	AssetLibrary(Vulkanised::VulkanRenderer& RendererToUploadInto, Audio::AudioEngine& AudioEngineToLoadInto);

	/**
	 * @brief A model file (.gltf, .glb, .obj, .fbx), loaded on first use.
	 * @param ModelRelativePath Relative to the assets folder (see resolveAssetPath).
	 * @return The model, or std::nullopt (and logged) if the file couldn't be loaded.
	 */
	[[nodiscard]] std::optional<ImportedModel> getOrLoadModel(const std::filesystem::path& ModelRelativePath);
	/**
	 * @brief An image, as a texture, loaded on first use.
	 * @param TextureRelativePath Relative to the assets folder (see resolveAssetPath).
	 * @return The renderer's texture identifier, or std::nullopt (and logged) if the file couldn't be loaded.
	 */
	[[nodiscard]] std::optional<std::uint32_t> getOrLoadTexture(const std::filesystem::path& TextureRelativePath);

	/**
	 * @brief A primitive shape as a model, built on first use, then shared.
	 *
	 * Untextured (white), so a texture override gives them their look.
	 * @param Shape The shape.
	 * @return The renderer's model identifier.
	 */
	[[nodiscard]] std::uint32_t getOrCreatePrimitiveModel(PrimitiveShape Shape);

	/**
	 * @brief A sound file (.mp3, .wav, .flac, .ogg), loaded on first use.
	 *
	 * Decoded whole if it's short, streamed while playing if it's long (see AudioEngine). Shared by everything playing it.
	 * @param SoundRelativePath Relative to the assets folder (see resolveAssetPath).
	 * @return The sound, or nullptr (and logged) if it couldn't be loaded.
	 */
	[[nodiscard]] std::shared_ptr<const Audio::SoundAsset> getOrLoadSound(const std::filesystem::path& SoundRelativePath);

	/**
	 * @brief A sprite sheet (sidecar .spritesheet.json, Aseprite JSON export, or a folder holding a sidecar: see
	 * SpriteSheet.h), loaded on first use.
	 *
	 * Its pages are decoded in parallel and uploaded, its frames and clips are ready. Shared by every sprite using it.
	 * @param SpriteSheetRelativePath Relative to the assets folder (see resolveAssetPath).
	 * @return The sheet, or nullptr (and logged) if it couldn't be loaded.
	 */
	[[nodiscard]] std::shared_ptr<const SpriteSheet> getOrLoadSpriteSheet(const std::filesystem::path& SpriteSheetRelativePath);

	/**
	 * @brief Frees what the scene just loaded doesn't use (Scene::loadFromJsonFile calls this once the new scene is
	 * loaded, so assets both scenes use are never reloaded).
	 *
	 * Frees:
	 * - every sprite sheet and sound nothing holds anymore (only this library's cache does: a playing sound is held by
	 *   its voice),
	 * - every texture from getOrLoadTexture that isn't in TextureIdentifiersStillInUse.
	 *
	 * Models (and the textures they brought along) stay for now: their geometry can't be freed yet.
	 * Built for one scene at a time: a second scene sharing this library would lose the textures only it uses.
	 * @param TextureIdentifiersStillInUse The textures in the new scene's asset table.
	 */
	void releaseAssetsUnusedByScene(std::span<const std::uint32_t> TextureIdentifiersStillInUse);

private:
	Vulkanised::VulkanRenderer& m_Renderer;	///< Where everything is uploaded
	Audio::AudioEngine& m_AudioEngine;		///< What sounds are made with
	ModelImporter m_ModelImporter;			///< Caches models by path itself
	HashMap<std::string, std::uint32_t> m_TextureIdentifiersByAbsolutePath;	///< Textures from getOrLoadTexture
	std::array<std::optional<std::uint32_t>, static_cast<std::size_t>(PrimitiveShape::Count)> m_PrimitiveModelIdentifiers;	///< One per shape, once built
	HashMap<std::string, std::shared_ptr<const SpriteSheet>> m_SpriteSheetsByAbsolutePath;	///< Sprite sheet cache
	HashMap<std::string, std::shared_ptr<Audio::SoundAsset>> m_SoundsByAbsolutePath;		///< Sound cache
};

}

#endif // !HEPHAESTIAN_ASSETLIBRARY_H
