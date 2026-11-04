#include "Hephaestian/Terrain/GroundHeightfield.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

#include <Vulkanised/Logger.h>

namespace Hephaestian::Terrain
{

namespace
{
	// 8-neighbourhood offsets, and the first 4 are the 4-neighbourhood
	constexpr std::array<glm::ivec2, 8> c_NeighbourOffsets{ {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 }
	} };

	[[nodiscard]] const char* skipSpacesAndTabs(const char* Cursor, const char* End) noexcept
	{
		while (Cursor < End && (*Cursor == ' ' || *Cursor == '\t'))
		{
			++Cursor;
		}
		return Cursor;
	}

	// Cross product of (Second - First) x (Third - First) in 2D: > 0 when the three points turn counter-clockwise
	[[nodiscard]] float calculateTurnDirection(const glm::vec2& FirstPoint, const glm::vec2& SecondPoint, const glm::vec2& ThirdPoint) noexcept
	{
		return (SecondPoint.x - FirstPoint.x) * (ThirdPoint.y - FirstPoint.y) - (SecondPoint.y - FirstPoint.y) * (ThirdPoint.x - FirstPoint.x);
	}
}

std::vector<glm::vec3> loadPointPositionsFromObjFile(const std::filesystem::path& ObjFilePath)
{
	// Whole file in one read: far faster than line-by-line streams on a 50+ MB file
	std::ifstream ObjFile(ObjFilePath, std::ios::binary | std::ios::ate);
	if (!ObjFile)
	{
		LOGE("Couldn't open point cloud {}", ObjFilePath.string());
		return {};
	}
	const std::streamsize ObjFileSizeInBytes = ObjFile.tellg();
	std::string ObjFileContents(static_cast<std::size_t>(ObjFileSizeInBytes), '\0');
	ObjFile.seekg(0);
	ObjFile.read(ObjFileContents.data(), ObjFileSizeInBytes);

	std::vector<glm::vec3> PointPositions;
	PointPositions.reserve(ObjFileContents.size() / 30);	// ~30 bytes per "v x y z" line, avoids most reallocations

	const char* Cursor = ObjFileContents.data();
	const char* const End = Cursor + ObjFileContents.size();
	while (Cursor < End)
	{
		const char* const LineEnd = std::find(Cursor, End, '\n');
		if (LineEnd - Cursor > 2 && Cursor[0] == 'v' && (Cursor[1] == ' ' || Cursor[1] == '\t'))
		{
			glm::vec3 PointPosition{ 0.0f };
			const char* NumberCursor = Cursor + 2;
			bool HasParsedAllCoordinates = true;
			for (int CurrentCoordinateIndex{ 0 }; CurrentCoordinateIndex < 3; ++CurrentCoordinateIndex)
			{
				NumberCursor = skipSpacesAndTabs(NumberCursor, LineEnd);
				const std::from_chars_result ParseResult = std::from_chars(NumberCursor, LineEnd, PointPosition[CurrentCoordinateIndex]);	// Locale-independent, no allocations
				if (ParseResult.ec != std::errc{})
				{
					HasParsedAllCoordinates = false;
					break;
				}
				NumberCursor = ParseResult.ptr;
			}
			if (HasParsedAllCoordinates)
			{
				PointPositions.push_back(PointPosition);
			}
		}
		Cursor = LineEnd + 1;
	}

	return PointPositions;
}

