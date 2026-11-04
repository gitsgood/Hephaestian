// Unit tests for the MAT301 maths: every expected value below is a hand calculation from the lecture notes.
// No framework on purpose: run the exe, read the list, non-zero exit code if anything failed.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Hephaestian/ECS/Components.h"
#include "Hephaestian/Physics/RollingBallPhysics.h"
#include "Hephaestian/Physics/TriangleSurface.h"
#include "Hephaestian/Splines/BiquadraticBSplineSurface.h"
#include "Hephaestian/Splines/QuadraticBSplineBasis.h"
#include "Hephaestian/Splines/QuadraticBSplineCurve.h"
#include "Hephaestian/Terrain/GroundHeightfield.h"

namespace
{
	int g_PassedCheckCount{ 0 };
	int g_FailedCheckCount{ 0 };

	void checkCondition(bool Condition, std::string_view CheckDescription)
	{
		(Condition ? g_PassedCheckCount : g_FailedCheckCount)++;
		std::printf("  [%s] %.*s\n", Condition ? " OK " : "FAIL", static_cast<int>(CheckDescription.size()), CheckDescription.data());
	}

	void checkNear(float Actual, float Expected, std::string_view CheckDescription, float Tolerance = 1e-5f)
	{
		const bool IsNear = std::abs(Actual - Expected) <= Tolerance;
		(IsNear ? g_PassedCheckCount : g_FailedCheckCount)++;
		std::printf("  [%s] %.*s: %.6f (expected %.6f)\n", IsNear ? " OK " : "FAIL", static_cast<int>(CheckDescription.size()), CheckDescription.data(), Actual, Expected);
	}

	void checkNear(const glm::vec3& Actual, const glm::vec3& Expected, std::string_view CheckDescription, float Tolerance = 1e-5f)
	{
		const bool IsNear = glm::all(glm::lessThanEqual(glm::abs(Actual - Expected), glm::vec3(Tolerance)));
		(IsNear ? g_PassedCheckCount : g_FailedCheckCount)++;
		std::printf("  [%s] %.*s: (%.6f, %.6f, %.6f) (expected (%.6f, %.6f, %.6f))\n", IsNear ? " OK " : "FAIL", static_cast<int>(CheckDescription.size()), CheckDescription.data(),
			Actual.x, Actual.y, Actual.z, Expected.x, Expected.y, Expected.z);
	}

	void testKnotIntervalsAndBasisFunctions()
	{
		using namespace Hephaestian::Splines;
		std::printf("Knot intervals and quadratic basis functions (sections 5.6.2, 7.5)\n");
		const std::vector<float> KnotVectorU{ 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 2.0f, 2.0f };	// n = 4
		const std::vector<float> KnotVectorV{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };			// n = 3

		checkCondition(findKnotIntervalContainingParameter(KnotVectorU, 2, 4, 1.5f) == 3, "t = 1.5 on {0,0,0,1,2,2,2} -> knot interval 3");
		checkCondition(findKnotIntervalContainingParameter(KnotVectorU, 2, 4, 2.0f) == 3, "t = 2.0 (end of the domain) -> knot interval 3");
		checkCondition(findKnotIntervalContainingParameter(KnotVectorU, 2, 4, 0.5f) == 2, "t = 0.5 -> knot interval 2");
		checkCondition(findKnotIntervalContainingParameter(KnotVectorV, 2, 3, 2.0f / 3.0f) == 2, "t = 2/3 on {0,0,0,1,1,1} -> knot interval 2");

		checkNear(calculateActiveQuadraticBasisFunctions(KnotVectorU, 3, 4.0f / 3.0f), { 2.0f / 9.0f, 6.0f / 9.0f, 1.0f / 9.0f }, "B(1,2), B(2,2), B(3,2) at u = 4/3");
		checkNear(calculateActiveQuadraticBasisFunctions(KnotVectorV, 2, 2.0f / 3.0f), { 1.0f / 9.0f, 4.0f / 9.0f, 4.0f / 9.0f }, "B(0,2), B(1,2), B(2,2) at v = 2/3");
		checkNear(calculateActiveQuadraticBasisFunctions(KnotVectorU, 3, 2.0f), { 0.0f, 0.0f, 1.0f }, "only the last basis function is 1 at the domain's end");

		checkNear(calculateActiveQuadraticBasisFunctionDerivatives(KnotVectorU, 3, 4.0f / 3.0f), { -2.0f / 3.0f, 0.0f, 2.0f / 3.0f }, "basis derivatives at u = 4/3 (section 12.4.1)");
		checkNear(calculateActiveQuadraticBasisFunctionDerivatives(KnotVectorV, 2, 2.0f / 3.0f), { -2.0f / 3.0f, -2.0f / 3.0f, 4.0f / 3.0f }, "basis derivatives at v = 2/3 (section 12.4.1)");

		checkCondition(makeClampedUniformQuadraticKnotVector(4) == KnotVectorU, "clamped uniform knot vector for 4 control points = {0,0,0,1,2,2,2}");
		checkNear(calculateQuadraticGrevilleAbscissa(KnotVectorU, 0), 0.0f, "Greville abscissa of c0");
		checkNear(calculateQuadraticGrevilleAbscissa(KnotVectorU, 1), 0.5f, "Greville abscissa of c1");
		checkNear(calculateQuadraticGrevilleAbscissa(KnotVectorU, 3), 2.0f, "Greville abscissa of the last control point");
	}

