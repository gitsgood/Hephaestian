#include "Hephaestian/Assets/ModelImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <limits>
#include <map>
#include <span>
#include <tuple>
#include <variant>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>	// Lets fastgltf read accessors straight into glm::vec2/3/4
#include <ufbx.h>

#include <Vulkanised/Logger.h>

namespace Hephaestian::Assets
{

namespace
{
	constexpr glm::vec3 c_FallbackNormalPointingUp{ 0.0f, 1.0f, 0.0f };

	// Grows to wrap around whatever points it's given
	struct AxisAlignedBounds
	{
		glm::vec3 m_MinimumCorner{ std::numeric_limits<float>::max() };
		glm::vec3 m_MaximumCorner{ std::numeric_limits<float>::lowest() };

		[[nodiscard]] bool isEmpty() const noexcept { return m_MinimumCorner.x > m_MaximumCorner.x; }

		void expandToIncludePoint(const glm::vec3& PointToInclude) noexcept
		{
			m_MinimumCorner = glm::min(m_MinimumCorner, PointToInclude);
			m_MaximumCorner = glm::max(m_MaximumCorner, PointToInclude);
		}

		// Transforms all 8 corners rather than every vertex, so placing a mesh 1000 times costs 8000 points, not millions
		void expandToIncludeTransformedBounds(const AxisAlignedBounds& BoundsToInclude, const glm::mat4& TransformToApply) noexcept
		{
			if (BoundsToInclude.isEmpty())
			{
				return;
			}
			for (int CurrentCornerIndex{ 0 }; CurrentCornerIndex < 8; ++CurrentCornerIndex)
			{
				const glm::vec3 CurrentCorner{
					(CurrentCornerIndex & 1) ? BoundsToInclude.m_MaximumCorner.x : BoundsToInclude.m_MinimumCorner.x,
					(CurrentCornerIndex & 2) ? BoundsToInclude.m_MaximumCorner.y : BoundsToInclude.m_MinimumCorner.y,
					(CurrentCornerIndex & 4) ? BoundsToInclude.m_MaximumCorner.z : BoundsToInclude.m_MinimumCorner.z
				};
				expandToIncludePoint(glm::vec3(TransformToApply * glm::vec4(CurrentCorner, 1.0f)));
			}
		}
	};

	// A piece of geometry that's been uploaded, ready to be placed (possibly many times) as a ModelSubmesh
	struct UploadedSubmeshGeometry
	{
		std::uint32_t m_MeshIdentifier{ 0 };
		std::uint32_t m_TextureIdentifier{ 0 };
		AxisAlignedBounds m_LocalBounds;
	};

	[[nodiscard]] std::string convertPathToUtf8String(const std::filesystem::path& PathToConvert)
	{
		const std::u8string Utf8Path = PathToConvert.generic_u8string();
		return std::string(Utf8Path.begin(), Utf8Path.end());
	}

	[[nodiscard]] std::string getLowercaseFileExtension(const std::filesystem::path& FilePath)
	{
		std::string LowercaseExtension = convertPathToUtf8String(FilePath.extension());
		std::ranges::transform(LowercaseExtension, LowercaseExtension.begin(), [](char CurrentCharacter) { return static_cast<char>(std::tolower(static_cast<unsigned char>(CurrentCharacter))); });
		return LowercaseExtension;
	}

	// Area-weighted smooth normals, for files that don't provide any (glTF allows that, and the lighting needs them)
	void generateSmoothNormalsForTriangles(std::vector<Vulkanised::Vertex>& VerticesToFill, const std::vector<std::uint32_t>& TriangleIndices)
	{
		for (Vulkanised::Vertex& CurrentVertex : VerticesToFill)
		{
			CurrentVertex.normal = glm::vec3(0.0f);
		}

		for (size_t CurrentTriangleFirstIndex{ 0 }; CurrentTriangleFirstIndex + 2 < TriangleIndices.size(); CurrentTriangleFirstIndex += 3)
		{
			const std::uint32_t FirstVertexIndex = TriangleIndices[CurrentTriangleFirstIndex];
			const std::uint32_t SecondVertexIndex = TriangleIndices[CurrentTriangleFirstIndex + 1];
			const std::uint32_t ThirdVertexIndex = TriangleIndices[CurrentTriangleFirstIndex + 2];
			if (FirstVertexIndex >= VerticesToFill.size() || SecondVertexIndex >= VerticesToFill.size() || ThirdVertexIndex >= VerticesToFill.size())
			{
				continue;
			}

			// Not normalised on purpose: the cross product's length is twice the triangle's area, so big triangles weigh more
			const glm::vec3 AreaWeightedFaceNormal = glm::cross(
				VerticesToFill[SecondVertexIndex].pos - VerticesToFill[FirstVertexIndex].pos,
				VerticesToFill[ThirdVertexIndex].pos - VerticesToFill[FirstVertexIndex].pos);
			VerticesToFill[FirstVertexIndex].normal += AreaWeightedFaceNormal;
			VerticesToFill[SecondVertexIndex].normal += AreaWeightedFaceNormal;
			VerticesToFill[ThirdVertexIndex].normal += AreaWeightedFaceNormal;
		}

		for (Vulkanised::Vertex& CurrentVertex : VerticesToFill)
		{
			const float AccumulatedNormalLength = glm::length(CurrentVertex.normal);
			CurrentVertex.normal = AccumulatedNormalLength > 1e-12f ? CurrentVertex.normal / AccumulatedNormalLength : c_FallbackNormalPointingUp;
		}
	}

