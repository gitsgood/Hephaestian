/**
 * @file ModelImporter.h
 * @brief Model files (.gltf, .glb, .obj, .fbx) into the renderer, one multi-mesh model each.
 */
#ifndef HEPHAESTIAN_MODELIMPORTER_H
#define HEPHAESTIAN_MODELIMPORTER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <glm/glm.hpp>

#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Core/HashMap.h"

// Forward declared so including this header doesn't drag fastgltf (and simdjson) into every file that imports models
namespace fastgltf { class Parser; }

namespace Hephaestian::Assets
{

/**
 * @brief What an import hands back: the renderer's model, plus its size so it can be placed and scaled sensibly.
 */
struct ImportedModel
{
	std::uint32_t m_ModelIdentifier{ 0 };		///< The renderer's model (what ModelRendererComponent draws)
	glm::vec3 m_BoundsMinimumCorner{ 0.0f };	///< Axis-aligned bounds of the whole model, in model space
	glm::vec3 m_BoundsMaximumCorner{ 0.0f };	///< Axis-aligned bounds of the whole model, in model space
	std::uint32_t m_SubmeshCount{ 0 };			///< How many pieces it's drawn in

	/**
	 * @brief The model's size.
	 * @return Width, height and depth of its bounds, in model units.
	 */
	[[nodiscard]] glm::vec3 calculateBoundsSize() const noexcept { return m_BoundsMaximumCorner - m_BoundsMinimumCorner; }
	/**
	 * @brief The middle of the model.
	 * @return The centre of its bounds, in model space.
	 */
	[[nodiscard]] glm::vec3 calculateBoundsCentre() const noexcept { return (m_BoundsMinimumCorner + m_BoundsMaximumCorner) * 0.5f; }
};

/**
 * @brief Loads model files into the renderer, as one multi-mesh model each.
 *
 * - .gltf / .glb: fastgltf (SIMD JSON parsing, the fastest glTF loader there is).
 * - .obj / .fbx: ufbx (also picks up .mtl files and embedded FBX textures).
 *
 * Every file comes out the same way: one ModelSubmesh per (mesh piece, material) per node it's placed on, with the node
 * hierarchy flattened into each submesh's local transform. Geometry and textures used several times in one file are
 * only uploaded once. Importing the same file twice returns the cached model.
 *
 * Relative paths are looked up in the runtime assets folder (e.g. "Models/Duck.glb" -> `<exe>/assets/Models/Duck.glb`).
 */
class ModelImporter
{
public:
	/**
	 * @brief Ready to import.
	 * @param RendererToUploadInto Where models and their textures are uploaded.
	 */
	explicit ModelImporter(Vulkanised::VulkanRenderer& RendererToUploadInto);
	~ModelImporter();

	ModelImporter(const ModelImporter&) = delete;
	ModelImporter& operator=(const ModelImporter&) = delete;

	/**
	 * @brief Imports a model file (or returns it from the cache if it was imported before).
	 * @param ModelFilePath Absolute, or relative to the runtime assets folder.
	 * @return The model, or std::nullopt (and logged) if the file couldn't be imported.
	 */
	[[nodiscard]] std::optional<ImportedModel> importModelFromFile(const std::filesystem::path& ModelFilePath);

	/**
	 * @brief Whether a file looks like a model this importer reads.
	 * @param ModelFilePath The file.
	 * @return True for .gltf, .glb, .obj and .fbx (any case).
	 */
	[[nodiscard]] static bool isModelFileExtensionSupported(const std::filesystem::path& ModelFilePath);

private:
	/**
	 * @brief Imports a .gltf or .glb with fastgltf.
	 * @param AbsoluteModelFilePath The file.
	 * @return The model, or std::nullopt (and logged).
	 */
	[[nodiscard]] std::optional<ImportedModel> importGltfModel(const std::filesystem::path& AbsoluteModelFilePath);
	/**
	 * @brief Imports an .obj or .fbx with ufbx.
	 * @param AbsoluteModelFilePath The file.
	 * @return The model, or std::nullopt (and logged).
	 */
	[[nodiscard]] std::optional<ImportedModel> importUfbxModel(const std::filesystem::path& AbsoluteModelFilePath);

	/**
	 * @brief Loads a texture a model refers to by file, once: shared between every model that uses it.
	 * @param AbsoluteTextureFilePath The image.
	 * @return The texture, or the renderer's white texture if it's missing or unreadable (logged).
	 */
	[[nodiscard]] std::uint32_t loadTextureFromFileOnce(const std::filesystem::path& AbsoluteTextureFilePath);

	Vulkanised::VulkanRenderer& m_Renderer;			///< Where everything is uploaded
	std::unique_ptr<fastgltf::Parser> m_GltfParser;	///< Reused across imports, so simdjson keeps its buffers allocated

	HashMap<std::string, ImportedModel> m_ImportedModelsByAbsoluteFilePath;			///< The model cache (UTF-8 absolute paths)
	HashMap<std::string, std::uint32_t> m_TextureIdentifiersByAbsoluteFilePath;		///< The texture cache (UTF-8 absolute paths)
};

}

#endif // !HEPHAESTIAN_MODELIMPORTER_H
