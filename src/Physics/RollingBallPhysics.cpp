#include "Hephaestian/Physics/RollingBallPhysics.h"

#include <algorithm>
#include <cmath>

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Physics
{

namespace
{
	constexpr glm::vec3 c_WorldUpDirection{ 0.0f, 1.0f, 0.0f };

	// Places the centre exactly one radius above the triangle's plane, along its normal (book 9.8.5 "Correcting the position")
	[[nodiscard]] glm::vec3 correctPositionOntoTrianglePlane(const glm::vec3& BallCentre, const TriangleSurface& Surface, int TriangleIndex, float BallRadius) noexcept
	{
		const glm::vec3& PointOnPlane = Surface.getTriangleVertex(TriangleIndex, 0);
		const glm::vec3& PlaneNormal = Surface.getTriangleNormal(TriangleIndex);
		const float CurrentDistanceAbovePlane = glm::dot(BallCentre - PointOnPlane, PlaneNormal);	// Book: y = ((P - P0) . n)
		return BallCentre + PlaneNormal * (BallRadius - CurrentDistanceAbovePlane);					// Move back d = r - y along n
	}
}

glm::vec3 calculateGravityAccelerationAlongPlane(const glm::vec3& UnitPlaneNormal, float GravitationalAcceleration) noexcept
{
	// Book (z up):  a = (1/m)(N + G) = [nx, ny, nz] * g * nz + [0, 0, -g] = g [nx nz, ny nz, nz^2 - 1]
	// Here y is up, so "nz" is the component along up: n . up = ny, and G = -g * up
	const float CosineOfSlopeAngle = glm::dot(UnitPlaneNormal, c_WorldUpDirection);	// Book eq. 9.13: cos(alpha) = n . k
	return GravitationalAcceleration * (UnitPlaneNormal * CosineOfSlopeAngle - c_WorldUpDirection);
}

glm::vec3 redirectVelocityOntoNewTriangle(const glm::vec3& Velocity, const glm::vec3& PreviousUnitNormal, const glm::vec3& NewUnitNormal) noexcept
{
	const glm::vec3 NormalSum = PreviousUnitNormal + NewUnitNormal;
	const float NormalSumLength = glm::length(NormalSum);
	if (NormalSumLength < 1e-6f)
	{
		return Velocity;	// The triangles face opposite ways: not a surface a ball can roll across
	}
	const glm::vec3 CollisionPlaneNormal = NormalSum / NormalSumLength;					// Book eq. 9.29: x = (m + n) / |m + n|
	return Velocity - 2.0f * glm::dot(Velocity, CollisionPlaneNormal) * CollisionPlaneNormal;	// Book eq. 9.18: v - 2 (v . n) n
}

void RollingBallPhysicsWorld::advanceSimulation(entt::registry& Registry, float RealDeltaTimeInSeconds)
{
	m_StepsTakenLastFrame = 0;
	if (m_Settings.m_IsPaused || m_Surface == nullptr || m_Surface->isEmpty())
	{
		m_AccumulatedUnsimulatedTimeInSeconds = 0.0f;
		return;
	}

	const float FixedTimeStep = std::max(m_Settings.m_FixedTimeStepInSeconds, 1e-4f);
	m_AccumulatedUnsimulatedTimeInSeconds += std::max(RealDeltaTimeInSeconds, 0.0f);
	while (m_AccumulatedUnsimulatedTimeInSeconds >= FixedTimeStep && m_StepsTakenLastFrame < m_Settings.m_MaximumStepsPerFrame)
	{
		stepAllBallsOnce(Registry, FixedTimeStep);
		m_AccumulatedUnsimulatedTimeInSeconds -= FixedTimeStep;
		++m_StepsTakenLastFrame;
	}
	if (m_StepsTakenLastFrame == m_Settings.m_MaximumStepsPerFrame)
	{
		m_AccumulatedUnsimulatedTimeInSeconds = std::min(m_AccumulatedUnsimulatedTimeInSeconds, FixedTimeStep);	// Drop the backlog
	}
}

void RollingBallPhysicsWorld::stepAllBallsOnce(entt::registry& Registry, float TimeStepInSeconds)
{
	if (m_Surface == nullptr || m_Surface->isEmpty())
	{
		return;
	}
	const TriangleSurface& Surface = *m_Surface;
	const float GravitationalAcceleration = m_Settings.m_GravitationalAccelerationInMetresPerSecondSquared;
	const float FrictionCoefficient = std::max(m_Settings.m_FrictionCoefficient, 0.0f);

	auto BallEntities = Registry.view<Components::TransformComponent, RollingBallComponent>();
	BallEntities.each([&](Components::TransformComponent& BallTransform, RollingBallComponent& Ball)
	{
		if (!Ball.m_IsOnSurface)
		{
			return;
		}

		// 1. WHICH TRIANGLE: the contact point (one radius below the centre, along the normal) is what touches the surface.
		// The book looks up the centre itself, which drifts half a cell off on the quarry's steep walls.
		const glm::vec3 ContactPoint = BallTransform.m_LocalPosition - Ball.m_CurrentTriangleNormal * Ball.m_RadiusInMetres;
		const int TriangleIndex = Surface.findTriangleContainingPoint({ ContactPoint.x, ContactPoint.z }, Ball.m_CurrentTriangleIndex);
		if (TriangleIndex == TriangleSurface::c_NoTriangle)
		{
			Ball.m_IsOnSurface = false;
			Ball.m_VelocityInMetresPerSecond = glm::vec3(0.0f);
			return;
		}

		// 2. NORMAL of that triangle
		const glm::vec3& TriangleNormal = Surface.getTriangleNormal(TriangleIndex);
		glm::vec3 Velocity = Ball.m_VelocityInMetresPerSecond;

		// New triangle: the velocity has to follow the new slope (book 9.8.3 - 9.8.4)
		if (Ball.m_CurrentTriangleIndex != TriangleSurface::c_NoTriangle && TriangleIndex != Ball.m_CurrentTriangleIndex)
		{
			if (glm::dot(Ball.m_CurrentTriangleNormal, TriangleNormal) < 0.999999f)
			{
				Velocity = redirectVelocityOntoNewTriangle(Velocity, Ball.m_CurrentTriangleNormal, TriangleNormal);
			}
			++Ball.m_TriangleTransitionCount;
		}
		Velocity -= glm::dot(Velocity, TriangleNormal) * TriangleNormal;	// Strictly tangent: removes floating point drift

		// 3. ACCELERATION (eq. 9.14), then 4. VELOCITY (eq. 9.16)
		const glm::vec3 GravityAcceleration = calculateGravityAccelerationAlongPlane(TriangleNormal, GravitationalAcceleration);
		glm::vec3 NewVelocity = Velocity + GravityAcceleration * TimeStepInSeconds;

		// Friction: |N| / m = g * cos(alpha), so it slows the ball by mu * g * cos(alpha) every second. Clamping the speed at 0
		// (instead of letting friction push backwards) also gives static friction: on a shallow enough slope the ball stays put.
		if (FrictionCoefficient > 0.0f)
		{
			const float SpeedBeforeFriction = glm::length(NewVelocity);
			const float FrictionSpeedLoss = FrictionCoefficient * GravitationalAcceleration * glm::dot(TriangleNormal, c_WorldUpDirection) * TimeStepInSeconds;
			NewVelocity = SpeedBeforeFriction > FrictionSpeedLoss ? NewVelocity * ((SpeedBeforeFriction - FrictionSpeedLoss) / SpeedBeforeFriction) : glm::vec3(0.0f);
		}

		// 5. POSITION (eq. 9.17 with the average velocity, which the book shows equals p + v t + a t^2 / 2)
		glm::vec3 NewPosition = BallTransform.m_LocalPosition + 0.5f * (Velocity + NewVelocity) * TimeStepInSeconds;

		// Keep the ball ON the surface where it ends up (it never flies): one radius above whichever triangle is now underneath
		const glm::vec3 NewContactPoint = NewPosition - TriangleNormal * Ball.m_RadiusInMetres;
		const int TriangleIndexAtNewPosition = Surface.findTriangleContainingPoint({ NewContactPoint.x, NewContactPoint.z }, TriangleIndex);
		if (TriangleIndexAtNewPosition == TriangleSurface::c_NoTriangle)
		{
			// Rolled off the edge of the scan: stop right at the edge rather than hovering over nothing
			Ball.m_IsOnSurface = false;
			Ball.m_VelocityInMetresPerSecond = glm::vec3(0.0f);
			BallTransform.m_LocalPosition = correctPositionOntoTrianglePlane(BallTransform.m_LocalPosition, Surface, TriangleIndex, Ball.m_RadiusInMetres);
			return;
		}
		NewPosition = correctPositionOntoTrianglePlane(NewPosition, Surface, TriangleIndexAtNewPosition, Ball.m_RadiusInMetres);

		// 6./7. ROTATION (visual): axis n x v (eq. 9.11), angle = distance / radius (eq. 9.10)
		if (m_Settings.m_ShouldRotateBallsVisually)
		{
			const glm::vec3 RotationAxis = glm::cross(TriangleNormal, NewVelocity);
			const float RotationAxisLength = glm::length(RotationAxis);
			if (RotationAxisLength > 1e-6f && Ball.m_RadiusInMetres > 0.0f)
			{
				const float RotationAngleInRadians = glm::length(NewVelocity) * TimeStepInSeconds / Ball.m_RadiusInMetres;
				BallTransform.m_LocalRotation = glm::normalize(glm::angleAxis(RotationAngleInRadians, RotationAxis / RotationAxisLength) * BallTransform.m_LocalRotation);
			}
		}

		// The triangle we computed THIS step's physics on: if the ball now sits on another one, next step detects the change
		BallTransform.m_LocalPosition = NewPosition;
		Ball.m_VelocityInMetresPerSecond = NewVelocity;
		Ball.m_CurrentTriangleIndex = TriangleIndex;
		Ball.m_CurrentTriangleNormal = TriangleNormal;
	});

	m_SimulatedTimeInSeconds += static_cast<double>(TimeStepInSeconds);
}

bool RollingBallPhysicsWorld::placeBallOnSurface(entt::registry& Registry, entt::entity BallEntity, const glm::vec2& WorldXZ) const
{
	if (m_Surface == nullptr || !Registry.valid(BallEntity) || !Registry.all_of<Components::TransformComponent, RollingBallComponent>(BallEntity))
	{
		return false;
	}
	const int TriangleIndex = m_Surface->findTriangleContainingPoint(WorldXZ);
	if (TriangleIndex == TriangleSurface::c_NoTriangle)
	{
		return false;
	}

	auto& Ball = Registry.get<RollingBallComponent>(BallEntity);
	auto& BallTransform = Registry.get<Components::TransformComponent>(BallEntity);
	const glm::vec3 PointOnSurface{ WorldXZ.x, m_Surface->calculateHeightOnTriangle(TriangleIndex, WorldXZ), WorldXZ.y };

	BallTransform.m_LocalPosition = PointOnSurface + m_Surface->getTriangleNormal(TriangleIndex) * Ball.m_RadiusInMetres;
	BallTransform.m_LocalScale = glm::vec3(Ball.m_RadiusInMetres);	// The ball mesh has radius 1
	Ball.m_VelocityInMetresPerSecond = glm::vec3(0.0f);
	Ball.m_CurrentTriangleIndex = TriangleIndex;
	Ball.m_CurrentTriangleNormal = m_Surface->getTriangleNormal(TriangleIndex);
	Ball.m_IsOnSurface = true;
	Ball.m_TriangleTransitionCount = 0;
	return true;
}

}
