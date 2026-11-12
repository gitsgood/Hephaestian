#include "Hephaestian/Assets/AssetLibrary.h"

#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>

#include <Vulkanised/Logger.h>
#include <Vulkanised/utils/FileUtils.h>

namespace Hephaestian::Assets
{

namespace
{
	constexpr std::array<std::string_view, static_cast<std::size_t>(PrimitiveShape::Count)> c_PrimitiveShapeNames{ "quad", "cube", "sphere" };

	[[nodiscard]] std::filesystem::path convertToAbsoluteAssetPath(const std::filesystem::path& AssetRelativePath)
	{
		const std::filesystem::path AbsoluteAssetPath = AssetRelativePath.is_absolute() ? AssetRelativePath : Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / AssetRelativePath;
		std::error_code CanonicalisationError;
		const std::filesystem::path CanonicalAssetPath = std::filesystem::weakly_canonical(AbsoluteAssetPath, CanonicalisationError);
		return CanonicalisationError ? AbsoluteAssetPath : CanonicalAssetPath;
	}

	// The pipeline hides back faces, so every triangle has to wind counter-clockwise seen from outside. Rather than trusting
	// the hand-written corner orders, each triangle is checked against the direction it should face and flipped if needed.
	void makeTrianglesFaceOutwards(const std::vector<Vulkanised::Vertex>& Vertices, std::vector<std::uint32_t>& TriangleIndices)
	{
		for (std::size_t CurrentTriangleStart{ 0 }; CurrentTriangleStart + 2 < TriangleIndices.size(); CurrentTriangleStart += 3)
		{
			const Vulkanised::Vertex& FirstCorner = Vertices[TriangleIndices[CurrentTriangleStart]];
			const Vulkanised::Vertex& SecondCorner = Vertices[TriangleIndices[CurrentTriangleStart + 1]];
			const Vulkanised::Vertex& ThirdCorner = Vertices[TriangleIndices[CurrentTriangleStart + 2]];
			const glm::vec3 WindingNormal = glm::cross(SecondCorner.pos - FirstCorner.pos, ThirdCorner.pos - FirstCorner.pos);
			const glm::vec3 IntendedFacingDirection = FirstCorner.normal + SecondCorner.normal + ThirdCorner.normal;
			if (glm::dot(WindingNormal, IntendedFacingDirection) < 0.0f)
			{
				std::swap(TriangleIndices[CurrentTriangleStart + 1], TriangleIndices[CurrentTriangleStart + 2]);
			}
		}
	}

	// One flat 1 x 1 face: its centre, the direction it faces, and the two directions across it (texture right and up)
	void appendSquareFace(std::vector<Vulkanised::Vertex>& Vertices, std::vector<std::uint32_t>& TriangleIndices,
		const glm::vec3& FaceCentre, const glm::vec3& FaceNormal, const glm::vec3& FaceRightDirection, const glm::vec3& FaceUpDirection)
	{
		const std::uint32_t FirstVertexIndex = static_cast<std::uint32_t>(Vertices.size());
		const glm::vec3 HalfRight = FaceRightDirection * 0.5f;
		const glm::vec3 HalfUp = FaceUpDirection * 0.5f;
		constexpr glm::vec3 c_White{ 1.0f };
		// Texture coordinates start at the TOP-left in Vulkan, so the face's top edge gets v = 0
		Vertices.push_back({ FaceCentre - HalfRight - HalfUp, c_White, { 0.0f, 1.0f }, FaceNormal });
		Vertices.push_back({ FaceCentre + HalfRight - HalfUp, c_White, { 1.0f, 1.0f }, FaceNormal });
		Vertices.push_back({ FaceCentre + HalfRight + HalfUp, c_White, { 1.0f, 0.0f }, FaceNormal });
		Vertices.push_back({ FaceCentre - HalfRight + HalfUp, c_White, { 0.0f, 0.0f }, FaceNormal });
		TriangleIndices.insert(TriangleIndices.end(), { FirstVertexIndex, FirstVertexIndex + 1, FirstVertexIndex + 2, FirstVertexIndex, FirstVertexIndex + 2, FirstVertexIndex + 3 });
	}

