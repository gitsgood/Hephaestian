#ifndef HEPHAESTIAN_QUADRATICBSPLINECURVE_H
#define HEPHAESTIAN_QUADRATICBSPLINECURVE_H

#include <vector>

#include <glm/glm.hpp>

namespace Hephaestian::Splines
{

/*
* Quadratic B-spline curve in the surface's UV (parameter) plane, MAT301 eq. 5.24 / chapter 7.4 - 7.6:
*	f(t) = c(mu-2) B(mu-2,2)(t) + c(mu-1) B(mu-1,2)(t) + c(mu) B(mu,2)(t)		for t[mu] <= t < t[mu+1]
* with a clamped uniform knot vector, so the curve starts at the first control point and ends at the last.
* Uses exactly the same knot interval search and basis functions as BiquadraticBSplineSurface.
*/
class QuadraticBSplineCurve
{
public:
	// Needs at least 3 control points to be a quadratic curve; fewer just leaves the curve empty
	void setControlPoints(std::vector<glm::vec2> ControlPointsInUVPlane);
	void appendControlPoint(const glm::vec2& ControlPointInUVPlane);
	void clear() noexcept;

	[[nodiscard]] bool isEvaluable() const noexcept { return m_ControlPoints.size() >= 3; }
	[[nodiscard]] float getParameterDomainEnd() const noexcept;	// The parameter runs from 0 to n - 2
	[[nodiscard]] glm::vec2 evaluateCurvePoint(float CurveParameter) const;

	// SamplesPerKnotInterval points per knot interval, from the start of the curve to its end (inclusive)
	void sampleCurve(int SamplesPerKnotInterval, std::vector<glm::vec2>& OutCurvePoints) const;

	[[nodiscard]] const std::vector<glm::vec2>& getControlPoints() const noexcept { return m_ControlPoints; }

private:
	void rebuildKnotVector();

	std::vector<glm::vec2> m_ControlPoints;
	std::vector<float> m_KnotVector;
};

}

#endif // !HEPHAESTIAN_QUADRATICBSPLINECURVE_H