	void testBiquadraticSurface()
	{
		using namespace Hephaestian::Splines;
		std::printf("Bi-quadratic B-spline surface (sections 12.2.1, 12.2.2, 12.4.1)\n");
		const BiquadraticBSplineSurface BookSurface;	// Listing 12.2's test data

		const auto [BasisFunctionsU, BasisFunctionsV] = BookSurface.calculateActiveBasisFunctionsInBothDirections(4.0f / 3.0f, 2.0f / 3.0f, 3, 2);
		checkNear(BasisFunctionsU, { 2.0f / 9.0f, 6.0f / 9.0f, 1.0f / 9.0f }, "basis functions along u at (4/3, 2/3)");
		checkNear(BasisFunctionsV, { 1.0f / 9.0f, 4.0f / 9.0f, 4.0f / 9.0f }, "basis functions along v at (4/3, 2/3)");
		checkNear(BookSurface.sumActiveControlPointsWeightedByBasisFunctions(3, 2, BasisFunctionsU, BasisFunctionsV), { 153.0f / 81.0f, 108.0f / 81.0f, 64.0f / 81.0f }, "f(4/3, 2/3) = (153/81, 108/81, 64/81)");

		const auto [PartialDerivativeU, PartialDerivativeV] = BookSurface.evaluatePartialDerivatives(4.0f / 3.0f, 2.0f / 3.0f);
		// The notes print df/du = (1, 0, -4/9), but summing their own weight table (12.4.1) gives (4/3, 0, -16/27): the same
		// direction, scaled by 3/4, just like they scale df/dv to (0, 1, -16/27) a few lines earlier. Check both.
		checkNear(PartialDerivativeU, { 4.0f / 3.0f, 0.0f, -16.0f / 27.0f }, "df/du(4/3, 2/3) = (4/3, 0, -16/27), the unscaled sum of the weight table");
		checkNear(glm::normalize(PartialDerivativeU), glm::normalize(glm::vec3(1.0f, 0.0f, -4.0f / 9.0f)), "df/du is parallel to the notes' scaled (1, 0, -4/9)");
		checkNear(PartialDerivativeV, { 0.0f, 2.0f, -32.0f / 27.0f }, "df/dv(4/3, 2/3) = (0, 2, -32/27)");

		std::vector<glm::vec3> VertexPositions;
		std::vector<std::uint32_t> TriangleIndices;
		BookSurface.tessellateSurfaceIntoTriangles(0.1f, VertexPositions, TriangleIndices);
		checkCondition(VertexPositions.size() == 21u * 11u, "tessellateSurfaceIntoTriangles(step 0.1): 21 x 11 points");
		checkCondition(TriangleIndices.size() == 20u * 10u * 6u, "tessellateSurfaceIntoTriangles(step 0.1): 20 x 10 quads, 2 triangles each");
		checkNear(VertexPositions.front(), { 0.0f, 0.0f, 0.0f }, "the surface starts at the first control point (clamped)");
		checkNear(VertexPositions.back(), { 3.0f, 2.0f, 0.0f }, "the surface ends at the last control point (clamped)");
	}

	void testQuadraticCurve()
	{
		using namespace Hephaestian::Splines;
		std::printf("Quadratic B-spline curve in the UV plane\n");
		QuadraticBSplineCurve Curve;
		Curve.setControlPoints({ { 1.0f, 1.0f }, { 3.0f, 3.0f }, { 4.0f, 1.0f } });	// Knots {0,0,0,1,1,1}: the quadratic Bezier of section 7.4
		const glm::vec2 MiddlePoint = Curve.evaluateCurvePoint(0.5f);
		checkNear(glm::vec3(MiddlePoint, 0.0f), { 11.0f / 4.0f, 2.0f, 0.0f }, "f(1/2) = (11/4, 2)");
		checkNear(glm::vec3(Curve.evaluateCurvePoint(0.0f), 0.0f), { 1.0f, 1.0f, 0.0f }, "starts at the first control point");
		checkNear(glm::vec3(Curve.evaluateCurvePoint(1.0f), 0.0f), { 4.0f, 1.0f, 0.0f }, "ends at the last control point");
	}

