#ifndef HEPHAESTIAN_TRIANGLESURFACE_H
#define HEPHAESTIAN_TRIANGLESURFACE_H

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

namespace Hephaestian::Physics
{

/*
* A triangulated surface that can answer "which triangle is under this point?" fast (book chapter 6.2.5 - 6.2.7):
*	- triangles with indexing (6.2.5)
*	- neighbour information per triangle (6.2.6): neighbour k is across the edge OPPOSITE vertex k
*	- topological search (6.2.7): barycentric coordinates in the XZ plane (y is up here, the book has z up), and while
*	  the point is outside the current triangle, step to the neighbour across the edge with the most negative coordinate
*
* The walk starts from a hint (a ball's triangle from the previous step, usually 0 or 1 step away), or from a coarse
* bucket grid for a cold start, so lookups are effectively O(1) however many triangles there are.
* Works with ANY triangulation (regular grid, B-spline tessellation, Delaunay...), as long as it's a heightfield.
*/
class TriangleSurface
{
public:
	static constexpr int c_NoTriangle{ -1 };

	// Every triangle is re-wound so its normal points up (+y)
	void buildFromTriangles(std::vector<glm::vec3> VertexPositions, std::vector<std::uint32_t> TriangleIndices);

	// The triangle whose XZ projection contains the point, or c_NoTriangle when it's off the surface
	[[nodiscard]] int findTriangleContainingPoint(const glm::vec2& WorldXZ, int StartTriangleIndexHint = c_NoTriangle) const;

	// Barycentric coordinates of the point's XZ projection in a triangle (they sum to 1; all >= 0 inside)
	[[nodiscard]] glm::vec3 calculateBarycentricCoordinatesXZ(int TriangleIndex, const glm::vec2& WorldXZ) const;

	// Height of the triangle's plane at that XZ position
	[[nodiscard]] float calculateHeightOnTriangle(int TriangleIndex, const glm::vec2& WorldXZ) const;
	[[nodiscard]] std::optional<float> sampleHeight(const glm::vec2& WorldXZ, int StartTriangleIndexHint = c_NoTriangle) const;

	[[nodiscard]] std::size_t getTriangleCount() const noexcept { return m_TriangleNormals.size(); }
	[[nodiscard]] const std::vector<glm::vec3>& getVertexPositions() const noexcept { return m_VertexPositions; }
	[[nodiscard]] const std::vector<std::uint32_t>& getTriangleIndices() const noexcept { return m_TriangleIndices; }
	[[nodiscard]] const glm::vec3& getTriangleNormal(int TriangleIndex) const noexcept { return m_TriangleNormals[static_cast<std::size_t>(TriangleIndex)]; }
	[[nodiscard]] const glm::vec3& getTriangleVertex(int TriangleIndex, int CornerIndex) const noexcept
	{
		return m_VertexPositions[m_TriangleIndices[static_cast<std::size_t>(TriangleIndex) * 3 + static_cast<std::size_t>(CornerIndex)]];
	}
	[[nodiscard]] const std::array<int, 3>& getTriangleNeighbours(int TriangleIndex) const noexcept { return m_TriangleNeighbours[static_cast<std::size_t>(TriangleIndex)]; }

	// Area-weighted average of the triangle normals around each vertex, for smooth rendering
	[[nodiscard]] std::vector<glm::vec3> calculateSmoothVertexNormals() const;

	[[nodiscard]] bool isEmpty() const noexcept { return m_TriangleNormals.empty(); }

private:
	void buildNeighbourInformation();
	void buildStartingPointBucketGrid();
	[[nodiscard]] int walkTowardsPoint(const glm::vec2& WorldXZ, int StartTriangleIndex) const;
	[[nodiscard]] int findStartingTriangleNear(const glm::vec2& WorldXZ) const;

	std::vector<glm::vec3> m_VertexPositions;
	std::vector<std::uint32_t> m_TriangleIndices;
	std::vector<glm::vec3> m_TriangleNormals;
	std::vector<std::array<int, 3>> m_TriangleNeighbours;

	// Cold-start accelerator: one triangle per square bucket of the XZ bounding box
	glm::vec2 m_BucketGridOriginXZ{ 0.0f };
	float m_BucketSizeInMetres{ 1.0f };
	int m_BucketCountX{ 0 };
	int m_BucketCountZ{ 0 };
	std::vector<int> m_StartingTriangleByBucket;
};

}

#endif // !HEPHAESTIAN_TRIANGLESURFACE_H