	// - glTF helpers
	[[nodiscard]] glm::mat4 convertGltfMatrixToGlm(const fastgltf::math::nmat4x4& GltfMatrix) noexcept
	{
		glm::mat4 ConvertedMatrix{ 1.0f };
		for (int CurrentColumnIndex{ 0 }; CurrentColumnIndex < 4; ++CurrentColumnIndex)
		{
			for (int CurrentRowIndex{ 0 }; CurrentRowIndex < 4; ++CurrentRowIndex)
			{
				ConvertedMatrix[CurrentColumnIndex][CurrentRowIndex] = static_cast<float>(GltfMatrix[static_cast<size_t>(CurrentColumnIndex)][static_cast<size_t>(CurrentRowIndex)]);
			}
		}
		return ConvertedMatrix;
	}

	// Raw bytes behind a buffer that fastgltf already loaded into memory (empty if it isn't in memory)
	[[nodiscard]] std::span<const std::byte> getBytesOfLoadedGltfDataSource(const fastgltf::DataSource& DataSourceToRead)
	{
		return std::visit(fastgltf::visitor{
			[](const fastgltf::sources::Array& ArraySource) { return std::span<const std::byte>(ArraySource.bytes.data(), ArraySource.bytes.size()); },
			[](const fastgltf::sources::Vector& VectorSource) { return std::span<const std::byte>(VectorSource.bytes.data(), VectorSource.bytes.size()); },
			[](const fastgltf::sources::ByteView& ByteViewSource) { return ByteViewSource.bytes; },
			[](const auto&) { return std::span<const std::byte>{}; }
		}, DataSourceToRead);
	}

	// - ufbx helpers
	[[nodiscard]] glm::mat4 convertUfbxMatrixToGlm(const ufbx_matrix& UfbxMatrix) noexcept
	{
		// ufbx matrices are affine 3x4, column major (3 basis columns + translation)
		glm::mat4 ConvertedMatrix{ 1.0f };
		for (int CurrentColumnIndex{ 0 }; CurrentColumnIndex < 4; ++CurrentColumnIndex)
		{
			const ufbx_vec3& CurrentColumn = UfbxMatrix.cols[CurrentColumnIndex];
			ConvertedMatrix[CurrentColumnIndex] = glm::vec4(static_cast<float>(CurrentColumn.x), static_cast<float>(CurrentColumn.y), static_cast<float>(CurrentColumn.z), CurrentColumnIndex == 3 ? 1.0f : 0.0f);
		}
		return ConvertedMatrix;
	}

	[[nodiscard]] glm::vec3 convertUfbxVectorToGlm(const ufbx_vec3& UfbxVector) noexcept
	{
		return { static_cast<float>(UfbxVector.x), static_cast<float>(UfbxVector.y), static_cast<float>(UfbxVector.z) };
	}

	[[nodiscard]] std::filesystem::path convertUfbxStringToPath(const ufbx_string& UfbxString)
	{
		const std::u8string Utf8String(reinterpret_cast<const char8_t*>(UfbxString.data), UfbxString.length);
		return std::filesystem::path(Utf8String);
	}
}

ModelImporter::ModelImporter(Vulkanised::VulkanRenderer& RendererToUploadInto)
	: m_Renderer(RendererToUploadInto)
	, m_GltfParser(std::make_unique<fastgltf::Parser>())
{
}

// Defined here, where fastgltf::Parser is a complete type (the unique_ptr needs it to delete)
ModelImporter::~ModelImporter() = default;

bool ModelImporter::isModelFileExtensionSupported(const std::filesystem::path& ModelFilePath)
{
	const std::string LowercaseExtension = getLowercaseFileExtension(ModelFilePath);
	return LowercaseExtension == ".gltf" || LowercaseExtension == ".glb" || LowercaseExtension == ".obj" || LowercaseExtension == ".fbx";
}

std::optional<ImportedModel> ModelImporter::importModelFromFile(const std::filesystem::path& ModelFilePath)
{
	std::filesystem::path AbsoluteModelFilePath = ModelFilePath.is_absolute() ? ModelFilePath : Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / ModelFilePath;
	std::error_code CanonicalisationError;
	const std::filesystem::path CanonicalModelFilePath = std::filesystem::weakly_canonical(AbsoluteModelFilePath, CanonicalisationError);
	if (!CanonicalisationError)
	{
		AbsoluteModelFilePath = CanonicalModelFilePath;
	}
	const std::string AbsoluteModelFilePathAsUtf8 = convertPathToUtf8String(AbsoluteModelFilePath);

	if (const auto CachedImportedModel = m_ImportedModelsByAbsoluteFilePath.find(AbsoluteModelFilePathAsUtf8); CachedImportedModel != m_ImportedModelsByAbsoluteFilePath.end())
	{
		return CachedImportedModel->second;
	}

	std::error_code FileExistenceCheckError;
	if (!std::filesystem::is_regular_file(AbsoluteModelFilePath, FileExistenceCheckError))
	{
		LOGE("Model file not found: {}", AbsoluteModelFilePathAsUtf8);
		return std::nullopt;
	}

	const std::string LowercaseExtension = getLowercaseFileExtension(AbsoluteModelFilePath);
	const auto ImportStartTime = std::chrono::steady_clock::now();

	std::optional<ImportedModel> ImportResult;
	try
	{
		if (LowercaseExtension == ".gltf" || LowercaseExtension == ".glb")
		{
			ImportResult = importGltfModel(AbsoluteModelFilePath);
		}
		else if (LowercaseExtension == ".obj" || LowercaseExtension == ".fbx")
		{
			ImportResult = importUfbxModel(AbsoluteModelFilePath);
		}
		else
		{
			LOGE("Unsupported model format '{}' for {}", LowercaseExtension, AbsoluteModelFilePathAsUtf8);
			return std::nullopt;
		}
	}
	catch (const std::exception& ImportException)
	{
		LOGE("Importing {} threw: {}", AbsoluteModelFilePathAsUtf8, ImportException.what());
		return std::nullopt;
	}

	if (ImportResult.has_value())
	{
		const double ImportDurationInMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ImportStartTime).count();
		LOG("Imported {} ({} submeshes) in {:.2f} ms", AbsoluteModelFilePath.filename().string(), ImportResult->m_SubmeshCount, ImportDurationInMilliseconds);
		m_ImportedModelsByAbsoluteFilePath.emplace(AbsoluteModelFilePathAsUtf8, ImportResult.value());
	}
	return ImportResult;
}

