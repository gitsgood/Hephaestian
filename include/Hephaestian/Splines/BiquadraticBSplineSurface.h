#ifndef HEPHAESTIAN_BIQUADRATICBSPLINESURFACE_H
#define HEPHAESTIAN_BIQUADRATICBSPLINESURFACE_H

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace Hephaestian::Splines
{

/*
* Bi-quadratic B-spline tensor-product surface, MAT301 chapter 12 (Listings 12.1 - 12.5):
*
*			   m-1 n-1
*	f(u, v) =  sum sum  c(i,j) B(i,2)(u) B(j,2)(v)		(eq. 12.1)
*			   i=0 j=0
*
* Control points are stored row-wise in the u-direction (like the book's mc): c(i,j) = m_ControlPoints[j * n_u + i].
* For a point, only the 3 x 3 control points around the two knot intervals are active (Algorithm 12.1).
*
* Book function -> name here:
*	B2()					-> calculateActiveBasisFunctionsInBothDirections()
*	evaluateBiquadratic()	-> sumActiveControlPointsWeightedByBasisFunctions()
*	makeBiquadraticSurface()-> tessellateSurfaceIntoTriangles()
*	findKnotInterval()		-> findKnotIntervalContainingParameter() (QuadraticBSplineBasis.h)
*/
class BiquadraticBSplineSurface
{
public:
	// The book's test surface (Listing 12.2): n_u = 4, n_v = 3, u = {0,0,0,1,2,2,2}, v = {0,0,0,1,1,1}
	BiquadraticBSplineSurface();

	void setControlGrid(int ControlPointCountU, int ControlPointCountV, std::vector<float> KnotVectorU, std::vector<float> KnotVectorV, std::vector<glm::vec3> ControlPoints);

	// Book Listing 12.4 (B2): the 3 active basis functions in each parameter direction, { along u, along v }
	[[nodiscard]] std::pair<glm::vec3, glm::vec3> calculateActiveBasisFunctionsInBothDirections(float ParameterU, float ParameterV, int KnotIntervalIndexU, int KnotIntervalIndexV) const;

	// Book Listing 12.5 (evaluateBiquadratic): every active control point times its weight (the outer product of the two
	// basis function triples), summed. The basis functions have to be computed first.
	[[nodiscard]] glm::vec3 sumActiveControlPointsWeightedByBasisFunctions(int KnotIntervalIndexU, int KnotIntervalIndexV, const glm::vec3& BasisFunctionsU, const glm::vec3& BasisFunctionsV) const;

	// Algorithm 12.1 in one call: find both knot intervals, the basis functions, then the weighted sum
	[[nodiscard]] glm::vec3 evaluateSurfacePoint(float ParameterU, float ParameterV) const;

	// Section 12.4 / eqs. 12.2 - 12.3: { df/du, df/dv }, with the basis derivatives from eq. 7.7
	[[nodiscard]] std::pair<glm::vec3, glm::vec3> evaluatePartialDerivatives(float ParameterU, float ParameterV) const;
	// Unit normal from the two partials (flipped to point up, +y)
	[[nodiscard]] glm::vec3 evaluateSurfaceNormal(float ParameterU, float ParameterV) const;

	// Book Listing 12.3 (makeBiquadraticSurface): points at a fixed parameter step over the whole parameter domain, plus a
	// regular triangulation of them. The optional filter drops triangles with any corner where it returns false.
	void tessellateSurfaceIntoTriangles(float ParameterStep, std::vector<glm::vec3>& OutVertexPositions, std::vector<std::uint32_t>& OutTriangleIndices,
		const std::function<bool(float ParameterU, float ParameterV)>& ShouldKeepParameterPoint = {}) const;

	[[nodiscard]] float getParameterDomainStartU() const noexcept { return m_KnotVectorU[static_cast<std::size_t>(m_DegreeU)]; }			// u[d_u]
	[[nodiscard]] float getParameterDomainEndU() const noexcept { return m_KnotVectorU[static_cast<std::size_t>(m_ControlPointCountU)]; }	// u[n_u]
	[[nodiscard]] float getParameterDomainStartV() const noexcept { return m_KnotVectorV[static_cast<std::size_t>(m_DegreeV)]; }
	[[nodiscard]] float getParameterDomainEndV() const noexcept { return m_KnotVectorV[static_cast<std::size_t>(m_ControlPointCountV)]; }
	[[nodiscard]] int getControlPointCountU() const noexcept { return m_ControlPointCountU; }
	[[nodiscard]] int getControlPointCountV() const noexcept { return m_ControlPointCountV; }

private:
	[[nodiscard]] const glm::vec3& getControlPoint(int ControlPointIndexU, int ControlPointIndexV) const noexcept
	{
		return m_ControlPoints[static_cast<std::size_t>(ControlPointIndexV) * static_cast<std::size_t>(m_ControlPointCountU) + static_cast<std::size_t>(ControlPointIndexU)];
	}

	int m_ControlPointCountU{ 4 };			// Book: n_u
	int m_ControlPointCountV{ 3 };			// Book: n_v
	int m_DegreeU{ 2 };						// Book: d_u
	int m_DegreeV{ 2 };						// Book: d_v
	std::vector<float> m_KnotVectorU;		// Book: mu
	std::vector<float> m_KnotVectorV;		// Book: mv
	std::vector<glm::vec3> m_ControlPoints;	// Book: mc, row-wise in the u-direction
};

}

#endif // !HEPHAESTIAN_BIQUADRATICBSPLINESURFACE_H
