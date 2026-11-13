#include "Hephaestian/Assets/AssetLibrary.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <stb_image.h>

#include <Vulkanised/Logger.h>
#include <Vulkanised/utils/FileUtils.h>

namespace Hephaestian::Assets
{

namespace
{
	constexpr std::array<std::string_view, static_cast<std::size_t>(PrimitiveShape::Count)> c_PrimitiveShapeNames{ "quad", "cube", "sphere", "sprite" };

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

		case PrimitiveShape::Sprite:
			// Texture right = +x, texture up = +y, so frame (0, 0) is its top-left corner like in the image. Pivots and flips
			// are applied per instance, billboarding in the vertex shader.
			appendSquareFace(OutVertices, OutTriangleIndices, glm::vec3(0.0f), c_PositiveZ, c_PositiveX, c_PositiveY);
			break;

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

AssetLibrary::AssetLibrary(Vulkanised::VulkanRenderer& RendererToUploadInto, Audio::AudioEngine& AudioEngineToLoadInto)
	: m_Renderer(RendererToUploadInto)
	, m_AudioEngine(AudioEngineToLoadInto)
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

std::shared_ptr<const SpriteSheet> AssetLibrary::getOrLoadSpriteSheet(const std::filesystem::path& SpriteSheetRelativePath)
{
	const std::filesystem::path AbsoluteSpriteSheetPath = convertToAbsoluteAssetPath(SpriteSheetRelativePath);
	const std::string AbsoluteSpriteSheetPathAsText = AbsoluteSpriteSheetPath.generic_string();
	if (const auto CachedSpriteSheet = m_SpriteSheetsByAbsolutePath.find(AbsoluteSpriteSheetPathAsText); CachedSpriteSheet != m_SpriteSheetsByAbsolutePath.end())
	{
		return CachedSpriteSheet->second;
	}

	std::optional<SpriteSheetDescription> Description = readSpriteSheetFile(AbsoluteSpriteSheetPath);	// Logs its own problems
	if (!Description.has_value())
	{
		return nullptr;
	}

	// Its own texture loads rather than getOrLoadTexture: sprite sheets usually want different options (pixelated, no
	// mipmaps), and may need a transparent colour applied to the pixels first.
	// Long animations can have dozens of big pages: they're decoded several at once on worker threads, then uploaded one
	// batch at a time (waiting for each), so the staging memory never holds more than one batch.
	const auto LoadStartTime = std::chrono::steady_clock::now();
	auto LoadedSpriteSheet = std::make_shared<SpriteSheet>(std::move(Description->m_SpriteSheet));
	const std::vector<std::filesystem::path>& PageImageFilePaths = Description->m_PageImageFilePaths;
	const std::size_t PagesPerBatch = std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
	struct DecodedPage { unsigned char* m_RgbaPixels{ nullptr }; int m_WidthInPixels{ 0 }; int m_HeightInPixels{ 0 }; std::string m_FailureReason; };
	for (std::size_t BatchFirstPageIndex{ 0 }; BatchFirstPageIndex < PageImageFilePaths.size(); BatchFirstPageIndex += PagesPerBatch)
	{
		const std::size_t BatchEndPageIndex = std::min(BatchFirstPageIndex + PagesPerBatch, PageImageFilePaths.size());
		std::vector<std::future<DecodedPage>> PagesBeingDecoded;
		for (std::size_t CurrentPageIndex = BatchFirstPageIndex; CurrentPageIndex < BatchEndPageIndex; ++CurrentPageIndex)
		{
			PagesBeingDecoded.push_back(std::async(std::launch::async, [&Description, &PageImageFilePaths, CurrentPageIndex]()
			{
				DecodedPage Page{};
				Page.m_RgbaPixels = decodeSpriteSheetPage(Description.value(), PageImageFilePaths[CurrentPageIndex], Page.m_WidthInPixels, Page.m_HeightInPixels);
				if (Page.m_RgbaPixels == nullptr)
				{
					Page.m_FailureReason = stbi_failure_reason();	// stb_image keeps it per thread: read it on this one
				}
				return Page;
			}));
		}

		// Every future is collected (and its pixels freed) even after a failure, so no worker is left running
		bool HasBatchFailed{ false };
		for (std::size_t CurrentPageIndex = BatchFirstPageIndex; CurrentPageIndex < BatchEndPageIndex; ++CurrentPageIndex)
		{
			const DecodedPage Page = PagesBeingDecoded[CurrentPageIndex - BatchFirstPageIndex].get();
			if (Page.m_RgbaPixels == nullptr)
			{
				LOGE("Sprite sheet {}: its image {} couldn't be loaded ({}).", AbsoluteSpriteSheetPathAsText, PageImageFilePaths[CurrentPageIndex].string(), Page.m_FailureReason);
				HasBatchFailed = true;
				continue;
			}
			if (!HasBatchFailed)
			{
				LoadedSpriteSheet->m_PageTextureIdentifiers.push_back(static_cast<std::uint32_t>(m_Renderer.createTextureFromRgbaPixels(Page.m_RgbaPixels,
					static_cast<std::uint32_t>(Page.m_WidthInPixels), static_cast<std::uint32_t>(Page.m_HeightInPixels), Description->m_TextureOptions)));
			}
			stbi_image_free(Page.m_RgbaPixels);
		}
		if (HasBatchFailed)
		{
			for (const std::uint32_t UploadedPageTextureIdentifier : LoadedSpriteSheet->m_PageTextureIdentifiers)
			{
				m_Renderer.destroyTexture(UploadedPageTextureIdentifier);	// The pages that did load are of no use alone
			}
			return nullptr;
		}
		if (PageImageFilePaths.size() > 1)
		{
			m_Renderer.flushPendingUploads();
		}
	}

	// Every sprite draws the same standing quad: its mesh, straight from the primitive model
	const std::uint32_t SpriteQuadModelIdentifier = getOrCreatePrimitiveModel(PrimitiveShape::Sprite);
	LoadedSpriteSheet->m_QuadMeshIdentifier = m_Renderer.getModelAsset(SpriteQuadModelIdentifier).m_Submeshes.front().m_MeshIdentifier;

	const double LoadTimeInMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - LoadStartTime).count();
	LOG("Loaded sprite sheet {}: {} frames on {} page(s), {} clips ({:.0f} ms).", AbsoluteSpriteSheetPath.filename().string(), LoadedSpriteSheet->m_Frames.size(),
		LoadedSpriteSheet->m_PageTextureIdentifiers.size(), LoadedSpriteSheet->m_Clips.size(), LoadTimeInMilliseconds);
	m_SpriteSheetsByAbsolutePath.emplace(AbsoluteSpriteSheetPathAsText, LoadedSpriteSheet);
	return LoadedSpriteSheet;
}

void AssetLibrary::releaseAssetsUnusedByScene(std::span<const std::uint32_t> TextureIdentifiersStillInUse)
{
	// Sprite sheets: still held by something (sprites, the scene being loaded, game code) = still used
	std::size_t ReleasedSpriteSheetCount{ 0 };
	std::size_t ReleasedPageCount{ 0 };
	std::erase_if(m_SpriteSheetsByAbsolutePath, [this, &ReleasedSpriteSheetCount, &ReleasedPageCount](const auto& CachedSpriteSheetEntry)
	{
		const std::shared_ptr<const SpriteSheet>& CachedSpriteSheet = CachedSpriteSheetEntry.second;
		if (CachedSpriteSheet.use_count() > 1)
		{
			return false;
		}
		for (const std::uint32_t PageTextureIdentifier : CachedSpriteSheet->m_PageTextureIdentifiers)
		{
			m_Renderer.destroyTexture(PageTextureIdentifier);
		}
		++ReleasedSpriteSheetCount;
		ReleasedPageCount += CachedSpriteSheet->m_PageTextureIdentifiers.size();
		return true;
	});

	// Standalone textures: only the scene knows which ones it uses (as texture overrides, through its asset table)
	std::size_t ReleasedTextureCount{ 0 };
	std::erase_if(m_TextureIdentifiersByAbsolutePath, [this, TextureIdentifiersStillInUse, &ReleasedTextureCount](const auto& CachedTextureEntry)
	{
		if (std::ranges::find(TextureIdentifiersStillInUse, CachedTextureEntry.second) != TextureIdentifiersStillInUse.end())
		{
			return false;
		}
		m_Renderer.destroyTexture(CachedTextureEntry.second);
		++ReleasedTextureCount;
		return true;
	});

	// Sounds: the same rule (a voice playing one holds it, so a sound still playing, a one-shot carried over from the
	// previous scene say, is never freed under it)
	std::size_t ReleasedSoundCount{ 0 };
	std::erase_if(m_SoundsByAbsolutePath, [this, &ReleasedSoundCount](const auto& CachedSoundEntry)
	{
		if (CachedSoundEntry.second.use_count() > 1)
		{
			return false;
		}
		m_AudioEngine.releaseSound(*CachedSoundEntry.second);
		++ReleasedSoundCount;
		return true;
	});

	if (ReleasedSpriteSheetCount > 0 || ReleasedTextureCount > 0 || ReleasedSoundCount > 0)
	{
		LOG("Unloaded what the new scene doesn't use: {} sprite sheet(s) ({} page textures), {} texture(s), {} sound(s). Their GPU memory goes within a couple of frames.",
			ReleasedSpriteSheetCount, ReleasedPageCount, ReleasedTextureCount, ReleasedSoundCount);
	}
}

std::shared_ptr<const Audio::SoundAsset> AssetLibrary::getOrLoadSound(const std::filesystem::path& SoundRelativePath)
{
	const std::filesystem::path AbsoluteSoundPath = convertToAbsoluteAssetPath(SoundRelativePath);
	const std::string AbsoluteSoundPathAsText = AbsoluteSoundPath.generic_string();
	if (const auto CachedSound = m_SoundsByAbsolutePath.find(AbsoluteSoundPathAsText); CachedSound != m_SoundsByAbsolutePath.end())
	{
		return CachedSound->second;
	}

	const auto LoadStartTime = std::chrono::steady_clock::now();
	std::shared_ptr<Audio::SoundAsset> LoadedSound = m_AudioEngine.createSound(AbsoluteSoundPath);	// Logs its own problems
	if (LoadedSound == nullptr)
	{
		return nullptr;
	}
	const double LoadTimeInMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - LoadStartTime).count();
	LOG("Loaded sound {}: {} channel(s), {} Hz, {}{:.1f} s, {} ({:.0f} ms).", AbsoluteSoundPath.filename().string(), LoadedSound->m_ChannelCount,
		LoadedSound->m_SampleRateInHertz, LoadedSound->m_IsStreamed ? "about " : "", LoadedSound->m_DurationInSeconds,
		LoadedSound->m_IsStreamed ? "streamed while playing" : "decoded whole", LoadTimeInMilliseconds);
	m_SoundsByAbsolutePath.emplace(AbsoluteSoundPathAsText, LoadedSound);
	return LoadedSound;
}

}