GroundPlaneFrame computeMinimumAreaBoundingFrame(const std::vector<glm::vec3>& PointPositions)
{
	GroundPlaneFrame BestFrame{};
	if (PointPositions.size() < 3)
	{
		return BestFrame;
	}

	// 1. Convex hull of the XZ footprint (Andrew's monotone chain)
	std::vector<glm::vec2> FootprintPoints;
	FootprintPoints.reserve(PointPositions.size());
	for (const glm::vec3& CurrentPoint : PointPositions)
	{
		FootprintPoints.emplace_back(CurrentPoint.x, CurrentPoint.z);
	}
	std::sort(FootprintPoints.begin(), FootprintPoints.end(), [](const glm::vec2& LeftPoint, const glm::vec2& RightPoint) { return LeftPoint.x < RightPoint.x || (LeftPoint.x == RightPoint.x && LeftPoint.y < RightPoint.y); });

	std::vector<glm::vec2> ConvexHull(2 * FootprintPoints.size());
	std::size_t HullPointCount{ 0 };
	for (const glm::vec2& CurrentPoint : FootprintPoints)	// Lower hull
	{
		while (HullPointCount >= 2 && calculateTurnDirection(ConvexHull[HullPointCount - 2], ConvexHull[HullPointCount - 1], CurrentPoint) <= 0.0f) { --HullPointCount; }
		ConvexHull[HullPointCount++] = CurrentPoint;
	}
	const std::size_t LowerHullPointCount = HullPointCount + 1;
	for (auto CurrentPoint = FootprintPoints.rbegin() + 1; CurrentPoint != FootprintPoints.rend(); ++CurrentPoint)	// Upper hull
	{
		while (HullPointCount >= LowerHullPointCount && calculateTurnDirection(ConvexHull[HullPointCount - 2], ConvexHull[HullPointCount - 1], *CurrentPoint) <= 0.0f) { --HullPointCount; }
		ConvexHull[HullPointCount++] = *CurrentPoint;
	}
	ConvexHull.resize(HullPointCount - 1);	// The last point repeats the first

	// 2. The minimum-area rectangle always has one side on a hull edge: try them all
	float SmallestArea = std::numeric_limits<float>::max();
	for (std::size_t CurrentEdgeIndex{ 0 }; CurrentEdgeIndex < ConvexHull.size(); ++CurrentEdgeIndex)
	{
		const glm::vec2 EdgeVector = ConvexHull[(CurrentEdgeIndex + 1) % ConvexHull.size()] - ConvexHull[CurrentEdgeIndex];
		const float EdgeLength = glm::length(EdgeVector);
		if (EdgeLength < 1e-6f)
		{
			continue;
		}
		const glm::vec2 CandidateAxisU = EdgeVector / EdgeLength;
		const glm::vec2 CandidateAxisV{ -CandidateAxisU.y, CandidateAxisU.x };

		glm::vec2 MinimumLocal{ std::numeric_limits<float>::max() };
		glm::vec2 MaximumLocal{ std::numeric_limits<float>::lowest() };
		for (const glm::vec2& CurrentHullPoint : ConvexHull)
		{
			const glm::vec2 Local{ glm::dot(CurrentHullPoint, CandidateAxisU), glm::dot(CurrentHullPoint, CandidateAxisV) };
			MinimumLocal = glm::min(MinimumLocal, Local);
			MaximumLocal = glm::max(MaximumLocal, Local);
		}

		const glm::vec2 RectangleSize = MaximumLocal - MinimumLocal;
		if (RectangleSize.x * RectangleSize.y < SmallestArea)
		{
			SmallestArea = RectangleSize.x * RectangleSize.y;
			BestFrame.m_AxisUXZ = CandidateAxisU;
			BestFrame.m_AxisVXZ = CandidateAxisV;
			BestFrame.m_OriginXZ = CandidateAxisU * MinimumLocal.x + CandidateAxisV * MinimumLocal.y;
			BestFrame.m_LengthAlongU = RectangleSize.x;
			BestFrame.m_LengthAlongV = RectangleSize.y;
		}
	}

	// Long side along the U axis, purely so "u" is the quarry's length (swap axes, keeping the frame right-handed in XZ)
	if (BestFrame.m_LengthAlongV > BestFrame.m_LengthAlongU)
	{
		const glm::vec2 PreviousAxisU = BestFrame.m_AxisUXZ;
		BestFrame.m_OriginXZ += PreviousAxisU * BestFrame.m_LengthAlongU;
		BestFrame.m_AxisUXZ = BestFrame.m_AxisVXZ;
		BestFrame.m_AxisVXZ = -PreviousAxisU;
		std::swap(BestFrame.m_LengthAlongU, BestFrame.m_LengthAlongV);
	}

	return BestFrame;
}

