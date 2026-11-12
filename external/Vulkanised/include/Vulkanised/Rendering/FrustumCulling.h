#ifndef VULKANISED_RENDERING_FRUSTUMCULLING_H
#define VULKANISED_RENDERING_FRUSTUMCULLING_H

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/glm.hpp>

/*
* Frustum culling: anything whose bounding sphere is entirely outside the camera's view volume is never sent to the GPU.
* The test is a handful of dot products per instance, far cheaper than transforming and rasterising what nobody sees.
*/
namespace Vulkanised::Rendering
{

// A plane as (normal, distance): a point P is on the inside when dot(normal, P) + distance >= 0
struct FrustumPlanes
{
	std::array<glm::vec4, 6> m_Planes{};	// Left, right, bottom, top, near, far
};

// The six planes of whatever ViewProjectionMatrix sees (Gribb & Hartmann), for Vulkan's 0..1 clip depth.
// Planes come out in world space when given projection * view.
[[nodiscard]] inline FrustumPlanes extractFrustumPlanes(const glm::mat4& ViewProjectionMatrix) noexcept
{
	// GLM is column-major: row R of the matrix is (M[0][R], M[1][R], M[2][R], M[3][R])
	auto getRow = [&ViewProjectionMatrix](int RowIndex)
	{
		return glm::vec4(ViewProjectionMatrix[0][RowIndex], ViewProjectionMatrix[1][RowIndex], ViewProjectionMatrix[2][RowIndex], ViewProjectionMatrix[3][RowIndex]);
	};
	const glm::vec4 RowX = getRow(0);
	const glm::vec4 RowY = getRow(1);
	const glm::vec4 RowZ = getRow(2);
	const glm::vec4 RowW = getRow(3);

	FrustumPlanes Frustum{};
	Frustum.m_Planes[0] = RowW + RowX;	// Left:   -w <= x
	Frustum.m_Planes[1] = RowW - RowX;	// Right:   x <= w
	Frustum.m_Planes[2] = RowW + RowY;	// Bottom: -w <= y (which side is "top" doesn't matter, the Vulkan Y flip just swaps them)
	Frustum.m_Planes[3] = RowW - RowY;	// Top:     y <= w
	Frustum.m_Planes[4] = RowZ;			// Near:    0 <= z (Vulkan depth range, not OpenGL's -w)
	Frustum.m_Planes[5] = RowW - RowZ;	// Far:     z <= w

	// Normalised, so plane distances are real world units (sphere radii can be compared against them)
	for (glm::vec4& CurrentPlane : Frustum.m_Planes)
	{
		CurrentPlane /= glm::length(glm::vec3(CurrentPlane));
	}
	return Frustum;
}

// True if any part of the sphere might be visible (conservative: a sphere near a frustum corner can pass while being outside)
[[nodiscard]] inline bool isSphereInsideFrustum(const FrustumPlanes& Frustum, const glm::vec3& WorldCentre, float WorldRadius) noexcept
{
	for (const glm::vec4& CurrentPlane : Frustum.m_Planes)
	{
		if (glm::dot(glm::vec3(CurrentPlane), WorldCentre) + CurrentPlane.w < -WorldRadius)
		{
			return false;	// Completely on the outside of this plane
		}
	}
	return true;
}

// The largest factor a transform scales anything by: a sphere of radius R becomes (at most) one of radius R * this
[[nodiscard]] inline float getLargestScaleOfTransform(const glm::mat4& Transform) noexcept
{
	const float LargestSquaredAxisLength = std::max({ glm::dot(glm::vec3(Transform[0]), glm::vec3(Transform[0])),
		glm::dot(glm::vec3(Transform[1]), glm::vec3(Transform[1])), glm::dot(glm::vec3(Transform[2]), glm::vec3(Transform[2])) });
	return std::sqrt(LargestSquaredAxisLength);
}

}

#endif // !VULKANISED_RENDERING_FRUSTUMCULLING_H
