#include "Hephaestian/Splines/QuadraticBSplineCurve.h"

#include <algorithm>

#include "Hephaestian/Splines/QuadraticBSplineBasis.h"

namespace Hephaestian::Splines
{

void QuadraticBSplineCurve::setControlPoints(std::vector<glm::vec2> ControlPointsInUVPlane)
{
	m_ControlPoints = std::move(ControlPointsInUVPlane);
	rebuildKnotVector();
}

void QuadraticBSplineCurve::appendControlPoint(const glm::vec2& ControlPointInUVPlane)
{
	m_ControlPoints.push_back(ControlPointInUVPlane);
	rebuildKnotVector();	// Cheap: one float per control point, and a clamped knot vector changes at the end anyway
}

void QuadraticBSplineCurve::clear() noexcept
{
	m_ControlPoints.clear();
	m_KnotVector.clear();
}

void QuadraticBSplineCurve::rebuildKnotVector()
{
	m_KnotVector = isEvaluable() ? makeClampedUniformQuadraticKnotVector(static_cast<int>(m_ControlPoints.size())) : std::vector<float>{};
}

float QuadraticBSplineCurve::getParameterDomainEnd() const noexcept
{
	return isEvaluable() ? static_cast<float>(m_ControlPoints.size()) - static_cast<float>(c_QuadraticDegree) : 0.0f;
}

glm::vec2 QuadraticBSplineCurve::evaluateCurvePoint(float CurveParameter) const
{
	if (!isEvaluable())
	{
		return m_ControlPoints.empty() ? glm::vec2(0.0f) : m_ControlPoints.front();
	}

	CurveParameter = std::clamp(CurveParameter, 0.0f, getParameterDomainEnd());
	const int ControlPointCount = static_cast<int>(m_ControlPoints.size());
	const int KnotIntervalIndex = findKnotIntervalContainingParameter(m_KnotVector, c_QuadraticDegree, ControlPointCount, CurveParameter);
	const glm::vec3 ActiveBasisFunctions = calculateActiveQuadraticBasisFunctions(m_KnotVector, KnotIntervalIndex, CurveParameter);	// { B(mu-2,2), B(mu-1,2), B(mu,2) }

	// eq. 5.24 with only the three active control points
	return m_ControlPoints[static_cast<std::size_t>(KnotIntervalIndex - 2)] * ActiveBasisFunctions[0]
		+ m_ControlPoints[static_cast<std::size_t>(KnotIntervalIndex - 1)] * ActiveBasisFunctions[1]
		+ m_ControlPoints[static_cast<std::size_t>(KnotIntervalIndex)] * ActiveBasisFunctions[2];
}

void QuadraticBSplineCurve::sampleCurve(int SamplesPerKnotInterval, std::vector<glm::vec2>& OutCurvePoints) const
{
	OutCurvePoints.clear();
	if (!isEvaluable())
	{
		return;
	}

	SamplesPerKnotInterval = std::max(SamplesPerKnotInterval, 1);
	const float ParameterDomainEnd = getParameterDomainEnd();
	const int TotalSampleCount = static_cast<int>(ParameterDomainEnd) * SamplesPerKnotInterval + 1;
	OutCurvePoints.reserve(static_cast<std::size_t>(TotalSampleCount));
	for (int CurrentSampleIndex{ 0 }; CurrentSampleIndex < TotalSampleCount; ++CurrentSampleIndex)
	{
		const float CurveParameter = ParameterDomainEnd * static_cast<float>(CurrentSampleIndex) / static_cast<float>(TotalSampleCount - 1);
		OutCurvePoints.push_back(evaluateCurvePoint(CurveParameter));
	}
}

}