GroundHeightfield GroundHeightfield::buildFromPointCloud(const std::vector<glm::vec3>& PointPositions, const GroundHeightfieldSettings& Settings)
{
	GroundHeightfield Heightfield;
	Heightfield.m_Statistics.m_InputPointCount = PointPositions.size();
	Heightfield.m_CellSizeInMetres = std::max(Settings.m_CellSizeInMetres, 0.01f);
	Heightfield.m_GroundPlaneFrame = computeMinimumAreaBoundingFrame(PointPositions);

	const float CellSize = Heightfield.m_CellSizeInMetres;
	const GroundPlaneFrame& Frame = Heightfield.m_GroundPlaneFrame;
	const int SampleCountAlongU = std::max(1, static_cast<int>(std::ceil(Frame.m_LengthAlongU / CellSize)));
	const int SampleCountAlongV = std::max(1, static_cast<int>(std::ceil(Frame.m_LengthAlongV / CellSize)));
	Heightfield.m_SampleCountAlongU = SampleCountAlongU;
	Heightfield.m_SampleCountAlongV = SampleCountAlongV;

	const std::size_t SampleCount = static_cast<std::size_t>(SampleCountAlongU) * static_cast<std::size_t>(SampleCountAlongV);
	std::vector<float>& Heights = Heightfield.m_SampleHeights;
	std::vector<std::uint8_t>& IsValid = Heightfield.m_IsSampleValid;
	Heights.assign(SampleCount, std::numeric_limits<float>::max());
	IsValid.assign(SampleCount, 0);

	auto isInsideGrid = [&](int SampleIndexU, int SampleIndexV) { return SampleIndexU >= 0 && SampleIndexV >= 0 && SampleIndexU < SampleCountAlongU && SampleIndexV < SampleCountAlongV; };
	auto toLinearIndex = [&](int SampleIndexU, int SampleIndexV) { return static_cast<std::size_t>(SampleIndexV) * static_cast<std::size_t>(SampleCountAlongU) + static_cast<std::size_t>(SampleIndexU); };

	// 1. LOWEST point per cell = best guess for the ground (canopy is hit first, ground last)
	for (const glm::vec3& CurrentPoint : PointPositions)
	{
		const glm::vec2 Local = Frame.convertWorldXZToLocal({ CurrentPoint.x, CurrentPoint.z });
		const int CellIndexU = std::clamp(static_cast<int>(Local.x / CellSize), 0, SampleCountAlongU - 1);
		const int CellIndexV = std::clamp(static_cast<int>(Local.y / CellSize), 0, SampleCountAlongV - 1);
		const std::size_t LinearIndex = toLinearIndex(CellIndexU, CellIndexV);
		Heights[LinearIndex] = std::min(Heights[LinearIndex], CurrentPoint.y);
		IsValid[LinearIndex] = 1;
	}
	Heightfield.m_Statistics.m_CellsWithPoints = static_cast<std::size_t>(std::count(IsValid.begin(), IsValid.end(), std::uint8_t{ 1 }));

	// 2. OUTLIER REMOVAL (canopy and low noise), see GroundHeightfieldSettings: a cell goes when (almost) its whole window is
	// much lower (a tree) or much higher (a noise point below ground). Small windows first, so isolated cells go before
	// whole crowns are judged; each removal round only looks at the cells that are still valid.
	std::vector<std::size_t> CellsToRemove;
	auto removeOutliersWithWindowRadius = [&](int WindowRadiusInCells, float HeightDirection) -> std::size_t
	{
		std::size_t RemovedCellCount{ 0 };
		const int MinimumValidCellsInWindow = std::max(4, (2 * WindowRadiusInCells + 1) * (2 * WindowRadiusInCells + 1) / 4);	// Don't judge cells on almost no evidence
		for (int CurrentIteration{ 0 }; CurrentIteration < Settings.m_IterationsPerWindowRadius; ++CurrentIteration)
		{
			CellsToRemove.clear();
			for (int SampleIndexV{ 0 }; SampleIndexV < SampleCountAlongV; ++SampleIndexV)
			{
				for (int SampleIndexU{ 0 }; SampleIndexU < SampleCountAlongU; ++SampleIndexU)
				{
					const std::size_t LinearIndex = toLinearIndex(SampleIndexU, SampleIndexV);
					if (!IsValid[LinearIndex]) { continue; }

					// HeightDirection +1: count cells much LOWER than this one (canopy), -1: much HIGHER (low noise)
					const float SignedCellHeight = HeightDirection * Heights[LinearIndex];
					int ValidCellsInWindow{ 0 };
					int OutlyingCellsInWindow{ 0 };
					for (int OffsetV = -WindowRadiusInCells; OffsetV <= WindowRadiusInCells; ++OffsetV)
					{
						for (int OffsetU = -WindowRadiusInCells; OffsetU <= WindowRadiusInCells; ++OffsetU)
						{
							if ((OffsetU == 0 && OffsetV == 0) || !isInsideGrid(SampleIndexU + OffsetU, SampleIndexV + OffsetV)) { continue; }
							const std::size_t WindowCellIndex = toLinearIndex(SampleIndexU + OffsetU, SampleIndexV + OffsetV);
							if (!IsValid[WindowCellIndex]) { continue; }
							++ValidCellsInWindow;
							if (SignedCellHeight - HeightDirection * Heights[WindowCellIndex] > Settings.m_OutlierHeightThresholdInMetres) { ++OutlyingCellsInWindow; }
						}
					}
					if (ValidCellsInWindow >= MinimumValidCellsInWindow && static_cast<float>(OutlyingCellsInWindow) >= Settings.m_OutlierMinimumFraction * static_cast<float>(ValidCellsInWindow))
					{
						CellsToRemove.push_back(LinearIndex);
					}
				}
			}
			for (const std::size_t CurrentCellIndex : CellsToRemove) { IsValid[CurrentCellIndex] = 0; }
			RemovedCellCount += CellsToRemove.size();
			if (CellsToRemove.empty()) { break; }
		}
		return RemovedCellCount;
	};
	for (const int CurrentWindowRadius : Settings.m_LowNoiseWindowRadiiInCells)
	{
		Heightfield.m_Statistics.m_LowNoiseCellsRemoved += removeOutliersWithWindowRadius(CurrentWindowRadius, -1.0f);
	}
	for (const int CurrentWindowRadius : Settings.m_CanopyWindowRadiiInCells)
	{
		Heightfield.m_Statistics.m_CanopyCellsRemoved += removeOutliersWithWindowRadius(CurrentWindowRadius, 1.0f);
	}

	// 3. EXTERIOR: empty cells reachable from the grid border through other empty cells are outside the scan
	std::vector<std::uint8_t> IsExterior(SampleCount, 0);
	std::vector<glm::ivec2> FloodFillStack;
	auto pushIfExteriorCandidate = [&](int SampleIndexU, int SampleIndexV)
	{
		const std::size_t LinearIndex = toLinearIndex(SampleIndexU, SampleIndexV);
		if (!IsValid[LinearIndex] && !IsExterior[LinearIndex]) { IsExterior[LinearIndex] = 1; FloodFillStack.emplace_back(SampleIndexU, SampleIndexV); }
	};
	for (int SampleIndexU{ 0 }; SampleIndexU < SampleCountAlongU; ++SampleIndexU) { pushIfExteriorCandidate(SampleIndexU, 0); pushIfExteriorCandidate(SampleIndexU, SampleCountAlongV - 1); }
	for (int SampleIndexV{ 0 }; SampleIndexV < SampleCountAlongV; ++SampleIndexV) { pushIfExteriorCandidate(0, SampleIndexV); pushIfExteriorCandidate(SampleCountAlongU - 1, SampleIndexV); }
	while (!FloodFillStack.empty())
	{
		const glm::ivec2 CurrentCell = FloodFillStack.back();
		FloodFillStack.pop_back();
		for (int CurrentOffsetIndex{ 0 }; CurrentOffsetIndex < 4; ++CurrentOffsetIndex)	// 4-connected, so it can't leak through diagonal gaps
		{
			const glm::ivec2 NeighbourCell = CurrentCell + c_NeighbourOffsets[static_cast<std::size_t>(CurrentOffsetIndex)];
			if (isInsideGrid(NeighbourCell.x, NeighbourCell.y)) { pushIfExteriorCandidate(NeighbourCell.x, NeighbourCell.y); }
		}
	}
	Heightfield.m_Statistics.m_ExteriorCells = static_cast<std::size_t>(std::count(IsExterior.begin(), IsExterior.end(), std::uint8_t{ 1 }));

	// 4. HOLE FILLING in waves from the edges of each hole inwards: each empty cell takes the average of its valid neighbours.
	// Waves (instead of filling in scan order) keep the fill symmetric, so holes don't get streaky.
	auto fillEmptyCellsInWaves = [&](std::vector<float>& HeightsToFill, std::vector<std::uint8_t>& IsCellFilled, const std::vector<std::uint8_t>& IsCellAllowedToFill, int MinimumFilledNeighbours) -> std::size_t
	{
		std::size_t TotalFilledCellCount{ 0 };
		std::vector<std::pair<std::size_t, float>> CellsFilledThisWave;
		for (int MinimumNeighboursThisPass = MinimumFilledNeighbours; MinimumNeighboursThisPass >= 1; --MinimumNeighboursThisPass)
		{
			while (true)
			{
				CellsFilledThisWave.clear();
				for (int SampleIndexV{ 0 }; SampleIndexV < SampleCountAlongV; ++SampleIndexV)
				{
					for (int SampleIndexU{ 0 }; SampleIndexU < SampleCountAlongU; ++SampleIndexU)
					{
						const std::size_t LinearIndex = toLinearIndex(SampleIndexU, SampleIndexV);
						if (IsCellFilled[LinearIndex] || !IsCellAllowedToFill[LinearIndex]) { continue; }
						float NeighbourHeightSum{ 0.0f };
						int FilledNeighbourCount{ 0 };
						for (const glm::ivec2& Offset : c_NeighbourOffsets)
						{
							if (!isInsideGrid(SampleIndexU + Offset.x, SampleIndexV + Offset.y)) { continue; }
							const std::size_t NeighbourIndex = toLinearIndex(SampleIndexU + Offset.x, SampleIndexV + Offset.y);
							if (IsCellFilled[NeighbourIndex]) { NeighbourHeightSum += HeightsToFill[NeighbourIndex]; ++FilledNeighbourCount; }
						}
						if (FilledNeighbourCount >= MinimumNeighboursThisPass)
						{
							CellsFilledThisWave.emplace_back(LinearIndex, NeighbourHeightSum / static_cast<float>(FilledNeighbourCount));
						}
					}
				}
				if (CellsFilledThisWave.empty()) { break; }
				for (const auto& [CurrentCellIndex, CurrentHeight] : CellsFilledThisWave)
				{
					HeightsToFill[CurrentCellIndex] = CurrentHeight;
					IsCellFilled[CurrentCellIndex] = 1;
				}
				TotalFilledCellCount += CellsFilledThisWave.size();
			}
		}
		return TotalFilledCellCount;
	};

	std::vector<std::uint8_t> IsInteriorCell(SampleCount, 0);
	for (std::size_t CurrentCellIndex{ 0 }; CurrentCellIndex < SampleCount; ++CurrentCellIndex)
	{
		IsInteriorCell[CurrentCellIndex] = IsExterior[CurrentCellIndex] ? 0 : 1;
	}
	Heightfield.m_Statistics.m_HoleCellsFilled = fillEmptyCellsInWaves(Heights, IsValid, IsInteriorCell, 3);

	// 5. EXTENDED copy: the exterior filled in too, so the B-spline control grid is a complete rectangle
	Heightfield.m_ExtendedSampleHeights = Heights;
	std::vector<std::uint8_t> IsExtendedFilled = IsValid;
	const std::vector<std::uint8_t> EveryCellAllowed(SampleCount, 1);
	fillEmptyCellsInWaves(Heightfield.m_ExtendedSampleHeights, IsExtendedFilled, EveryCellAllowed, 1);
	for (std::size_t CurrentCellIndex{ 0 }; CurrentCellIndex < SampleCount; ++CurrentCellIndex)
	{
		if (!IsExtendedFilled[CurrentCellIndex]) { Heightfield.m_ExtendedSampleHeights[CurrentCellIndex] = 0.0f; }	// Only if there were no points at all
		if (!IsValid[CurrentCellIndex]) { Heights[CurrentCellIndex] = 0.0f; }
	}

	return Heightfield;
}

