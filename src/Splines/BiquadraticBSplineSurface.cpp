#include "Hephaestian/Splines/BiquadraticBSplineSurface.h"

#include <algorithm>
#include <cmath>

#include "Hephaestian/Splines/QuadraticBSplineBasis.h"

namespace Hephaestian::Splines
{

BiquadraticBSplineSurface::BiquadraticBSplineSurface()
{
	// Book Listing 12.2: knot vectors and control points of the test surface
	m_ControlPointCountU = 4; m_ControlPointCountV = 3; m_DegreeU = 2; m_DegreeV = 2;
	m_KnotVectorU = { 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 2.0f, 2.0f };
	m_KnotVectorV = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
	for (int ControlPointIndexU{ 0 }; ControlPointIndexU < m_ControlPointCountU; ++ControlPointIndexU) { m_ControlPoints.emplace_back(static_cast<float>(ControlPointIndexU), 0.0f, 0.0f); }
	m_ControlPoints.emplace_back(0.0f, 1.0f, 0.0f);
	m_ControlPoints.emplace_back(1.0f, 1.0f, 2.0f);
	m_ControlPoints.emplace_back(2.0f, 1.0f, 2.0f);
	m_ControlPoints.emplace_back(3.0f, 1.0f, 0.0f);
	for (int ControlPointIndexU{ 0 }; ControlPointIndexU < m_ControlPointCountU; ++ControlPointIndexU) { m_ControlPoints.emplace_back(static_cast<float>(ControlPointIndexU), 2.0f, 0.0f); }
}

void BiquadraticBSplineSurface::setControlGrid(int ControlPointCountU, int ControlPointCountV, std::vector<float> KnotVectorU, std::vector<float> KnotVectorV, std::vector<glm::vec3> ControlPoints)
{
	m_ControlPointCountU = ControlPointCountU;
	m_ControlPointCountV = ControlPointCountV;
	m_DegreeU = c_QuadraticDegree;
	m_DegreeV = c_QuadraticDegree;
	m_KnotVectorU = std::move(KnotVectorU);
	m_KnotVectorV = std::move(KnotVectorV);
	m_ControlPoints = std::move(ControlPoints);
}

std::pair<glm::vec3, glm::vec3> BiquadraticBSplineSurface::calculateActiveBasisFunctionsInBothDirections(float ParameterU, float ParameterV, int KnotIntervalIndexU, int KnotIntervalIndexV) const
{
	// Book Listing 12.4 (B2): compute the B-spline coefficients according to this scheme,
	// see Example 12.2.1 and Chapter 5.6.1 and Figure 5.17
	//
	//          1-w11       w11
	//         /    \      /    \     .
	//     1-w12     w12 1-w22   w22
	//       |        |    |      |
	//    B_mu-2,2   B_mu-1,2   B_mu,2
	const glm::vec3 BasisFunctionsU = calculateActiveQuadraticBasisFunctions(m_KnotVectorU, KnotIntervalIndexU, ParameterU);
	const glm::vec3 BasisFunctionsV = calculateActiveQuadraticBasisFunctions(m_KnotVectorV, KnotIntervalIndexV, ParameterV);
	return { BasisFunctionsU, BasisFunctionsV };
}

glm::vec3 BiquadraticBSplineSurface::sumActiveControlPointsWeightedByBasisFunctions(int KnotIntervalIndexU, int KnotIntervalIndexV, const glm::vec3& BasisFunctionsU, const glm::vec3& BasisFunctionsV) const
{
	// The active control points are c(muU-2 .. muU, muV-2 .. muV); each one's weight is its u basis function times its
	// v basis function (the outer product bu^T bv of Listing 12.5). The 9 weights always sum to 1.
	glm::vec3 WeightedSumOfControlPoints{ 0.0f, 0.0f, 0.0f };
	for (int ActiveControlPointOffsetV{ 0 }; ActiveControlPointOffsetV < 3; ++ActiveControlPointOffsetV)
	{
		for (int ActiveControlPointOffsetU{ 0 }; ActiveControlPointOffsetU < 3; ++ActiveControlPointOffsetU)
		{
			const float ControlPointWeight = BasisFunctionsU[ActiveControlPointOffsetU] * BasisFunctionsV[ActiveControlPointOffsetV];
			WeightedSumOfControlPoints += ControlPointWeight * getControlPoint(KnotIntervalIndexU - 2 + ActiveControlPointOffsetU, KnotIntervalIndexV - 2 + ActiveControlPointOffsetV);
		}
	}
	return WeightedSumOfControlPoints;
}

glm::vec3 BiquadraticBSplineSurface::evaluateSurfacePoint(float ParameterU, float ParameterV) const
{
	// Algorithm 12.1: 1. active intervals, 2. B-spline coefficients, 3. the weighted sum (eq. 12.1)
	const int KnotIntervalIndexU = findKnotIntervalContainingParameter(m_KnotVectorU, m_DegreeU, m_ControlPointCountU, ParameterU);
	const int KnotIntervalIndexV = findKnotIntervalContainingParameter(m_KnotVectorV, m_DegreeV, m_ControlPointCountV, ParameterV);
	const auto [BasisFunctionsU, BasisFunctionsV] = calculateActiveBasisFunctionsInBothDirections(ParameterU, ParameterV, KnotIntervalIndexU, KnotIntervalIndexV);
	return sumActiveControlPointsWeightedByBasisFunctions(KnotIntervalIndexU, KnotIntervalIndexV, BasisFunctionsU, BasisFunctionsV);
}

std::pair<glm::vec3, glm::vec3> BiquadraticBSplineSurface::evaluatePartialDerivatives(float ParameterU, float ParameterV) const
{
	const int KnotIntervalIndexU = findKnotIntervalContainingParameter(m_KnotVectorU, m_DegreeU, m_ControlPointCountU, ParameterU);
	const int KnotIntervalIndexV = findKnotIntervalContainingParameter(m_KnotVectorV, m_DegreeV, m_ControlPointCountV, ParameterV);
	const auto [BasisFunctionsU, BasisFunctionsV] = calculateActiveBasisFunctionsInBothDirections(ParameterU, ParameterV, KnotIntervalIndexU, KnotIntervalIndexV);
	const glm::vec3 BasisFunctionDerivativesU = calculateActiveQuadraticBasisFunctionDerivatives(m_KnotVectorU, KnotIntervalIndexU, ParameterU);
	const glm::vec3 BasisFunctionDerivativesV = calculateActiveQuadraticBasisFunctionDerivatives(m_KnotVectorV, KnotIntervalIndexV, ParameterV);

	// eq. 12.2: differentiate only the u basis functions; eq. 12.3: only the v ones. Same 3 x 3 active control points.
	return {
		sumActiveControlPointsWeightedByBasisFunctions(KnotIntervalIndexU, KnotIntervalIndexV, BasisFunctionDerivativesU, BasisFunctionsV),
		sumActiveControlPointsWeightedByBasisFunctions(KnotIntervalIndexU, KnotIntervalIndexV, BasisFunctionsU, BasisFunctionDerivativesV)
	};
}

glm::vec3 BiquadraticBSplineSurface::evaluateSurfaceNormal(float ParameterU, float ParameterV) const
{
	const auto [PartialDerivativeU, PartialDerivativeV] = evaluatePartialDerivatives(ParameterU, ParameterV);
	glm::vec3 SurfaceNormal = glm::cross(PartialDerivativeU, PartialDerivativeV);
	const float SurfaceNormalLength = glm::length(SurfaceNormal);
	if (SurfaceNormalLength < 1e-12f)
	{
		return { 0.0f, 1.0f, 0.0f };
	}
	SurfaceNormal /= SurfaceNormalLength;
	return SurfaceNormal.y < 0.0f ? -SurfaceNormal : SurfaceNormal;
}

void BiquadraticBSplineSurface::tessellateSurfaceIntoTriangles(float ParameterStep, std::vector<glm::vec3>& OutVertexPositions, std::vector<std::uint32_t>& OutTriangleIndices,
	const std::function<bool(float ParameterU, float ParameterV)>& ShouldKeepParameterPoint) const
{
	OutVertexPositions.clear();
	OutTriangleIndices.clear();
	ParameterStep = std::max(ParameterStep, 1e-4f);	// Book: h

	// Number of intervals in each parameter (for clamped knot vectors): (u[n_u] - u[d_u]) / h, and one more point than intervals
	const float ParameterDomainStartU = getParameterDomainStartU();
	const float ParameterDomainStartV = getParameterDomainStartV();
	const int PointCountAlongU = static_cast<int>(std::floor((getParameterDomainEndU() - ParameterDomainStartU) / ParameterStep + 1e-4f)) + 1;	// Book: nu
	const int PointCountAlongV = static_cast<int>(std::floor((getParameterDomainEndV() - ParameterDomainStartV) / ParameterStep + 1e-4f)) + 1;	// Book: nv

	std::vector<std::uint8_t> IsPointKept(static_cast<std::size_t>(PointCountAlongU) * static_cast<std::size_t>(PointCountAlongV), 1);
	OutVertexPositions.reserve(IsPointKept.size());
	for (int PointIndexV{ 0 }; PointIndexV < PointCountAlongV; ++PointIndexV)
	{
		for (int PointIndexU{ 0 }; PointIndexU < PointCountAlongU; ++PointIndexU)
		{
			// The last row/column lands exactly on the domain's end, even when the step doesn't divide it evenly
			const float ParameterU = std::min(ParameterDomainStartU + static_cast<float>(PointIndexU) * ParameterStep, getParameterDomainEndU());
			const float ParameterV = std::min(ParameterDomainStartV + static_cast<float>(PointIndexV) * ParameterStep, getParameterDomainEndV());
			const int KnotIntervalIndexU = findKnotIntervalContainingParameter(m_KnotVectorU, m_DegreeU, m_ControlPointCountU, ParameterU);
			const int KnotIntervalIndexV = findKnotIntervalContainingParameter(m_KnotVectorV, m_DegreeV, m_ControlPointCountV, ParameterV);
			const auto [BasisFunctionsU, BasisFunctionsV] = calculateActiveBasisFunctionsInBothDirections(ParameterU, ParameterV, KnotIntervalIndexU, KnotIntervalIndexV);
			OutVertexPositions.push_back(sumActiveControlPointsWeightedByBasisFunctions(KnotIntervalIndexU, KnotIntervalIndexV, BasisFunctionsU, BasisFunctionsV));

			if (ShouldKeepParameterPoint && !ShouldKeepParameterPoint(ParameterU, ParameterV))
			{
				IsPointKept[static_cast<std::size_t>(PointIndexV) * static_cast<std::size_t>(PointCountAlongU) + static_cast<std::size_t>(PointIndexU)] = 0;
			}
		}
	}

	// Regular triangulation, as drawn in Listing 12.3:
	//
	// TopLeft======TopRight
	// |   \             |
	// |        \        |
	// BottomLeft===BottomRight		-> triangles (BottomLeft, BottomRight, TopLeft) and (BottomRight, TopRight, TopLeft)
	//
	OutTriangleIndices.reserve(static_cast<std::size_t>(PointCountAlongU - 1) * static_cast<std::size_t>(PointCountAlongV - 1) * 6);
	for (int PointIndexV{ 0 }; PointIndexV < PointCountAlongV - 1; ++PointIndexV)
	{
		for (int PointIndexU{ 0 }; PointIndexU < PointCountAlongU - 1; ++PointIndexU)
		{
			const std::uint32_t BottomLeftCorner = static_cast<std::uint32_t>(PointIndexV * PointCountAlongU + PointIndexU);	// Book: 1
			const std::uint32_t BottomRightCorner = BottomLeftCorner + 1;															// Book: 2
			const std::uint32_t TopLeftCorner = static_cast<std::uint32_t>((PointIndexV + 1) * PointCountAlongU + PointIndexU);		// Book: 4
			const std::uint32_t TopRightCorner = TopLeftCorner + 1;																	// Book: 3
			const bool AreAllCornersKept = IsPointKept[BottomLeftCorner] && IsPointKept[BottomRightCorner] && IsPointKept[TopRightCorner] && IsPointKept[TopLeftCorner];
			if (!AreAllCornersKept)
			{
				continue;
			}
			OutTriangleIndices.insert(OutTriangleIndices.end(), { BottomLeftCorner, BottomRightCorner, TopLeftCorner, BottomRightCorner, TopRightCorner, TopLeftCorner });
		}
	}
}

}