std::uint32_t ModelImporter::loadTextureFromFileOnce(const std::filesystem::path& AbsoluteTextureFilePath)
{
	const std::string AbsoluteTextureFilePathAsUtf8 = convertPathToUtf8String(AbsoluteTextureFilePath);
	if (const auto CachedTexture = m_TextureIdentifiersByAbsoluteFilePath.find(AbsoluteTextureFilePathAsUtf8); CachedTexture != m_TextureIdentifiersByAbsoluteFilePath.end())
	{
		return CachedTexture->second;
	}

	std::uint32_t LoadedTextureIdentifier{ 0 };
	std::error_code FileExistenceCheckError;
	if (!std::filesystem::is_regular_file(AbsoluteTextureFilePath, FileExistenceCheckError))
	{
		LOGW("Model texture not found: {} (using white instead)", AbsoluteTextureFilePathAsUtf8);
		LoadedTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
	}
	else
	{
		try
		{
			LoadedTextureIdentifier = static_cast<std::uint32_t>(m_Renderer.createTexture(AbsoluteTextureFilePath.string()));
		}
		catch (const std::exception&)
		{
			// createTexture already logged why
			LoadedTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
		}
	}

	m_TextureIdentifiersByAbsoluteFilePath.emplace(AbsoluteTextureFilePathAsUtf8, LoadedTextureIdentifier);
	return LoadedTextureIdentifier;
}