	void buildPrimitiveGeometry(PrimitiveShape Shape, std::vector<Vulkanised::Vertex>& OutVertices, std::vector<std::uint32_t>& OutTriangleIndices)
	{
		constexpr glm::vec3 c_PositiveX{ 1.0f, 0.0f, 0.0f };
		constexpr glm::vec3 c_PositiveY{ 0.0f, 1.0f, 0.0f };
		constexpr glm::vec3 c_PositiveZ{ 0.0f, 0.0f, 1.0f };

		switch (Shape)
		{
		case PrimitiveShape::Quad:
			appendSquareFace(OutVertices, OutTriangleIndices, glm::vec3(0.0f), c_PositiveY, c_PositiveX, -c_PositiveZ);
			break;

		case PrimitiveShape::Cube:
			// Each face gets its own 4 vertices, so every face has flat normals and its own full texture
			appendSquareFace(OutVertices, OutTriangleIndices, c_PositiveX * 0.5f, c_PositiveX, -c_PositiveZ, c_PositiveY);
			appendSquareFace(OutVertices, OutTriangleIndices, -c_PositiveX * 0.5f, -c_PositiveX, c_PositiveZ, c_PositiveY);
			appendSquareFace(OutVertices, OutTriangleIndices, c_PositiveZ * 0.5f, c_PositiveZ, c_PositiveX, c_PositiveY);
			appendSquareFace(OutVertices, OutTriangleIndices, -c_PositiveZ * 0.5f, -c_PositiveZ, -c_PositiveX, c_PositiveY);
			appendSquareFace(OutVertices, OutTriangleIndices, c_PositiveY * 0.5f, c_PositiveY, c_PositiveX, -c_PositiveZ);
			appendSquareFace(OutVertices, OutTriangleIndices, -c_PositiveY * 0.5f, -c_PositiveY, c_PositiveX, c_PositiveZ);
			break;

		case PrimitiveShape::Sphere:
		{
			// Latitude/longitude sphere. The texture wraps around once; the seam column is duplicated so it can go from u = 1 back to 0.
			constexpr int c_RingCount{ 24 };
			constexpr int c_SegmentCount{ 48 };
			constexpr float c_SphereRadius{ 0.5f };
			for (int CurrentRingIndex{ 0 }; CurrentRingIndex <= c_RingCount; ++CurrentRingIndex)
			{
				const float PolarAngle = glm::pi<float>() * static_cast<float>(CurrentRingIndex) / static_cast<float>(c_RingCount);
				for (int CurrentSegmentIndex{ 0 }; CurrentSegmentIndex <= c_SegmentCount; ++CurrentSegmentIndex)
				{
					const float AzimuthAngle = glm::two_pi<float>() * static_cast<float>(CurrentSegmentIndex) / static_cast<float>(c_SegmentCount);
					const glm::vec3 DirectionFromCentre{ std::sin(PolarAngle) * std::cos(AzimuthAngle), std::cos(PolarAngle), std::sin(PolarAngle) * std::sin(AzimuthAngle) };
					const glm::vec2 TextureCoordinate{ static_cast<float>(CurrentSegmentIndex) / static_cast<float>(c_SegmentCount), static_cast<float>(CurrentRingIndex) / static_cast<float>(c_RingCount) };
					OutVertices.push_back({ DirectionFromCentre * c_SphereRadius, glm::vec3(1.0f), TextureCoordinate, DirectionFromCentre });
				}
			}
			const std::uint32_t VerticesPerRing = c_SegmentCount + 1;
			for (std::uint32_t CurrentRingIndex{ 0 }; CurrentRingIndex < c_RingCount; ++CurrentRingIndex)
			{
				for (std::uint32_t CurrentSegmentIndex{ 0 }; CurrentSegmentIndex < c_SegmentCount; ++CurrentSegmentIndex)
				{
					const std::uint32_t UpperLeftVertex = CurrentRingIndex * VerticesPerRing + CurrentSegmentIndex;
					const std::uint32_t LowerLeftVertex = UpperLeftVertex + VerticesPerRing;
					OutTriangleIndices.insert(OutTriangleIndices.end(), { UpperLeftVertex, LowerLeftVertex, LowerLeftVertex + 1, UpperLeftVertex, LowerLeftVertex + 1, UpperLeftVertex + 1 });
				}
			}
			break;
		}

		case PrimitiveShape::Count:
			break;
		}

		makeTrianglesFaceOutwards(OutVertices, OutTriangleIndices);
	}
}

std::optional<PrimitiveShape> parsePrimitiveShapeName(std::string_view PrimitiveShapeName)
{
	for (std::size_t CurrentShapeIndex{ 0 }; CurrentShapeIndex < c_PrimitiveShapeNames.size(); ++CurrentShapeIndex)
	{
		if (c_PrimitiveShapeNames[CurrentShapeIndex] == PrimitiveShapeName)
		{
			return static_cast<PrimitiveShape>(CurrentShapeIndex);
		}
	}
	return std::nullopt;
}

std::string_view getPrimitiveShapeName(PrimitiveShape Shape)
{
	const std::size_t ShapeIndex = static_cast<std::size_t>(Shape);
	return ShapeIndex < c_PrimitiveShapeNames.size() ? c_PrimitiveShapeNames[ShapeIndex] : std::string_view{ "unknown" };
}

AssetLibrary::AssetLibrary(Vulkanised::VulkanRenderer& RendererToUploadInto)
	: m_Renderer(RendererToUploadInto)
	, m_ModelImporter(RendererToUploadInto)
{
}

std::optional<ImportedModel> AssetLibrary::getOrLoadModel(const std::filesystem::path& ModelRelativePath)
{
	return m_ModelImporter.importModelFromFile(ModelRelativePath);	// Resolves the path against assets/ and caches by path itself
}

std::optional<std::uint32_t> AssetLibrary::getOrLoadTexture(const std::filesystem::path& TextureRelativePath)
{
	const std::filesystem::path AbsoluteTexturePath = convertToAbsoluteAssetPath(TextureRelativePath);
	const std::string AbsoluteTexturePathAsText = AbsoluteTexturePath.generic_string();
	if (const auto CachedTexture = m_TextureIdentifiersByAbsolutePath.find(AbsoluteTexturePathAsText); CachedTexture != m_TextureIdentifiersByAbsolutePath.end())
	{
		return CachedTexture->second;
	}

	std::error_code FileExistenceCheckError;
	if (!std::filesystem::is_regular_file(AbsoluteTexturePath, FileExistenceCheckError))
	{
		LOGE("Texture not found: {}", AbsoluteTexturePathAsText);
		return std::nullopt;
	}

	try
	{
		const std::uint32_t LoadedTextureIdentifier = static_cast<std::uint32_t>(m_Renderer.createTexture(AbsoluteTexturePath.string()));
		m_TextureIdentifiersByAbsolutePath.emplace(AbsoluteTexturePathAsText, LoadedTextureIdentifier);
		return LoadedTextureIdentifier;
	}
	catch (const std::exception&)
	{
		return std::nullopt;	// createTexture already logged why
	}
}

std::uint32_t AssetLibrary::getOrCreatePrimitiveModel(PrimitiveShape Shape)
{
	std::optional<std::uint32_t>& CachedPrimitiveModel = m_PrimitiveModelIdentifiers[static_cast<std::size_t>(Shape)];
	if (!CachedPrimitiveModel.has_value())
	{
		std::vector<Vulkanised::Vertex> PrimitiveVertices;
		std::vector<std::uint32_t> PrimitiveTriangleIndices;
		buildPrimitiveGeometry(Shape, PrimitiveVertices, PrimitiveTriangleIndices);

		const std::uint32_t WhiteTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
		const std::uint32_t PrimitiveMeshIdentifier = m_Renderer.loadMesh(PrimitiveVertices, PrimitiveTriangleIndices, static_cast<int>(WhiteTextureIdentifier));
		CachedPrimitiveModel = m_Renderer.createModelFromSingleMesh(PrimitiveMeshIdentifier, WhiteTextureIdentifier);
	}
	return CachedPrimitiveModel.value();
}

}
