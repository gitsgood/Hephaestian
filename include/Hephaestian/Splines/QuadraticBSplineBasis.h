#ifndef HEPHAESTIAN_QUADRATICBSPLINEBASIS_H
#define HEPHAESTIAN_QUADRATICBSPLINEBASIS_H

#include <vector>

#include <glm/glm.hpp>

/*
* Quadratic B-spline basis functions, as in MAT301 chapters 5.5 - 5.6 and 7.5 - 7.7. Shared by the bi-quadratic surface
* (chapter 12) and the quadratic curve, so both use exactly the same maths.
*
* Book notation -> names used here:
*	t (knot vector)			-> KnotVector				n (control points) -> ControlPointCount		d -> Degree
*	mu / my (knot interval)	-> KnotIntervalIndex: the index with KnotVector[KnotIntervalIndex] <= x < KnotVector[KnotIntervalIndex + 1]
*	B(i,d) (basis function)	-> only B(mu-2,2), B(mu-1,2) and B(mu,2) are non-zero on that interval (local support), so the
*							   functions below return exactly those three, in that order, as a glm::vec3
*/
namespace Hephaestian::Splines
{

inline constexpr int c_QuadraticDegree{ 2 };

// Book Listing 7.4 ("findKnotInterval", with the extra test that also allows knot vectors without d+1 equal end knots):
//	start at the last control point's index, step down while the parameter is still left of that knot
[[nodiscard]] int findKnotIntervalContainingParameter(const std::vector<float>& KnotVector, int Degree, int ControlPointCount, float ParameterValue) noexcept;

// Book eq. 5.22, w(i,d)(t): how far the parameter has travelled from knot i to knot i+d, from 0 to 1 (0 if those knots coincide)
[[nodiscard]] float calculateKnotWeight(const std::vector<float>& KnotVector, int KnotIndex, int Degree, float ParameterValue) noexcept;

// The three active quadratic basis functions on the knot interval, { B(mu-2,2), B(mu-1,2), B(mu,2) }.
// Book eqs. 5.26 - 5.28 / fig. 5.17 (the book's w11, w12 and w22):
//	LinearWeightFromIntervalStart		= w(mu,1)		(book: w11)
//	QuadraticWeightFromPreviousKnot		= w(mu-1,2)		(book: w12)
//	QuadraticWeightFromIntervalStart	= w(mu,2)		(book: w22)
//
//			   1-w11       w11
//			  /    \      /    \     (a "\" can't end a // comment: it would continue onto the next line)
//		  1-w12    w12  1-w22   w22
//		    |        |    |      |
//		 B(mu-2,2)  B(mu-1,2)   B(mu,2)
//
//	B(mu-2,2) = (1-w11)(1-w12)		B(mu-1,2) = (1-w11) w12 + w11 (1-w22)		B(mu,2) = w11 w22
[[nodiscard]] glm::vec3 calculateActiveQuadraticBasisFunctions(const std::vector<float>& KnotVector, int KnotIntervalIndex, float ParameterValue) noexcept;

// Their derivatives, book eq. 7.7 with d = 2:  B'(i,2)(t) = 2/(t_(i+2) - t_i) B(i,1)(t) - 2/(t_(i+3) - t_(i+1)) B(i+1,1)(t)
// On the knot interval the only non-zero linear basis functions are B(mu-1,1) = 1 - w(mu,1) and B(mu,1) = w(mu,1).
[[nodiscard]] glm::vec3 calculateActiveQuadraticBasisFunctionDerivatives(const std::vector<float>& KnotVector, int KnotIntervalIndex, float ParameterValue) noexcept;

// Clamped ("d+1 equal knots at each end") uniform knot vector for n control points: {0,0,0, 1, 2, ..., n-2, n-2,n-2}.
// The parameter then runs from 0 to n-2, one unit per knot interval.
[[nodiscard]] std::vector<float> makeClampedUniformQuadraticKnotVector(int ControlPointCount);

// Greville abscissa of a control point: (t_(i+1) + t_(i+2)) / 2. Placing control points AT these parameter values makes
// the spline reproduce a straight line exactly, which keeps the surface's parameter -> ground plane mapping linear.
[[nodiscard]] float calculateQuadraticGrevilleAbscissa(const std::vector<float>& KnotVector, int ControlPointIndex) noexcept;

}

#endif // !HEPHAESTIAN_QUADRATICBSPLINEBASIS_H
