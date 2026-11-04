#include "Hephaestian/Physics/TriangleSurface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Hephaestian::Physics
{

namespace
{
	// Walks shouldn't need anywhere near this many steps (a hint is 0-2 steps away), it only stops broken data looping forever
	constexpr int c_MaximumWalkSteps{ 4096 };

	// Points exactly on an edge must count as inside one of its two triangles, despite rounding
	constexpr float c_BarycentricInsideTolerance{ -1e-5f };

	[[nodiscard]] glm::vec2 projectOntoXZ(const glm::vec3& WorldPosition) noexcept { return { WorldPosition.x, WorldPosition.z }; }
}

void TriangleSurface::buildFromTriangles(std::vector<glm::vec3> VertexPositions, std::vector<std::uint32_t> TriangleIndices)
{
	m_VertexPositions = std::move(VertexPositions);
	m_TriangleIndices = std::move(TriangleIndices);
	m_TriangleIndices.resize(m_TriangleIndices.size() - m_TriangleIndices.size() % 3);

	const std::size_t TriangleCount = m_TriangleIndices.size() / 3;
	m_TriangleNormals.resize(TriangleCount);
	for (std::size_t CurrentTriangleIndex{ 0 }; CurrentTriangleIndex < TriangleCount; ++CurrentTriangleIndex)
	{
		std::uint32_t* const Corners = &m_TriangleIndices[CurrentTriangleIndex * 3];
		glm::vec3 Normal = glm::cross(m_VertexPositions[Corners[1]] - m_VertexPositions[Corners[0]], m_VertexPositions[Corners[2]] - m_VertexPositions[Corners[0]]);	// Book: N = v0v1 x v0v2
		if (Normal.y < 0.0f)
		{
			// Upside down: swap two corners so the normal (and the rendered front face) points up
			std::swap(Corners[1], Corners[2]);
			Normal = -Normal;
		}
		const float NormalLength = glm::length(Normal);
		m_TriangleNormals[CurrentTriangleIndex] = NormalLength > 1e-12f ? Normal / NormalLength : glm::vec3(0.0f, 1.0f, 0.0f);
	}

	buildNeighbourInformation();
	buildStartingPointBucketGrid();
}

void TriangleSurface::buildNeighbourInformation()
{
	const std::size_t TriangleCount = m_TriangleNormals.size();
	m_TriangleNeighbours.assign(TriangleCount, { c_NoTriangle, c_NoTriangle, c_NoTriangle });

	// Each undirected edge (smaller vertex index first) -> the first triangle/side seen with it; the second one completes the pair
	std::unordered_map<std::uint64_t, std::uint64_t> FirstTriangleSideByEdge;
	FirstTriangleSideByEdge.reserve(TriangleCount * 2);
	for (std::size_t CurrentTriangleIndex{ 0 }; CurrentTriangleIndex < TriangleCount; ++CurrentTriangleIndex)
	{
		for (std::size_t CurrentCornerIndex{ 0 }; CurrentCornerIndex < 3; ++CurrentCornerIndex)
		{
			// The edge OPPOSITE corner k joins corners k+1 and k+2
			std::uint32_t EdgeStartVertex = m_TriangleIndices[CurrentTriangleIndex * 3 + (CurrentCornerIndex + 1) % 3];
			std::uint32_t EdgeEndVertex = m_TriangleIndices[CurrentTriangleIndex * 3 + (CurrentCornerIndex + 2) % 3];
			if (EdgeStartVertex > EdgeEndVertex) { std::swap(EdgeStartVertex, EdgeEndVertex); }
			const std::uint64_t EdgeKey = (static_cast<std::uint64_t>(EdgeStartVertex) << 32) | EdgeEndVertex;
			const std::uint64_t TriangleSide = CurrentTriangleIndex * 3 + CurrentCornerIndex;

			const auto [ExistingEntry, WasInserted] = FirstTriangleSideByEdge.try_emplace(EdgeKey, TriangleSide);
			if (!WasInserted)
			{
				const std::uint64_t OtherTriangleSide = ExistingEntry->second;
				m_TriangleNeighbours[CurrentTriangleIndex][CurrentCornerIndex] = static_cast<int>(OtherTriangleSide / 3);
				m_TriangleNeighbours[OtherTriangleSide / 3][OtherTriangleSide % 3] = static_cast<int>(CurrentTriangleIndex);
			}
		}
	}
}

void TriangleSurface::buildStartingPointBucketGrid()
{
	m_StartingTriangleByBucket.clear();
	m_BucketCountX = m_BucketCountZ = 0;
	if (m_TriangleNormals.empty())
	{
		return;
	}

	glm::vec2 MinimumXZ{ std::numeric_limits<float>::max() };
	glm::vec2 MaximumXZ{ std::numeric_limits<float>::lowest() };
	double TotalProjectedArea{ 0.0 };
	for (std::size_t CurrentTriangleIndex{ 0 }; CurrentTriangleIndex < m_TriangleNormals.size(); ++CurrentTriangleIndex)
	{
		const glm::vec2 FirstCornerXZ = projectOntoXZ(getTriangleVertex(static_cast<int>(CurrentTriangleIndex), 0));
		const glm::vec2 SecondCornerXZ = projectOntoXZ(getTriangleVertex(static_cast<int>(CurrentTriangleIndex), 1));
		const glm::vec2 ThirdCornerXZ = projectOntoXZ(getTriangleVertex(static_cast<int>(CurrentTriangleIndex), 2));
		MinimumXZ = glm::min(MinimumXZ, glm::min(FirstCornerXZ, glm::min(SecondCornerXZ, ThirdCornerXZ)));
		MaximumXZ = glm::max(MaximumXZ, glm::max(FirstCornerXZ, glm::max(SecondCornerXZ, ThirdCornerXZ)));
		TotalProjectedArea += std::abs(0.5 * static_cast<double>((SecondCornerXZ.x - FirstCornerXZ.x) * (ThirdCornerXZ.y - FirstCornerXZ.y) - (SecondCornerXZ.y - FirstCornerXZ.y) * (ThirdCornerXZ.x - FirstCornerXZ.x)));
	}

	// About 2 triangles per bucket: small enough that a cold-start walk is a few steps, big enough to stay cheap
	const double AverageTriangleArea = TotalProjectedArea / static_cast<double>(m_TriangleNormals.size());
	m_BucketSizeInMetres = std::max(static_cast<float>(std::sqrt(2.0 * AverageTriangleArea)), 1e-3f);
	m_BucketGridOriginXZ = MinimumXZ;
	m_BucketCountX = std::max(1, static_cast<int>(std::ceil((MaximumXZ.x - MinimumXZ.x) / m_BucketSizeInMetres)) + 1);
	m_BucketCountZ = std::max(1, static_cast<int>(std::ceil((MaximumXZ.y - MinimumXZ.y) / m_BucketSizeInMetres)) + 1);
	m_StartingTriangleByBucket.assign(static_cast<std::size_t>(m_BucketCountX) * static_cast<std::size_t>(m_BucketCountZ), c_NoTriangle);

	for (std::size_t CurrentTriangleIndex{ 0 }; CurrentTriangleIndex < m_TriangleNormals.size(); ++CurrentTriangleIndex)
	{
		const int TriangleIndex = static_cast<int>(CurrentTriangleIndex);
		const glm::vec2 Centroid = (projectOntoXZ(getTriangleVertex(TriangleIndex, 0)) + projectOntoXZ(getTriangleVertex(TriangleIndex, 1)) + projectOntoXZ(getTriangleVertex(TriangleIndex, 2))) / 3.0f;
		const int BucketX = std::clamp(static_cast<int>((Centroid.x - m_BucketGridOriginXZ.x) / m_BucketSizeInMetres), 0, m_BucketCountX - 1);
		const int BucketZ = std::clamp(static_cast<int>((Centroid.y - m_BucketGridOriginXZ.y) / m_BucketSizeInMetres), 0, m_BucketCountZ - 1);
		m_StartingTriangleByBucket[static_cast<std::size_t>(BucketZ) * static_cast<std::size_t>(m_BucketCountX) + static_cast<std::size_t>(BucketX)] = TriangleIndex;
	}
}

glm::vec3 TriangleSurface::calculateBarycentricCoordinatesXZ(int TriangleIndex, const glm::vec2& WorldXZ) const
{
	// Book 5.1.7: each coordinate is the (signed) area of the sub-triangle opposite that corner, over the whole area
	const glm::vec2 FirstCornerXZ = projectOntoXZ(getTriangleVertex(TriangleIndex, 0));
	const glm::vec2 SecondCornerXZ = projectOntoXZ(getTriangleVertex(TriangleIndex, 1));
	const glm::vec2 ThirdCornerXZ = projectOntoXZ(getTriangleVertex(TriangleIndex, 2));
	const float TwiceSignedArea = (SecondCornerXZ.x - FirstCornerXZ.x) * (ThirdCornerXZ.y - FirstCornerXZ.y) - (SecondCornerXZ.y - FirstCornerXZ.y) * (ThirdCornerXZ.x - FirstCornerXZ.x);
	if (std::abs(TwiceSignedArea) < 1e-12f)
	{
		return glm::vec3(-1.0f);	// Vertical triangle: its XZ projection contains nothing
	}

	const float CoordinateForFirstCorner = ((SecondCornerXZ.x - WorldXZ.x) * (ThirdCornerXZ.y - WorldXZ.y) - (SecondCornerXZ.y - WorldXZ.y) * (ThirdCornerXZ.x - WorldXZ.x)) / TwiceSignedArea;
	const float CoordinateForSecondCorner = ((ThirdCornerXZ.x - WorldXZ.x) * (FirstCornerXZ.y - WorldXZ.y) - (ThirdCornerXZ.y - WorldXZ.y) * (FirstCornerXZ.x - WorldXZ.x)) / TwiceSignedArea;
	return { CoordinateForFirstCorner, CoordinateForSecondCorner, 1.0f - CoordinateForFirstCorner - CoordinateForSecondCorner };
}

int TriangleSurface::walkTowardsPoint(const glm::vec2& WorldXZ, int StartTriangleIndex) const
{
	int CurrentTriangleIndex = StartTriangleIndex;
	for (int CurrentStep{ 0 }; CurrentStep < c_MaximumWalkSteps && CurrentTriangleIndex != c_NoTriangle; ++CurrentStep)
	{
		const glm::vec3 BarycentricCoordinates = calculateBarycentricCoordinatesXZ(CurrentTriangleIndex, WorldXZ);
		if (BarycentricCoordinates.x >= c_BarycentricInsideTolerance && BarycentricCoordinates.y >= c_BarycentricInsideTolerance && BarycentricCoordinates.z >= c_BarycentricInsideTolerance)
		{
			return CurrentTriangleIndex;
		}

		// Most negative coordinate = the point is furthest beyond the edge opposite that corner: cross that edge
		int MostNegativeCornerIndex{ 0 };
		if (BarycentricCoordinates.y < BarycentricCoordinates[MostNegativeCornerIndex]) { MostNegativeCornerIndex = 1; }
		if (BarycentricCoordinates.z < BarycentricCoordinates[MostNegativeCornerIndex]) { MostNegativeCornerIndex = 2; }
		CurrentTriangleIndex = m_TriangleNeighbours[static_cast<std::size_t>(CurrentTriangleIndex)][static_cast<std::size_t>(MostNegativeCornerIndex)];
	}
	return c_NoTriangle;	// Walked off the surface's boundary
}

int TriangleSurface::findStartingTriangleNear(const glm::vec2& WorldXZ) const
{
	if (m_StartingTriangleByBucket.empty())
	{
		return c_NoTriangle;
	}

	const int CentreBucketX = static_cast<int>(std::floor((WorldXZ.x - m_BucketGridOriginXZ.x) / m_BucketSizeInMetres));
	const int CentreBucketZ = static_cast<int>(std::floor((WorldXZ.y - m_BucketGridOriginXZ.y) / m_BucketSizeInMetres));

	// Spiral out a few rings: buckets along the surface's edge or over holes can be empty
	for (int CurrentRing{ 0 }; CurrentRing <= 4; ++CurrentRing)
	{
		for (int OffsetZ = -CurrentRing; OffsetZ <= CurrentRing; ++OffsetZ)
		{
			for (int OffsetX = -CurrentRing; OffsetX <= CurrentRing; ++OffsetX)
			{
				if (std::max(std::abs(OffsetX), std::abs(OffsetZ)) != CurrentRing) { continue; }
				const int BucketX = CentreBucketX + OffsetX;
				const int BucketZ = CentreBucketZ + OffsetZ;
				if (BucketX < 0 || BucketZ < 0 || BucketX >= m_BucketCountX || BucketZ >= m_BucketCountZ) { continue; }
				const int StartingTriangle = m_StartingTriangleByBucket[static_cast<std::size_t>(BucketZ) * static_cast<std::size_t>(m_BucketCountX) + static_cast<std::size_t>(BucketX)];
				if (StartingTriangle != c_NoTriangle) { return StartingTriangle; }
			}
		}
	}
	return c_NoTriangle;
}

int TriangleSurface::findTriangleContainingPoint(const glm::vec2& WorldXZ, int StartTriangleIndexHint) const
{
	if (m_TriangleNormals.empty())
	{
		return c_NoTriangle;
	}

	// 1. From the hint: the normal case for a moving ball, 0-2 steps
	if (StartTriangleIndexHint >= 0 && static_cast<std::size_t>(StartTriangleIndexHint) < m_TriangleNormals.size())
	{
		if (const int FoundTriangle = walkTowardsPoint(WorldXZ, StartTriangleIndexHint); FoundTriangle != c_NoTriangle)
		{
			return FoundTriangle;
		}
	}

	// 2. From a nearby bucket: a cold start, or the walk hit a hole/concave bit of the boundary on the way
	return walkTowardsPoint(WorldXZ, findStartingTriangleNear(WorldXZ));
}

float TriangleSurface::calculateHeightOnTriangle(int TriangleIndex, const glm::vec2& WorldXZ) const
{
	const glm::vec3 BarycentricCoordinates = calculateBarycentricCoordinatesXZ(TriangleIndex, WorldXZ);
	return BarycentricCoordinates.x * getTriangleVertex(TriangleIndex, 0).y
		+ BarycentricCoordinates.y * getTriangleVertex(TriangleIndex, 1).y
		+ BarycentricCoordinates.z * getTriangleVertex(TriangleIndex, 2).y;
}

std::optional<float> TriangleSurface::sampleHeight(const glm::vec2& WorldXZ, int StartTriangleIndexHint) const
{
	const int FoundTriangle = findTriangleContainingPoint(WorldXZ, StartTriangleIndexHint);
	if (FoundTriangle == c_NoTriangle)
	{
		return std::nullopt;
	}
	return calculateHeightOnTriangle(FoundTriangle, WorldXZ);
}

std::vector<glm::vec3> TriangleSurface::calculateSmoothVertexNormals() const
{
	std::vector<glm::vec3> VertexNormals(m_VertexPositions.size(), glm::vec3(0.0f));
	for (std::size_t CurrentTriangleIndex{ 0 }; CurrentTriangleIndex < m_TriangleNormals.size(); ++CurrentTriangleIndex)
	{
		const std::uint32_t* const Corners = &m_TriangleIndices[CurrentTriangleIndex * 3];
		// Unnormalised cross product = normal * twice the area, so big triangles count more
		const glm::vec3 AreaWeightedNormal = glm::cross(m_VertexPositions[Corners[1]] - m_VertexPositions[Corners[0]], m_VertexPositions[Corners[2]] - m_VertexPositions[Corners[0]]);
		VertexNormals[Corners[0]] += AreaWeightedNormal;
		VertexNormals[Corners[1]] += AreaWeightedNormal;
		VertexNormals[Corners[2]] += AreaWeightedNormal;
	}
	for (glm::vec3& CurrentNormal : VertexNormals)
	{
		const float NormalLength = glm::length(CurrentNormal);
		CurrentNormal = NormalLength > 1e-12f ? CurrentNormal / NormalLength : glm::vec3(0.0f, 1.0f, 0.0f);
	}
	return VertexNormals;
}

}
