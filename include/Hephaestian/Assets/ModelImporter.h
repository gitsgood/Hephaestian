#ifndef HEPHAESTIAN_MODELIMPORTER_H
#define HEPHAESTIAN_MODELIMPORTER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include <Vulkanised/VulkanRenderer.h>

// Forward declared so including this header doesn't drag fastgltf (and simdjson) into every file that imports models
namespace fastgltf { class Parser; }

namespace Hephaestian::Assets
{

// What an import hands back: the renderer's model, plus its size so it can be placed/scaled sensibly
struct ImportedModel
{
	std::uint32_t m_ModelIdentifier{ 0 };
	glm::vec3 m_BoundsMinimumCorner{ 0.0f };	// Axis-aligned bounds of the whole model, in model space
	glm::vec3 m_BoundsMaximumCorner{ 0.0f };
	std::uint32_t m_SubmeshCount{ 0 };

	[[nodiscard]] glm::vec3 calculateBoundsSize() const noexcept { return m_BoundsMaximumCorner - m_BoundsMinimumCorner; }
	[[nodiscard]] glm::vec3 calculateBoundsCentre() const noexcept { return (m_BoundsMinimumCorner + m_BoundsMaximumCorner) * 0.5f; }
};

/*
* Loads model files into the renderer, as one multi-mesh model each.
*	.gltf / .glb	-> fastgltf	(SIMD JSON parsing, the fastest glTF loader there is)
*	.obj / .fbx		-> ufbx		(also picks up .mtl files and embedded FBX textures)
*
* Every file comes out the same way: one ModelSubmesh per (mesh piece, material) per node it's placed on, with the node
* hierarchy flattened into each submesh's local transform. Geometry and textures used several times in one file are
* only uploaded once. Importing the same file twice returns the cached model.
*
* Relative paths are looked up in the runtime assets folder (e.g. "Models/Duck.glb" -> <exe>/assets/Models/Duck.glb).
*/
class ModelImporter
{
public:
	explicit ModelImporter(Vulkanised::VulkanRenderer& RendererToUploadInto);
	~ModelImporter();

	ModelImporter(const ModelImporter&) = delete;
	ModelImporter& operator=(const ModelImporter&) = delete;

	// Returns std::nullopt (and logs why) if the file couldn't be imported
	[[nodiscard]] std::optional<ImportedModel> importModelFromFile(const std::filesystem::path& ModelFilePath);

	[[nodiscard]] static bool isModelFileExtensionSupported(const std::filesystem::path& ModelFilePath);

private:
	[[nodiscard]] std::optional<ImportedModel> importGltfModel(const std::filesystem::path& AbsoluteModelFilePath);
	[[nodiscard]] std::optional<ImportedModel> importUfbxModel(const std::filesystem::path& AbsoluteModelFilePath);

	// Textures referenced by file are shared between every model that uses them
	[[nodiscard]] std::uint32_t loadTextureFromFileOnce(const std::filesystem::path& AbsoluteTextureFilePath);

	Vulkanised::VulkanRenderer& m_Renderer;
	std::unique_ptr<fastgltf::Parser> m_GltfParser;	// Reused across imports, so simdjson keeps its buffers allocated

	std::unordered_map<std::string, ImportedModel> m_ImportedModelsByAbsoluteFilePath;
	std::unordered_map<std::string, std::uint32_t> m_TextureIdentifiersByAbsoluteFilePath;
};

}

#endif // !HEPHAESTIAN_MODELIMPORTER_H