// =====================================================================================================================
// glTF / GLB (fastgltf)
// =====================================================================================================================
std::optional<ImportedModel> ModelImporter::importGltfModel(const std::filesystem::path& AbsoluteModelFilePath)
{
	auto GltfFileData = fastgltf::GltfDataBuffer::FromPath(AbsoluteModelFilePath);
	if (GltfFileData.error() != fastgltf::Error::None)
	{
		LOGE("Couldn't read glTF file {}: {}", AbsoluteModelFilePath.string(), fastgltf::getErrorMessage(GltfFileData.error()));
		return std::nullopt;
	}

	const std::filesystem::path ModelDirectory = AbsoluteModelFilePath.parent_path();

	// LoadExternalImages pulls image files into memory as well, so every image (external file, data URI, GLB chunk) ends up
	// as bytes we can decode the same way. GenerateMeshIndices means every primitive has an index buffer, even if the file didn't.
	constexpr auto c_GltfLoadOptions = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages | fastgltf::Options::GenerateMeshIndices;
	auto LoadedGltfAsset = m_GltfParser->loadGltf(GltfFileData.get(), ModelDirectory, c_GltfLoadOptions);
	if (LoadedGltfAsset.error() != fastgltf::Error::None)
	{
		LOGE("Couldn't parse glTF file {}: {}", AbsoluteModelFilePath.string(), fastgltf::getErrorMessage(LoadedGltfAsset.error()));
		return std::nullopt;
	}
	fastgltf::Asset& GltfAsset = LoadedGltfAsset.get();

	// 1. TEXTURES (decoded lazily, once per glTF image)
	std::vector<std::optional<std::uint32_t>> TextureIdentifiersByGltfImageIndex(GltfAsset.images.size());

	auto resolveTextureIdentifierOfGltfTexture = [&](size_t GltfTextureIndex) -> std::uint32_t
	{
		if (GltfTextureIndex >= GltfAsset.textures.size() || !GltfAsset.textures[GltfTextureIndex].imageIndex.has_value())
		{
			return m_Renderer.getDefaultWhiteTextureIdentifier();	// e.g. a KTX2-only texture, which we can't decode (yet)
		}

		const size_t GltfImageIndex = GltfAsset.textures[GltfTextureIndex].imageIndex.value();
		if (TextureIdentifiersByGltfImageIndex[GltfImageIndex].has_value())
		{
			return TextureIdentifiersByGltfImageIndex[GltfImageIndex].value();
		}

		auto createTextureFromEncodedBytes = [&](std::span<const std::byte> EncodedImageBytes) -> std::uint32_t
		{
			return static_cast<std::uint32_t>(m_Renderer.createTextureFromEncodedImageInMemory(reinterpret_cast<const unsigned char*>(EncodedImageBytes.data()), EncodedImageBytes.size()));
		};

		const std::uint32_t CreatedTextureIdentifier = std::visit(fastgltf::visitor{
			[&](const fastgltf::sources::BufferView& BufferViewSource) -> std::uint32_t
			{
				// The image lives inside a buffer (always the case for textures embedded in a .glb)
				const fastgltf::BufferView& ImageBufferView = GltfAsset.bufferViews[BufferViewSource.bufferViewIndex];
				const std::span<const std::byte> WholeBufferBytes = getBytesOfLoadedGltfDataSource(GltfAsset.buffers[ImageBufferView.bufferIndex].data);
				if (ImageBufferView.byteOffset + ImageBufferView.byteLength > WholeBufferBytes.size())
				{
					return m_Renderer.getDefaultWhiteTextureIdentifier();
				}
				return createTextureFromEncodedBytes(WholeBufferBytes.subspan(ImageBufferView.byteOffset, ImageBufferView.byteLength));
			},
			[&](const fastgltf::sources::URI& UriSource) -> std::uint32_t
			{
				// Only happens if fastgltf couldn't load the file itself. Try ourselves, relative to the model.
				return UriSource.uri->isLocalPath() ? loadTextureFromFileOnce(ModelDirectory / UriSource.uri->fspath()) : m_Renderer.getDefaultWhiteTextureIdentifier();
			},
			[&](const fastgltf::sources::Array& ArraySource) -> std::uint32_t { return createTextureFromEncodedBytes(std::span<const std::byte>(ArraySource.bytes.data(), ArraySource.bytes.size())); },
			[&](const fastgltf::sources::Vector& VectorSource) -> std::uint32_t { return createTextureFromEncodedBytes(std::span<const std::byte>(VectorSource.bytes.data(), VectorSource.bytes.size())); },
			[&](const fastgltf::sources::ByteView& ByteViewSource) -> std::uint32_t { return createTextureFromEncodedBytes(ByteViewSource.bytes); },
			[&](const auto&) -> std::uint32_t { return m_Renderer.getDefaultWhiteTextureIdentifier(); }
		}, GltfAsset.images[GltfImageIndex].data);

		TextureIdentifiersByGltfImageIndex[GltfImageIndex] = CreatedTextureIdentifier;
		return CreatedTextureIdentifier;
	};

	// 2. GEOMETRY (uploaded lazily, once per mesh primitive, no matter how many nodes use it)
	std::vector<std::vector<std::optional<UploadedSubmeshGeometry>>> UploadedGeometryByGltfMeshAndPrimitive(GltfAsset.meshes.size());
	std::vector<std::vector<bool>> HasTriedUploadingGltfMeshAndPrimitive(GltfAsset.meshes.size());
	for (size_t CurrentGltfMeshIndex{ 0 }; CurrentGltfMeshIndex < GltfAsset.meshes.size(); ++CurrentGltfMeshIndex)
	{
		UploadedGeometryByGltfMeshAndPrimitive[CurrentGltfMeshIndex].resize(GltfAsset.meshes[CurrentGltfMeshIndex].primitives.size());
		HasTriedUploadingGltfMeshAndPrimitive[CurrentGltfMeshIndex].resize(GltfAsset.meshes[CurrentGltfMeshIndex].primitives.size(), false);
	}

	auto uploadGltfPrimitive = [&](const fastgltf::Primitive& PrimitiveToUpload) -> std::optional<UploadedSubmeshGeometry>
	{
		if (PrimitiveToUpload.type != fastgltf::PrimitiveType::Triangles)
		{
			LOGW("Skipping a non-triangle glTF primitive (points/lines/strips aren't supported yet) in {}", AbsoluteModelFilePath.filename().string());
			return std::nullopt;
		}

		const auto PositionAttribute = PrimitiveToUpload.findAttribute("POSITION");
		if (PositionAttribute == PrimitiveToUpload.attributes.end() || !PrimitiveToUpload.indicesAccessor.has_value())
		{
			return std::nullopt;
		}

		// Material: base colour texture + base colour factor (the factor gets baked into the vertex colours, which the shader multiplies in)
		std::uint32_t BaseColourTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
		glm::vec4 BaseColourFactor{ 1.0f };
		if (PrimitiveToUpload.materialIndex.has_value() && PrimitiveToUpload.materialIndex.value() < GltfAsset.materials.size())
		{
			const fastgltf::Material& PrimitiveMaterial = GltfAsset.materials[PrimitiveToUpload.materialIndex.value()];
			const auto& GltfBaseColourFactor = PrimitiveMaterial.pbrData.baseColorFactor;
			BaseColourFactor = { GltfBaseColourFactor[0], GltfBaseColourFactor[1], GltfBaseColourFactor[2], GltfBaseColourFactor[3] };
			if (PrimitiveMaterial.pbrData.baseColorTexture.has_value())
			{
				BaseColourTextureIdentifier = resolveTextureIdentifierOfGltfTexture(PrimitiveMaterial.pbrData.baseColorTexture.value().textureIndex);
			}
		}

		UploadedSubmeshGeometry UploadedGeometry{};
		UploadedGeometry.m_TextureIdentifier = BaseColourTextureIdentifier;

		const fastgltf::Accessor& PositionAccessor = GltfAsset.accessors[PositionAttribute->accessorIndex];
		std::vector<Vulkanised::Vertex> PrimitiveVertices(PositionAccessor.count, Vulkanised::Vertex{ glm::vec3(0.0f), glm::vec3(BaseColourFactor), glm::vec2(0.0f), c_FallbackNormalPointingUp });

		fastgltf::iterateAccessorWithIndex<glm::vec3>(GltfAsset, PositionAccessor, [&](glm::vec3 VertexPosition, size_t VertexIndex)
		{
			PrimitiveVertices[VertexIndex].pos = VertexPosition;
			UploadedGeometry.m_LocalBounds.expandToIncludePoint(VertexPosition);
		});

		const auto NormalAttribute = PrimitiveToUpload.findAttribute("NORMAL");
		const bool HasNormals = NormalAttribute != PrimitiveToUpload.attributes.end();
		if (HasNormals)
		{
			fastgltf::iterateAccessorWithIndex<glm::vec3>(GltfAsset, GltfAsset.accessors[NormalAttribute->accessorIndex], [&](glm::vec3 VertexNormal, size_t VertexIndex)
			{
				PrimitiveVertices[VertexIndex].normal = VertexNormal;
			});
		}

		// glTF's UV origin is the top-left corner, same as Vulkan + stb_image, so no flipping needed here
		if (const auto TextureCoordinateAttribute = PrimitiveToUpload.findAttribute("TEXCOORD_0"); TextureCoordinateAttribute != PrimitiveToUpload.attributes.end())
		{
			fastgltf::iterateAccessorWithIndex<glm::vec2>(GltfAsset, GltfAsset.accessors[TextureCoordinateAttribute->accessorIndex], [&](glm::vec2 VertexTextureCoordinate, size_t VertexIndex)
			{
				PrimitiveVertices[VertexIndex].tex = VertexTextureCoordinate;
			});
		}

		// Vertex colours are optional, and can be RGB or RGBA (alpha is dropped, the pipeline is opaque for now)
		if (const auto ColourAttribute = PrimitiveToUpload.findAttribute("COLOR_0"); ColourAttribute != PrimitiveToUpload.attributes.end())
		{
			const fastgltf::Accessor& ColourAccessor = GltfAsset.accessors[ColourAttribute->accessorIndex];
			if (ColourAccessor.type == fastgltf::AccessorType::Vec4)
			{
				fastgltf::iterateAccessorWithIndex<glm::vec4>(GltfAsset, ColourAccessor, [&](glm::vec4 VertexColour, size_t VertexIndex)
				{
					PrimitiveVertices[VertexIndex].col = glm::vec3(VertexColour) * glm::vec3(BaseColourFactor);
				});
			}
			else if (ColourAccessor.type == fastgltf::AccessorType::Vec3)
			{
				fastgltf::iterateAccessorWithIndex<glm::vec3>(GltfAsset, ColourAccessor, [&](glm::vec3 VertexColour, size_t VertexIndex)
				{
					PrimitiveVertices[VertexIndex].col = VertexColour * glm::vec3(BaseColourFactor);
				});
			}
		}

		const fastgltf::Accessor& IndexAccessor = GltfAsset.accessors[PrimitiveToUpload.indicesAccessor.value()];
		std::vector<std::uint32_t> PrimitiveIndices(IndexAccessor.count);
		fastgltf::copyFromAccessor<std::uint32_t>(GltfAsset, IndexAccessor, PrimitiveIndices.data());	// Converts u8/u16 indices to u32 for us

		if (!HasNormals)
		{
			generateSmoothNormalsForTriangles(PrimitiveVertices, PrimitiveIndices);
		}

		const size_t MeshCountBeforeUpload = m_Renderer.getLoadedMeshCount();
		UploadedGeometry.m_MeshIdentifier = m_Renderer.loadMesh(PrimitiveVertices, PrimitiveIndices, static_cast<int>(BaseColourTextureIdentifier));
		if (m_Renderer.getLoadedMeshCount() == MeshCountBeforeUpload)
		{
			return std::nullopt;	// loadMesh already logged why (empty, or the geometry buffers are full)
		}
		return UploadedGeometry;
	};

	// 3. NODES -> SUBMESHES (the hierarchy gets flattened into each submesh's transform)
	std::vector<Vulkanised::ModelSubmesh> ModelSubmeshes;
	AxisAlignedBounds ModelBounds;

	auto addSubmeshesOfGltfMesh = [&](size_t GltfMeshIndex, const glm::mat4& MeshTransformRelativeToModelRoot)
	{
		const fastgltf::Mesh& GltfMesh = GltfAsset.meshes[GltfMeshIndex];
		for (size_t CurrentPrimitiveIndex{ 0 }; CurrentPrimitiveIndex < GltfMesh.primitives.size(); ++CurrentPrimitiveIndex)
		{
			if (!HasTriedUploadingGltfMeshAndPrimitive[GltfMeshIndex][CurrentPrimitiveIndex])
			{
				HasTriedUploadingGltfMeshAndPrimitive[GltfMeshIndex][CurrentPrimitiveIndex] = true;
				UploadedGeometryByGltfMeshAndPrimitive[GltfMeshIndex][CurrentPrimitiveIndex] = uploadGltfPrimitive(GltfMesh.primitives[CurrentPrimitiveIndex]);
			}

			const std::optional<UploadedSubmeshGeometry>& UploadedGeometry = UploadedGeometryByGltfMeshAndPrimitive[GltfMeshIndex][CurrentPrimitiveIndex];
			if (!UploadedGeometry.has_value())
			{
				continue;
			}

			ModelSubmeshes.push_back({ UploadedGeometry->m_MeshIdentifier, UploadedGeometry->m_TextureIdentifier, MeshTransformRelativeToModelRoot });
			ModelBounds.expandToIncludeTransformedBounds(UploadedGeometry->m_LocalBounds, MeshTransformRelativeToModelRoot);
		}
	};

	if (!GltfAsset.scenes.empty())
	{
		const size_t SceneIndexToImport = std::min(GltfAsset.defaultScene.value_or(0), GltfAsset.scenes.size() - 1);
		fastgltf::iterateSceneNodes(GltfAsset, SceneIndexToImport, fastgltf::math::nmat4x4(), [&](fastgltf::Node& CurrentNode, const fastgltf::math::nmat4x4& NodeTransformRelativeToModelRoot)
		{
			if (CurrentNode.meshIndex.has_value())
			{
				addSubmeshesOfGltfMesh(CurrentNode.meshIndex.value(), convertGltfMatrixToGlm(NodeTransformRelativeToModelRoot));
			}
		});
	}
	else
	{
		// Scenes are optional in glTF. Without one, the best we can do is show every mesh at the origin.
		for (size_t CurrentGltfMeshIndex{ 0 }; CurrentGltfMeshIndex < GltfAsset.meshes.size(); ++CurrentGltfMeshIndex)
		{
			addSubmeshesOfGltfMesh(CurrentGltfMeshIndex, glm::mat4(1.0f));
		}
	}

	if (ModelSubmeshes.empty())
	{
		LOGW("glTF file {} had nothing renderable in it.", AbsoluteModelFilePath.string());
		return std::nullopt;
	}

	ImportedModel Result{};
	Result.m_ModelIdentifier = m_Renderer.createModel(ModelSubmeshes);
	Result.m_BoundsMinimumCorner = ModelBounds.m_MinimumCorner;
	Result.m_BoundsMaximumCorner = ModelBounds.m_MaximumCorner;
	Result.m_SubmeshCount = static_cast<std::uint32_t>(ModelSubmeshes.size());
	return Result;
}