bool GroundHeightfield::isSampleValid(int SampleIndexU, int SampleIndexV) const noexcept
{
	if (SampleIndexU < 0 || SampleIndexV < 0 || SampleIndexU >= m_SampleCountAlongU || SampleIndexV >= m_SampleCountAlongV)
	{
		return false;
	}
	return m_IsSampleValid[convertSampleIndexToLinear(SampleIndexU, SampleIndexV)] != 0;
}

float GroundHeightfield::getSampleHeight(int SampleIndexU, int SampleIndexV) const noexcept
{
	SampleIndexU = std::clamp(SampleIndexU, 0, m_SampleCountAlongU - 1);
	SampleIndexV = std::clamp(SampleIndexV, 0, m_SampleCountAlongV - 1);
	return m_SampleHeights[convertSampleIndexToLinear(SampleIndexU, SampleIndexV)];
}

glm::vec3 GroundHeightfield::getSampleWorldPosition(int SampleIndexU, int SampleIndexV) const noexcept
{
	const glm::vec2 LocalPosition{ (static_cast<float>(SampleIndexU) + 0.5f) * m_CellSizeInMetres, (static_cast<float>(SampleIndexV) + 0.5f) * m_CellSizeInMetres };
	const glm::vec2 WorldXZ = m_GroundPlaneFrame.convertLocalToWorldXZ(LocalPosition);
	return { WorldXZ.x, getSampleHeight(SampleIndexU, SampleIndexV), WorldXZ.y };
}