	void testRollingBallPhysics()
	{
		using namespace Hephaestian;
		std::printf("Rolling ball (chapter 9)\n");
		constexpr float GravitationalAcceleration{ 9.81f };

		// eq. 9.14 on a 30 degree incline: |a| = g sin(30) = g/2, tangent to the plane, pointing downhill
		const float SlopeAngle = glm::radians(30.0f);
		const glm::vec3 InclineNormal{ -std::sin(SlopeAngle), std::cos(SlopeAngle), 0.0f };	// Rises towards +x
		const glm::vec3 Acceleration = Physics::calculateGravityAccelerationAlongPlane(InclineNormal, GravitationalAcceleration);
		checkNear(glm::length(Acceleration), GravitationalAcceleration * 0.5f, "|a| = g sin(30 degrees)");
		checkNear(glm::dot(Acceleration, InclineNormal), 0.0f, "a is tangent to the plane (a . n = 0)");
		checkCondition(Acceleration.x < 0.0f && Acceleration.y < 0.0f, "a points downhill");
		checkNear(Physics::calculateGravityAccelerationAlongPlane({ 0.0f, 1.0f, 0.0f }, GravitationalAcceleration), glm::vec3(0.0f), "a = 0 on flat ground");

		// eqs. 9.29 + 9.18: flat ground onto a 30 degree ramp keeps the speed and follows the ramp
		const glm::vec3 RedirectedVelocity = Physics::redirectVelocityOntoNewTriangle({ 2.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, InclineNormal);
		checkNear(glm::length(RedirectedVelocity), 2.0f, "speed is kept across the crease");
		checkNear(glm::dot(RedirectedVelocity, InclineNormal), 0.0f, "new velocity lies in the ramp's plane");
		checkNear(RedirectedVelocity, { 2.0f * std::cos(SlopeAngle), 2.0f * std::sin(SlopeAngle), 0.0f }, "new velocity goes up the ramp");

		// A big incline made of two triangles: from rest, s(t) = 1/2 g sin(alpha) t^2 (exact here: constant a, averaged velocity)
		Physics::TriangleSurface Incline;
		const float HeightAtFarEnd = 100.0f * std::tan(SlopeAngle);
		Incline.buildFromTriangles({ { 0.0f, HeightAtFarEnd, -50.0f }, { 0.0f, HeightAtFarEnd, 50.0f }, { -100.0f, 0.0f, 50.0f }, { -100.0f, 0.0f, -50.0f } }, { 0, 1, 2, 0, 2, 3 });
		checkCondition(Incline.getTriangleNeighbours(0)[1] == 1 || Incline.getTriangleNeighbours(0)[0] == 1 || Incline.getTriangleNeighbours(0)[2] == 1, "the two triangles are neighbours");

		entt::registry Registry;
		const entt::entity BallEntity = Registry.create();
		Registry.emplace<Components::TransformComponent>(BallEntity);
		Registry.emplace<Physics::RollingBallComponent>(BallEntity).m_RadiusInMetres = 0.1f;

		Physics::RollingBallPhysicsWorld PhysicsWorld;
		PhysicsWorld.getSettings().m_FrictionCoefficient = 0.0f;
		PhysicsWorld.getSettings().m_GravitationalAccelerationInMetresPerSecondSquared = GravitationalAcceleration;
		PhysicsWorld.setSurface(&Incline);
		checkCondition(PhysicsWorld.placeBallOnSurface(Registry, BallEntity, { -10.0f, 0.0f }), "ball placed on the incline");
		const glm::vec3 StartPosition = Registry.get<Components::TransformComponent>(BallEntity).m_LocalPosition;

		constexpr float TimeStep{ 1.0f / 120.0f };
		constexpr int StepCount{ 120 };	// 1 second
		for (int CurrentStep{ 0 }; CurrentStep < StepCount; ++CurrentStep)
		{
			PhysicsWorld.stepAllBallsOnce(Registry, TimeStep);
		}
		const float DistanceTravelled = glm::distance(Registry.get<Components::TransformComponent>(BallEntity).m_LocalPosition, StartPosition);
		checkNear(DistanceTravelled, 0.5f * GravitationalAcceleration * std::sin(SlopeAngle) * 1.0f, "s(1 s) = 1/2 g sin(30) t^2", 1e-3f);
		checkNear(glm::length(Registry.get<Physics::RollingBallComponent>(BallEntity).m_VelocityInMetresPerSecond), GravitationalAcceleration * std::sin(SlopeAngle), "v(1 s) = g sin(30) t", 1e-3f);

		// Static friction: mu > tan(alpha) keeps a ball at rest on the incline
		PhysicsWorld.getSettings().m_FrictionCoefficient = 0.7f;	// tan(30) = 0.577
		PhysicsWorld.placeBallOnSurface(Registry, BallEntity, { -10.0f, 0.0f });
		const glm::vec3 RestingPosition = Registry.get<Components::TransformComponent>(BallEntity).m_LocalPosition;
		for (int CurrentStep{ 0 }; CurrentStep < StepCount; ++CurrentStep)
		{
			PhysicsWorld.stepAllBallsOnce(Registry, TimeStep);
		}
		checkNear(glm::distance(Registry.get<Components::TransformComponent>(BallEntity).m_LocalPosition, RestingPosition), 0.0f, "mu = 0.7 > tan(30): the ball stays put");
	}

	void testTriangleSurfaceSearch()
	{
		using namespace Hephaestian::Physics;
		std::printf("Triangle surface: neighbours and topological search (sections 6.2.6, 6.2.7)\n");

		// A 10 x 10 regular grid of 1 m cells, gently sloping
		constexpr int GridVertexCountPerSide{ 11 };
		std::vector<glm::vec3> VertexPositions;
		std::vector<std::uint32_t> TriangleIndices;
		for (int GridRowIndex{ 0 }; GridRowIndex < GridVertexCountPerSide; ++GridRowIndex)
		{
			for (int GridColumnIndex{ 0 }; GridColumnIndex < GridVertexCountPerSide; ++GridColumnIndex)
			{
				VertexPositions.emplace_back(static_cast<float>(GridColumnIndex), 0.1f * static_cast<float>(GridColumnIndex + GridRowIndex), static_cast<float>(GridRowIndex));
			}
		}
		for (int GridRowIndex{ 0 }; GridRowIndex + 1 < GridVertexCountPerSide; ++GridRowIndex)
		{
			for (int GridColumnIndex{ 0 }; GridColumnIndex + 1 < GridVertexCountPerSide; ++GridColumnIndex)
			{
				const std::uint32_t BottomLeftVertex = static_cast<std::uint32_t>(GridRowIndex * GridVertexCountPerSide + GridColumnIndex);
				const std::uint32_t BottomRightVertex = BottomLeftVertex + 1;
				const std::uint32_t TopLeftVertex = BottomLeftVertex + GridVertexCountPerSide;
				const std::uint32_t TopRightVertex = TopLeftVertex + 1;
				TriangleIndices.insert(TriangleIndices.end(), { BottomLeftVertex, BottomRightVertex, TopRightVertex, BottomLeftVertex, TopRightVertex, TopLeftVertex });
			}
		}
		TriangleSurface Grid;
		Grid.buildFromTriangles(VertexPositions, TriangleIndices);
		checkCondition(Grid.getTriangleCount() == 200u, "200 triangles");

		bool AreAllNormalsUp = true;
		for (int CurrentTriangle{ 0 }; CurrentTriangle < 200; ++CurrentTriangle) { AreAllNormalsUp = AreAllNormalsUp && Grid.getTriangleNormal(CurrentTriangle).y > 0.0f; }
		checkCondition(AreAllNormalsUp, "every normal points up");

		const int FoundFromColdStart = Grid.findTriangleContainingPoint({ 7.3f, 2.6f });
		const int FoundByWalkingFromAFarCorner = Grid.findTriangleContainingPoint({ 7.3f, 2.6f }, 0);
		checkCondition(FoundFromColdStart != TriangleSurface::c_NoTriangle && FoundFromColdStart == FoundByWalkingFromAFarCorner, "cold start and a long walk find the same triangle");
		const glm::vec3 Barycentric = Grid.calculateBarycentricCoordinatesXZ(FoundFromColdStart, { 7.3f, 2.6f });
		checkCondition(Barycentric.x >= 0.0f && Barycentric.y >= 0.0f && Barycentric.z >= 0.0f, "the point is inside it (all barycentric coordinates >= 0)");
		checkNear(Grid.calculateHeightOnTriangle(FoundFromColdStart, { 7.3f, 2.6f }), 0.1f * (7.3f + 2.6f), "height on the plane z = 0.1 (x + z)");
		checkCondition(Grid.findTriangleContainingPoint({ 15.0f, 5.0f }, FoundFromColdStart) == TriangleSurface::c_NoTriangle, "a point off the surface finds nothing");
	}
}

// "HephaestianMathTests <point cloud .obj>": runs the terrain pipeline on a real file and times every stage
int runTerrainPipelineBenchmark(const char* PointCloudFilePath)
{
	using namespace Hephaestian;
	using Clock = std::chrono::steady_clock;
	auto calculateMillisecondsSince = [](Clock::time_point StartTime) { return std::chrono::duration<double, std::milli>(Clock::now() - StartTime).count(); };

	auto StageStartTime = Clock::now();
	const std::vector<glm::vec3> PointPositions = Terrain::loadPointPositionsFromObjFile(PointCloudFilePath);
	std::printf("read %zu points: %.1f ms\n", PointPositions.size(), calculateMillisecondsSince(StageStartTime));
	std::fflush(stdout);

	StageStartTime = Clock::now();
	const Terrain::GroundPlaneFrame Frame = Terrain::computeMinimumAreaBoundingFrame(PointPositions);
	std::printf("minimum-area frame %.1f x %.1f m, axis U (%.3f, %.3f): %.1f ms\n", Frame.m_LengthAlongU, Frame.m_LengthAlongV, Frame.m_AxisUXZ.x, Frame.m_AxisUXZ.y, calculateMillisecondsSince(StageStartTime));
	std::fflush(stdout);

	StageStartTime = Clock::now();
	const Terrain::GroundHeightfield Heightfield = Terrain::GroundHeightfield::buildFromPointCloud(PointPositions, {});
	const Terrain::GroundHeightfieldStatistics& Statistics = Heightfield.getStatistics();
	std::printf("heightfield %d x %d: %.1f ms (cells with points %zu, canopy removed %zu, low noise removed %zu, holes filled %zu, exterior %zu)\n",
		Heightfield.getSampleCountAlongU(), Heightfield.getSampleCountAlongV(), calculateMillisecondsSince(StageStartTime),
		Statistics.m_CellsWithPoints, Statistics.m_CanopyCellsRemoved, Statistics.m_LowNoiseCellsRemoved, Statistics.m_HoleCellsFilled, Statistics.m_ExteriorCells);
	std::fflush(stdout);

	StageStartTime = Clock::now();
	std::vector<glm::vec3> VertexPositions;
	std::vector<std::uint32_t> TriangleIndices;
	Heightfield.buildGridTriangulation(VertexPositions, TriangleIndices);
	Physics::TriangleSurface GroundSurface;
	GroundSurface.buildFromTriangles(std::move(VertexPositions), std::move(TriangleIndices));
	std::printf("ground triangulation + neighbours: %zu triangles, %.1f ms\n", GroundSurface.getTriangleCount(), calculateMillisecondsSince(StageStartTime));
	std::fflush(stdout);

	// One million random lookups, walking from the previous result like a moving ball would
	StageStartTime = Clock::now();
	int PreviousTriangle = Physics::TriangleSurface::c_NoTriangle;
	std::size_t FoundCount{ 0 };
	for (int CurrentLookup{ 0 }; CurrentLookup < 1000000; ++CurrentLookup)
	{
		const float Fraction = static_cast<float>(CurrentLookup) / 1000000.0f;
		const glm::vec2 LocalPosition{ Frame.m_LengthAlongU * Fraction, Frame.m_LengthAlongV * (0.5f + 0.4f * std::sin(Fraction * 40.0f)) };
		PreviousTriangle = GroundSurface.findTriangleContainingPoint(Frame.convertLocalToWorldXZ(LocalPosition), PreviousTriangle);
		FoundCount += PreviousTriangle != Physics::TriangleSurface::c_NoTriangle;
	}
	std::printf("1M walking lookups along a path: %.1f ms (%zu found)\n", calculateMillisecondsSince(StageStartTime), FoundCount);
	return 0;
}

int main(int ArgumentCount, char** Arguments)
{
	if (ArgumentCount > 1)
	{
		return runTerrainPipelineBenchmark(Arguments[1]);
	}

	testKnotIntervalsAndBasisFunctions();
	testBiquadraticSurface();
	testQuadraticCurve();
	testRollingBallPhysics();
	testTriangleSurfaceSearch();

	std::printf("\n%d passed, %d failed\n", g_PassedCheckCount, g_FailedCheckCount);
	return g_FailedCheckCount == 0 ? 0 : 1;
}