// =====================================================================================================================
// OBJ / FBX (ufbx)
// =====================================================================================================================
std::optional<ImportedModel> ModelImporter::importUfbxModel(const std::filesystem::path& AbsoluteModelFilePath)
{
	ufbx_load_opts UfbxLoadOptions{};
	UfbxLoadOptions.target_axes = ufbx_axes_right_handed_y_up;	// Same as the engine (and glTF)
	UfbxLoadOptions.target_unit_meters = 1.0f;					// FBX is usually in centimetres, this makes it match glTF's metres
	// NOT generate_missing_normals: OBJs without smoothing groups count as flat shaded, so ufbx would give every triangle
	// corner its own face normal. Nothing could be merged afterwards, tripling the vertex count (a 1M vertex scan became 2.8M).
	// We build smooth normals ourselves instead, after the corners have been merged back into shared vertices.
	UfbxLoadOptions.generate_missing_normals = false;
	UfbxLoadOptions.load_external_files = true;					// .mtl files for OBJ
	UfbxLoadOptions.ignore_missing_external_files = true;		// A missing .mtl shouldn't lose us the whole model
	UfbxLoadOptions.obj_search_mtl_by_filename = true;

	const std::string AbsoluteModelFilePathAsUtf8 = convertPathToUtf8String(AbsoluteModelFilePath);
	ufbx_error UfbxLoadError{};
	const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> UfbxScene(
		ufbx_load_file_len(AbsoluteModelFilePathAsUtf8.data(), AbsoluteModelFilePathAsUtf8.size(), &UfbxLoadOptions, &UfbxLoadError),
		&ufbx_free_scene);

	if (!UfbxScene)
	{
		std::array<char, 1024> FormattedUfbxError{};
		ufbx_format_error(FormattedUfbxError.data(), FormattedUfbxError.size(), &UfbxLoadError);
		LOGE("Couldn't load {}: {}", AbsoluteModelFilePathAsUtf8, FormattedUfbxError.data());
		return std::nullopt;
	}

	const std::filesystem::path ModelDirectory = AbsoluteModelFilePath.parent_path();

	// 1. TEXTURES (once per ufbx texture)
	std::unordered_map<const ufbx_texture*, std::uint32_t> TextureIdentifiersByUfbxTexture;

	auto resolveTextureIdentifierOfUfbxTexture = [&](const ufbx_texture* UfbxTexture) -> std::uint32_t
	{
		if (const auto CachedTexture = TextureIdentifiersByUfbxTexture.find(UfbxTexture); CachedTexture != TextureIdentifiersByUfbxTexture.end())
		{
			return CachedTexture->second;
		}

		std::uint32_t ResolvedTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
		if (UfbxTexture->content.size > 0)
		{
			// Embedded in the FBX itself
			ResolvedTextureIdentifier = static_cast<std::uint32_t>(m_Renderer.createTextureFromEncodedImageInMemory(static_cast<const unsigned char*>(UfbxTexture->content.data), UfbxTexture->content.size));
		}
		else
		{
			// Files written on another machine often have an absolute path that doesn't exist here, so try a few places
			const std::array<std::filesystem::path, 3> CandidateTextureFilePaths{
				convertUfbxStringToPath(UfbxTexture->absolute_filename),
				ModelDirectory / convertUfbxStringToPath(UfbxTexture->relative_filename),
				ModelDirectory / convertUfbxStringToPath(UfbxTexture->filename).filename()
			};

			std::filesystem::path TextureFilePathToLoad = CandidateTextureFilePaths[1];
			for (const std::filesystem::path& CurrentCandidateTextureFilePath : CandidateTextureFilePaths)
			{
				std::error_code FileExistenceCheckError;
				if (!CurrentCandidateTextureFilePath.empty() && std::filesystem::is_regular_file(CurrentCandidateTextureFilePath, FileExistenceCheckError))
				{
					TextureFilePathToLoad = CurrentCandidateTextureFilePath;
					break;
				}
			}
			ResolvedTextureIdentifier = loadTextureFromFileOnce(TextureFilePathToLoad);
		}

		TextureIdentifiersByUfbxTexture.emplace(UfbxTexture, ResolvedTextureIdentifier);
		return ResolvedTextureIdentifier;
	};

	// 2. GEOMETRY (once per mesh part + material combination)
	std::map<std::tuple<const ufbx_mesh*, std::uint32_t, const ufbx_material*>, std::optional<UploadedSubmeshGeometry>> UploadedGeometryByMeshPartAndMaterial;

	auto uploadUfbxMeshPart = [&](const ufbx_mesh* UfbxMesh, const ufbx_mesh_part& MeshPart, const ufbx_material* PartMaterial) -> std::optional<UploadedSubmeshGeometry>
	{
		if (MeshPart.num_triangles == 0)
		{
			return std::nullopt;
		}

		UploadedSubmeshGeometry UploadedGeometry{};
		UploadedGeometry.m_TextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
		glm::vec3 MaterialColourFactor{ 1.0f };

		if (PartMaterial != nullptr)
		{
			const ufbx_texture* BaseColourTexture = PartMaterial->pbr.base_color.texture != nullptr ? PartMaterial->pbr.base_color.texture : PartMaterial->fbx.diffuse_color.texture;
			if (BaseColourTexture != nullptr)
			{
				UploadedGeometry.m_TextureIdentifier = resolveTextureIdentifierOfUfbxTexture(BaseColourTexture);
			}
			else if (PartMaterial->pbr.base_color.has_value)
			{
				// Only used without a texture: OBJ/FBX exporters love writing a grey-ish diffuse colour next to a texture,
				// which would darken it if we multiplied them (glTF is stricter, there the factor is always applied)
				const ufbx_vec4& UfbxBaseColour = PartMaterial->pbr.base_color.value_vec4;
				MaterialColourFactor = { static_cast<float>(UfbxBaseColour.x), static_cast<float>(UfbxBaseColour.y), static_cast<float>(UfbxBaseColour.z) };
			}
		}

		// Triangulate every face of this part into a flat list of corners
		std::vector<std::uint32_t> TriangulatedCornerIndices(UfbxMesh->max_face_triangles * 3);
		std::vector<Vulkanised::Vertex> PartVertices;
		PartVertices.reserve(MeshPart.num_triangles * 3);

		for (size_t CurrentPartFaceIndex{ 0 }; CurrentPartFaceIndex < MeshPart.face_indices.count; ++CurrentPartFaceIndex)
		{
			const ufbx_face CurrentFace = UfbxMesh->faces.data[MeshPart.face_indices.data[CurrentPartFaceIndex]];
			const std::uint32_t CurrentFaceTriangleCount = ufbx_triangulate_face(TriangulatedCornerIndices.data(), TriangulatedCornerIndices.size(), UfbxMesh, CurrentFace);

			for (std::uint32_t CurrentCornerIndex{ 0 }; CurrentCornerIndex < CurrentFaceTriangleCount * 3; ++CurrentCornerIndex)
			{
				const std::uint32_t MeshIndex = TriangulatedCornerIndices[CurrentCornerIndex];

				Vulkanised::Vertex CornerVertex{ convertUfbxVectorToGlm(ufbx_get_vertex_vec3(&UfbxMesh->vertex_position, MeshIndex)), MaterialColourFactor, glm::vec2(0.0f), c_FallbackNormalPointingUp };
				if (UfbxMesh->vertex_normal.exists)
				{
					CornerVertex.normal = convertUfbxVectorToGlm(ufbx_get_vertex_vec3(&UfbxMesh->vertex_normal, MeshIndex));
				}
				if (UfbxMesh->vertex_uv.exists)
				{
					// OBJ/FBX put the UV origin at the bottom-left, Vulkan + stb_image at the top-left
					const ufbx_vec2 UfbxTextureCoordinate = ufbx_get_vertex_vec2(&UfbxMesh->vertex_uv, MeshIndex);
					CornerVertex.tex = { static_cast<float>(UfbxTextureCoordinate.x), 1.0f - static_cast<float>(UfbxTextureCoordinate.y) };
				}
				if (UfbxMesh->vertex_color.exists)
				{
					const ufbx_vec4 UfbxVertexColour = ufbx_get_vertex_vec4(&UfbxMesh->vertex_color, MeshIndex);
					CornerVertex.col *= glm::vec3(static_cast<float>(UfbxVertexColour.x), static_cast<float>(UfbxVertexColour.y), static_cast<float>(UfbxVertexColour.z));
				}

				UploadedGeometry.m_LocalBounds.expandToIncludePoint(CornerVertex.pos);
				PartVertices.push_back(CornerVertex);
			}
		}

		// Merge identical corners back into shared vertices (ufbx does this in place, and fast)
		std::vector<std::uint32_t> PartIndices(PartVertices.size());
		ufbx_vertex_stream PartVertexStream{ PartVertices.data(), PartVertices.size(), sizeof(Vulkanised::Vertex) };
		ufbx_error IndexGenerationError{};
		const size_t UniqueVertexCount = ufbx_generate_indices(&PartVertexStream, 1, PartIndices.data(), PartIndices.size(), nullptr, &IndexGenerationError);
		if (IndexGenerationError.type == UFBX_ERROR_NONE)
		{
			PartVertices.resize(UniqueVertexCount);
		}
		else
		{
			// Fall back to one vertex per corner, bigger but still correct
			for (size_t CurrentIndex{ 0 }; CurrentIndex < PartIndices.size(); ++CurrentIndex)
			{
				PartIndices[CurrentIndex] = static_cast<std::uint32_t>(CurrentIndex);
			}
		}

		// Every corner got the same placeholder normal above, so they merged purely on position/UV/colour. Now smooth them.
		if (!UfbxMesh->vertex_normal.exists)
		{
			generateSmoothNormalsForTriangles(PartVertices, PartIndices);
		}

		const size_t MeshCountBeforeUpload = m_Renderer.getLoadedMeshCount();
		UploadedGeometry.m_MeshIdentifier = m_Renderer.loadMesh(PartVertices, PartIndices, static_cast<int>(UploadedGeometry.m_TextureIdentifier));
		if (m_Renderer.getLoadedMeshCount() == MeshCountBeforeUpload)
		{
			return std::nullopt;	// loadMesh already logged why (empty, or the geometry buffers are full)
		}
		return UploadedGeometry;
	};

	// 3. NODES -> SUBMESHES
	std::vector<Vulkanised::ModelSubmesh> ModelSubmeshes;
	AxisAlignedBounds ModelBounds;

	for (size_t CurrentNodeIndex{ 0 }; CurrentNodeIndex < UfbxScene->nodes.count; ++CurrentNodeIndex)
	{
		const ufbx_node* CurrentNode = UfbxScene->nodes.data[CurrentNodeIndex];
		const ufbx_mesh* NodeMesh = CurrentNode->mesh;
		if (NodeMesh == nullptr || !CurrentNode->visible)
		{
			continue;
		}

		// geometry_to_world includes FBX's "geometric transforms" and the axis/unit conversion we asked for
		const glm::mat4 NodeTransformRelativeToModelRoot = convertUfbxMatrixToGlm(CurrentNode->geometry_to_world);

		for (size_t CurrentPartIndex{ 0 }; CurrentPartIndex < NodeMesh->material_parts.count; ++CurrentPartIndex)
		{
			const ufbx_mesh_part& CurrentMeshPart = NodeMesh->material_parts.data[CurrentPartIndex];

			// The same mesh can be placed on several nodes with different materials, so the node's list wins
			const ufbx_material* PartMaterial = nullptr;
			if (CurrentMeshPart.index < CurrentNode->materials.count)
			{
				PartMaterial = CurrentNode->materials.data[CurrentMeshPart.index];
			}
			else if (CurrentMeshPart.index < NodeMesh->materials.count)
			{
				PartMaterial = NodeMesh->materials.data[CurrentMeshPart.index];
			}

			const auto UploadKey = std::make_tuple(NodeMesh, CurrentMeshPart.index, PartMaterial);
			auto UploadedGeometryEntry = UploadedGeometryByMeshPartAndMaterial.find(UploadKey);
			if (UploadedGeometryEntry == UploadedGeometryByMeshPartAndMaterial.end())
			{
				UploadedGeometryEntry = UploadedGeometryByMeshPartAndMaterial.emplace(UploadKey, uploadUfbxMeshPart(NodeMesh, CurrentMeshPart, PartMaterial)).first;
			}

			const std::optional<UploadedSubmeshGeometry>& UploadedGeometry = UploadedGeometryEntry->second;
			if (!UploadedGeometry.has_value())
			{
				continue;
			}

			ModelSubmeshes.push_back({ UploadedGeometry->m_MeshIdentifier, UploadedGeometry->m_TextureIdentifier, NodeTransformRelativeToModelRoot });
			ModelBounds.expandToIncludeTransformedBounds(UploadedGeometry->m_LocalBounds, NodeTransformRelativeToModelRoot);
		}
	}

	if (ModelSubmeshes.empty())
	{
		LOGW("{} had nothing renderable in it.", AbsoluteModelFilePathAsUtf8);
		return std::nullopt;
	}

	ImportedModel Result{};
	Result.m_ModelIdentifier = m_Renderer.createModel(ModelSubmeshes);
	Result.m_BoundsMinimumCorner = ModelBounds.m_MinimumCorner;
	Result.m_BoundsMaximumCorner = ModelBounds.m_MaximumCorner;
	Result.m_SubmeshCount = static_cast<std::uint32_t>(ModelSubmeshes.size());
	return Result;
}

}