float GroundHeightfield::sampleExtendedHeightAtLocalPosition(const glm::vec2& LocalPosition) const noexcept
{
	if (m_ExtendedSampleHeights.empty())
	{
		return 0.0f;
	}

	// Samples sit at cell centres, so shift by half a cell before splitting into integer + fraction
	const float SampleCoordinateU = std::clamp(LocalPosition.x / m_CellSizeInMetres - 0.5f, 0.0f, static_cast<float>(m_SampleCountAlongU - 1));
	const float SampleCoordinateV = std::clamp(LocalPosition.y / m_CellSizeInMetres - 0.5f, 0.0f, static_cast<float>(m_SampleCountAlongV - 1));
	const int LowerIndexU = std::min(static_cast<int>(SampleCoordinateU), std::max(m_SampleCountAlongU - 2, 0));
	const int LowerIndexV = std::min(static_cast<int>(SampleCoordinateV), std::max(m_SampleCountAlongV - 2, 0));
	const int UpperIndexU = std::min(LowerIndexU + 1, m_SampleCountAlongU - 1);
	const int UpperIndexV = std::min(LowerIndexV + 1, m_SampleCountAlongV - 1);
	const float FractionU = SampleCoordinateU - static_cast<float>(LowerIndexU);
	const float FractionV = SampleCoordinateV - static_cast<float>(LowerIndexV);

	const float HeightLowerLower = m_ExtendedSampleHeights[convertSampleIndexToLinear(LowerIndexU, LowerIndexV)];
	const float HeightUpperLower = m_ExtendedSampleHeights[convertSampleIndexToLinear(UpperIndexU, LowerIndexV)];
	const float HeightLowerUpper = m_ExtendedSampleHeights[convertSampleIndexToLinear(LowerIndexU, UpperIndexV)];
	const float HeightUpperUpper = m_ExtendedSampleHeights[convertSampleIndexToLinear(UpperIndexU, UpperIndexV)];
	return glm::mix(glm::mix(HeightLowerLower, HeightUpperLower, FractionU), glm::mix(HeightLowerUpper, HeightUpperUpper, FractionU), FractionV);
}

