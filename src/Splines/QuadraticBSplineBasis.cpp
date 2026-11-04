#include "Hephaestian/Splines/QuadraticBSplineBasis.h"

#include <algorithm>

namespace Hephaestian::Splines
{

namespace
{
	[[nodiscard]] float getKnot(const std::vector<float>& KnotVector, int KnotIndex) noexcept
	{
		// Clamping only matters at the very ends, where the formulas multiply the out-of-range terms by 0 anyway
		return KnotVector[static_cast<std::size_t>(std::clamp(KnotIndex, 0, static_cast<int>(KnotVector.size()) - 1))];
	}
}

int findKnotIntervalContainingParameter(const std::vector<float>& KnotVector, int Degree, int ControlPointCount, float ParameterValue) noexcept
{
	int KnotIntervalIndex = ControlPointCount - 1;	// Index of the last control point (book: my = n-1)
	while (ParameterValue < KnotVector[static_cast<std::size_t>(KnotIntervalIndex)] && KnotIntervalIndex > Degree)
	{
		KnotIntervalIndex--;
	}
	return KnotIntervalIndex;
}

float calculateKnotWeight(const std::vector<float>& KnotVector, int KnotIndex, int Degree, float ParameterValue) noexcept
{
	const float StartKnot = getKnot(KnotVector, KnotIndex);
	const float EndKnot = getKnot(KnotVector, KnotIndex + Degree);
	if (StartKnot < EndKnot)
	{
		return (ParameterValue - StartKnot) / (EndKnot - StartKnot);
	}
	return 0.0f;
}

glm::vec3 calculateActiveQuadraticBasisFunctions(const std::vector<float>& KnotVector, int KnotIntervalIndex, float ParameterValue) noexcept
{
	const float LinearWeightFromIntervalStart = calculateKnotWeight(KnotVector, KnotIntervalIndex, 1, ParameterValue);			// w(mu,1), book: w11
	const float QuadraticWeightFromPreviousKnot = calculateKnotWeight(KnotVector, KnotIntervalIndex - 1, 2, ParameterValue);	// w(mu-1,2), book: w12
	const float QuadraticWeightFromIntervalStart = calculateKnotWeight(KnotVector, KnotIntervalIndex, 2, ParameterValue);		// w(mu,2), book: w22

	return {
		(1.0f - LinearWeightFromIntervalStart) * (1.0f - QuadraticWeightFromPreviousKnot),			// B(mu-2,2), eq. 5.26
		(1.0f - LinearWeightFromIntervalStart) * QuadraticWeightFromPreviousKnot
			+ LinearWeightFromIntervalStart * (1.0f - QuadraticWeightFromIntervalStart),			// B(mu-1,2), eq. 5.27
		LinearWeightFromIntervalStart * QuadraticWeightFromIntervalStart							// B(mu,2),   eq. 5.28
	};
}

glm::vec3 calculateActiveQuadraticBasisFunctionDerivatives(const std::vector<float>& KnotVector, int KnotIntervalIndex, float ParameterValue) noexcept
{
	const float LinearWeightFromIntervalStart = calculateKnotWeight(KnotVector, KnotIntervalIndex, 1, ParameterValue);
	const float LinearBasisFunctionEndingOnInterval = 1.0f - LinearWeightFromIntervalStart;	// B(mu-1,1) on this interval
	const float LinearBasisFunctionStartingOnInterval = LinearWeightFromIntervalStart;		// B(mu,1) on this interval

	// 2 / (t_(i+2) - t_i), or 0 when those knots coincide (the corresponding B(i,1) is identically 0 then)
	auto calculateDerivativeFactor = [&KnotVector](int KnotIndex)
	{
		const float KnotSpan = getKnot(KnotVector, KnotIndex + 2) - getKnot(KnotVector, KnotIndex);
		return KnotSpan > 0.0f ? 2.0f / KnotSpan : 0.0f;
	};
	const float DerivativeFactorFromPreviousKnot = calculateDerivativeFactor(KnotIntervalIndex - 1);	// 2 / (t_(mu+1) - t_(mu-1))
	const float DerivativeFactorFromIntervalStart = calculateDerivativeFactor(KnotIntervalIndex);		// 2 / (t_(mu+2) - t_mu)

	return {
		-DerivativeFactorFromPreviousKnot * LinearBasisFunctionEndingOnInterval,		// B'(mu-2,2): B(mu-2,1) = 0 here
		DerivativeFactorFromPreviousKnot * LinearBasisFunctionEndingOnInterval
			- DerivativeFactorFromIntervalStart * LinearBasisFunctionStartingOnInterval,	// B'(mu-1,2)
		DerivativeFactorFromIntervalStart * LinearBasisFunctionStartingOnInterval		// B'(mu,2): B(mu+1,1) = 0 here
	};
}

std::vector<float> makeClampedUniformQuadraticKnotVector(int ControlPointCount)
{
	ControlPointCount = std::max(ControlPointCount, c_QuadraticDegree + 1);
	const int LastKnotValue = ControlPointCount - c_QuadraticDegree;	// n - 2

	std::vector<float> KnotVector;
	KnotVector.reserve(static_cast<std::size_t>(ControlPointCount + c_QuadraticDegree + 1));	// n + d + 1 knots
	KnotVector.insert(KnotVector.end(), { 0.0f, 0.0f });
	for (int CurrentKnotValue{ 0 }; CurrentKnotValue <= LastKnotValue; ++CurrentKnotValue)
	{
		KnotVector.push_back(static_cast<float>(CurrentKnotValue));
	}
	KnotVector.insert(KnotVector.end(), { static_cast<float>(LastKnotValue), static_cast<float>(LastKnotValue) });
	return KnotVector;
}

float calculateQuadraticGrevilleAbscissa(const std::vector<float>& KnotVector, int ControlPointIndex) noexcept
{
	return 0.5f * (getKnot(KnotVector, ControlPointIndex + 1) + getKnot(KnotVector, ControlPointIndex + 2));
}

}
