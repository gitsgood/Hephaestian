#ifndef HEPHAESTIAN_GROUNDHEIGHTFIELD_H
#define HEPHAESTIAN_GROUNDHEIGHTFIELD_H

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

namespace Hephaestian::Terrain
{

// Reads only the "v x y z" lines of an .obj (faces, normals, materials are ignored): a point cloud, as fast as possible
[[nodiscard]] std::vector<glm::vec3> loadPointPositionsFromObjFile(const std::filesystem::path& ObjFilePath);

/*
* A 2D frame on the ground plane (world XZ), aligned with the smallest rectangle that encloses the scan.
* Scans are rarely aligned with the world axes (this quarry is rotated ~45 degrees), and a grid aligned with the scan
* wastes no cells on empty corners. Its two axes ARE the UV plane used by the B-spline surface and curve.
*	local (s, t) = ((xz - origin) . axisU, (xz - origin) . axisV), with 0 <= s <= m_LengthAlongU, 0 <= t <= m_LengthAlongV
*/
struct GroundPlaneFrame
{
	glm::vec2 m_OriginXZ{ 0.0f };
	glm::vec2 m_AxisUXZ{ 1.0f, 0.0f };	// Unit length, along the rectangle's long side
	glm::vec2 m_AxisVXZ{ 0.0f, 1.0f };	// Unit length, perpendicular to m_AxisUXZ
	float m_LengthAlongU{ 0.0f };
	float m_LengthAlongV{ 0.0f };

	[[nodiscard]] glm::vec2 convertWorldXZToLocal(const glm::vec2& WorldXZ) const noexcept
	{
		const glm::vec2 OffsetFromOrigin = WorldXZ - m_OriginXZ;
		return { glm::dot(OffsetFromOrigin, m_AxisUXZ), glm::dot(OffsetFromOrigin, m_AxisVXZ) };
	}
	[[nodiscard]] glm::vec2 convertLocalToWorldXZ(const glm::vec2& LocalPosition) const noexcept
	{
		return m_OriginXZ + m_AxisUXZ * LocalPosition.x + m_AxisVXZ * LocalPosition.y;
	}
};

// Minimum-area enclosing rectangle of the points' XZ footprint (convex hull + rotating the rectangle onto each hull edge)
[[nodiscard]] GroundPlaneFrame computeMinimumAreaBoundingFrame(const std::vector<glm::vec3>& PointPositions);

struct GroundHeightfieldSettings
{
	float m_CellSizeInMetres{ 0.5f };

	// Outlier filter, with square windows of growing radius: a cell is removed when at least m_OutlierMinimumFraction of
	// the valid cells around it are more than m_OutlierHeightThresholdInMetres LOWER (canopy, trees) or HIGHER (low noise).
	// Trees are compact, so they're almost entirely surrounded by lower ground. A terrace edge only has ~50% of its window
	// lower and a 90-degree bench corner 75%, so with 85% the quarry's real shape survives while clusters of trees don't.
	float m_OutlierHeightThresholdInMetres{ 1.5f };
	float m_OutlierMinimumFraction{ 0.85f };
	std::vector<int> m_CanopyWindowRadiiInCells{ 1, 2, 4, 8 };	// Up to 4 m at 0.5 m cells: crowns up to ~2.5 m radius
	std::vector<int> m_LowNoiseWindowRadiiInCells{ 1, 2 };		// Noise is isolated; big windows would eat real hollows
	int m_IterationsPerWindowRadius{ 3 };
};

// What the build did, for logging
struct GroundHeightfieldStatistics
{
	std::size_t m_InputPointCount{ 0 };
	std::size_t m_CellsWithPoints{ 0 };
	std::size_t m_CanopyCellsRemoved{ 0 };
	std::size_t m_LowNoiseCellsRemoved{ 0 };
	std::size_t m_HoleCellsFilled{ 0 };
	std::size_t m_ExteriorCells{ 0 };
};

/*
* Bare-earth heightfield on a regular grid, built from an airborne point cloud (book section 11.4, "regular triangulation
* with smoothing"): every cell keeps the LOWEST point that falls in it (LiDAR hits the canopy first, the ground last),
* isolated canopy cells are removed, and holes inside the scan are filled from their neighbours. Cells outside the scan
* (connected to the grid border without any data) stay invalid, so no triangles are invented outside the scanned area.
*
* Sample (i, j) sits at the CENTRE of cell (i, j): local position ((i + 0.5) * h, (j + 0.5) * h).
*/
class GroundHeightfield
{
public:
	[[nodiscard]] static GroundHeightfield buildFromPointCloud(const std::vector<glm::vec3>& PointPositions, const GroundHeightfieldSettings& Settings);

	[[nodiscard]] const GroundPlaneFrame& getGroundPlaneFrame() const noexcept { return m_GroundPlaneFrame; }
	[[nodiscard]] float getCellSizeInMetres() const noexcept { return m_CellSizeInMetres; }
	[[nodiscard]] int getSampleCountAlongU() const noexcept { return m_SampleCountAlongU; }
	[[nodiscard]] int getSampleCountAlongV() const noexcept { return m_SampleCountAlongV; }
	[[nodiscard]] const GroundHeightfieldStatistics& getStatistics() const noexcept { return m_Statistics; }

	[[nodiscard]] bool isSampleValid(int SampleIndexU, int SampleIndexV) const noexcept;
	[[nodiscard]] float getSampleHeight(int SampleIndexU, int SampleIndexV) const noexcept;
	[[nodiscard]] glm::vec3 getSampleWorldPosition(int SampleIndexU, int SampleIndexV) const noexcept;

	// Bilinear height anywhere in the frame, from a copy where even the exterior is filled in (never undefined).
	// Used for the B-spline control grid, which has to be a complete rectangle.
	[[nodiscard]] float sampleExtendedHeightAtLocalPosition(const glm::vec2& LocalPosition) const noexcept;

	// True if the nearest sample is a real (scanned or hole-filled) one, false outside the scan
	[[nodiscard]] bool isLocalPositionInsideScan(const glm::vec2& LocalPosition) const noexcept;

	// Regular triangulation of every quad of valid samples, diagonals oriented "/" as in the book.
	// Quads with exactly 3 valid corners get one triangle, so the scan's edge isn't jagged.
	void buildGridTriangulation(std::vector<glm::vec3>& OutVertexPositions, std::vector<std::uint32_t>& OutTriangleIndices) const;

private:
	[[nodiscard]] std::size_t convertSampleIndexToLinear(int SampleIndexU, int SampleIndexV) const noexcept { return static_cast<std::size_t>(SampleIndexV) * static_cast<std::size_t>(m_SampleCountAlongU) + static_cast<std::size_t>(SampleIndexU); }

	GroundPlaneFrame m_GroundPlaneFrame;
	float m_CellSizeInMetres{ 0.5f };
	int m_SampleCountAlongU{ 0 };
	int m_SampleCountAlongV{ 0 };
	std::vector<float> m_SampleHeights;
	std::vector<std::uint8_t> m_IsSampleValid;
	std::vector<float> m_ExtendedSampleHeights;
	GroundHeightfieldStatistics m_Statistics;
};

}

#endif // !HEPHAESTIAN_GROUNDHEIGHTFIELD_H