bool GroundHeightfield::isLocalPositionInsideScan(const glm::vec2& LocalPosition) const noexcept
{
	return isSampleValid(static_cast<int>(std::floor(LocalPosition.x / m_CellSizeInMetres)), static_cast<int>(std::floor(LocalPosition.y / m_CellSizeInMetres)));
}

void GroundHeightfield::buildGridTriangulation(std::vector<glm::vec3>& OutVertexPositions, std::vector<std::uint32_t>& OutTriangleIndices) const
{
	OutVertexPositions.clear();
	OutTriangleIndices.clear();

	// Only valid samples become vertices, so build a sample -> vertex index map first
	constexpr std::uint32_t c_NoVertex = std::numeric_limits<std::uint32_t>::max();
	std::vector<std::uint32_t> VertexIndexBySample(m_SampleHeights.size(), c_NoVertex);
	OutVertexPositions.reserve(m_SampleHeights.size());
	for (int SampleIndexV{ 0 }; SampleIndexV < m_SampleCountAlongV; ++SampleIndexV)
	{
		for (int SampleIndexU{ 0 }; SampleIndexU < m_SampleCountAlongU; ++SampleIndexU)
		{
			if (isSampleValid(SampleIndexU, SampleIndexV))
			{
				VertexIndexBySample[convertSampleIndexToLinear(SampleIndexU, SampleIndexV)] = static_cast<std::uint32_t>(OutVertexPositions.size());
				OutVertexPositions.push_back(getSampleWorldPosition(SampleIndexU, SampleIndexV));
			}
		}
	}

	OutTriangleIndices.reserve(m_SampleHeights.size() * 6);
	for (int SampleIndexV{ 0 }; SampleIndexV + 1 < m_SampleCountAlongV; ++SampleIndexV)
	{
		for (int SampleIndexU{ 0 }; SampleIndexU + 1 < m_SampleCountAlongU; ++SampleIndexU)
		{
			//	TopLeft======TopRight		Book-style regular triangulation, diagonal "/" from BottomLeft to TopRight:
			//	|    / |		both triangles when all four corners exist,
			//	|  /   |		otherwise the one triangle the three existing corners make
			//	BottomLeft===BottomRight
			const std::uint32_t BottomLeftVertex = VertexIndexBySample[convertSampleIndexToLinear(SampleIndexU, SampleIndexV)];
			const std::uint32_t BottomRightVertex = VertexIndexBySample[convertSampleIndexToLinear(SampleIndexU + 1, SampleIndexV)];
			const std::uint32_t TopRightVertex = VertexIndexBySample[convertSampleIndexToLinear(SampleIndexU + 1, SampleIndexV + 1)];
			const std::uint32_t TopLeftVertex = VertexIndexBySample[convertSampleIndexToLinear(SampleIndexU, SampleIndexV + 1)];
			const int ExistingCornerCount = (BottomLeftVertex != c_NoVertex) + (BottomRightVertex != c_NoVertex) + (TopRightVertex != c_NoVertex) + (TopLeftVertex != c_NoVertex);

			if (ExistingCornerCount == 4)
			{
				OutTriangleIndices.insert(OutTriangleIndices.end(), { BottomLeftVertex, BottomRightVertex, TopRightVertex, BottomLeftVertex, TopRightVertex, TopLeftVertex });
			}
			else if (ExistingCornerCount == 3)
			{
				for (const std::uint32_t CurrentCorner : { BottomLeftVertex, BottomRightVertex, TopRightVertex, TopLeftVertex })
				{
					if (CurrentCorner != c_NoVertex) { OutTriangleIndices.push_back(CurrentCorner); }
				}
			}
		}
	}
	// Winding isn't fixed here: TriangleSurface orients every triangle so its normal points up
}

}
