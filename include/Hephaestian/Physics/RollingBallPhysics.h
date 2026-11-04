#ifndef HEPHAESTIAN_ROLLINGBALLPHYSICS_H
#define HEPHAESTIAN_ROLLINGBALLPHYSICS_H

#include <cstdint>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "Hephaestian/Physics/TriangleSurface.h"

namespace Hephaestian::Physics
{

// A ball that rolls (well, slides: book eq. 9.14 has no rotational inertia) on a TriangleSurface, following Newton's 2nd law.
// The entity's TransformComponent::m_LocalPosition is the ball's CENTRE, so give ball entities no parent.
struct RollingBallComponent
{
	glm::vec3 m_VelocityInMetresPerSecond{ 0.0f };
	float m_RadiusInMetres{ 0.25f };
	float m_MassInKilograms{ 1.0f };	// a = F / m, and every force here is proportional to m, so it cancels: kept for ball-ball collisions later

	// Which triangle the ball was on last step, and its normal: needed to detect (and handle) crossing onto a new triangle
	int m_CurrentTriangleIndex{ TriangleSurface::c_NoTriangle };
	glm::vec3 m_CurrentTriangleNormal{ 0.0f, 1.0f, 0.0f };
	bool m_IsOnSurface{ true };			// False once it rolled off the edge of the surface: it then stays put (the book's balls never fly)
	std::uint64_t m_TriangleTransitionCount{ 0 };
};

struct RollingBallPhysicsSettings
{
	float m_GravitationalAccelerationInMetresPerSecondSquared{ 9.81f };
	float m_FrictionCoefficient{ 0.05f };		// Coulomb (kinetic and static) friction, mu: decelerates by mu * |N| / m
	float m_FixedTimeStepInSeconds{ 1.0f / 120.0f };	// Fixed, so results don't depend on the frame rate (and stay reproducible)
	int m_MaximumStepsPerFrame{ 12 };			// After a long hitch, drop time rather than spiral into ever more catch-up steps
	bool m_ShouldRotateBallsVisually{ true };	// Book eqs. 9.10 / 9.11, purely visual
	bool m_IsPaused{ false };
};

// Book eq. 9.14 for a y-up world: a = g * (n * (n . up) - up). It's always tangent to the plane (a . n = 0), with |a| = g * sin(angle).
[[nodiscard]] glm::vec3 calculateGravityAccelerationAlongPlane(const glm::vec3& UnitPlaneNormal, float GravitationalAcceleration) noexcept;

// Book eqs. 9.29 + 9.18: the normal of the plane halfway between the two triangles, and the velocity reflected in it.
// Reflecting in that bisecting plane maps the old triangle's plane exactly onto the new one, so the ball keeps its speed
// and simply follows the new slope.
[[nodiscard]] glm::vec3 redirectVelocityOntoNewTriangle(const glm::vec3& Velocity, const glm::vec3& PreviousUnitNormal, const glm::vec3& NewUnitNormal) noexcept;

/*
* Steps every entity with TransformComponent + RollingBallComponent on one TriangleSurface (book chapter 9.6 and Listing 9.3):
*	1. find the triangle under the ball (barycentric coordinates)	2. its normal
*	3. acceleration (eq. 9.14) + friction							4./5. Euler: velocity (9.16), position (9.17)
*	   on a new triangle: velocity redirected (9.29 + 9.18), position corrected along the new normal
*	6./7. rotation from n x v (9.11) and angle = distance / radius (9.10)
*/
class RollingBallPhysicsWorld
{
public:
	void setSurface(const TriangleSurface* SurfaceToRollOn) noexcept { m_Surface = SurfaceToRollOn; }
	[[nodiscard]] const TriangleSurface* getSurface() const noexcept { return m_Surface; }

	[[nodiscard]] RollingBallPhysicsSettings& getSettings() noexcept { return m_Settings; }
	[[nodiscard]] const RollingBallPhysicsSettings& getSettings() const noexcept { return m_Settings; }

	// Runs as many fixed steps as fit in the real time that passed (the rest carries over to the next frame)
	void advanceSimulation(entt::registry& Registry, float RealDeltaTimeInSeconds);

	// Exactly one fixed step of the given length, ignoring pause and the accumulator (tests, single-stepping)
	void stepAllBallsOnce(entt::registry& Registry, float TimeStepInSeconds);

	// Puts the ball at rest on the surface below the given XZ position. False if there's no surface there.
	bool placeBallOnSurface(entt::registry& Registry, entt::entity BallEntity, const glm::vec2& WorldXZ) const;

	[[nodiscard]] int getStepsTakenLastFrame() const noexcept { return m_StepsTakenLastFrame; }
	[[nodiscard]] double getSimulatedTimeInSeconds() const noexcept { return m_SimulatedTimeInSeconds; }

private:
	const TriangleSurface* m_Surface{ nullptr };
	RollingBallPhysicsSettings m_Settings;
	float m_AccumulatedUnsimulatedTimeInSeconds{ 0.0f };
	int m_StepsTakenLastFrame{ 0 };
	double m_SimulatedTimeInSeconds{ 0.0 };
};

}

#endif // !HEPHAESTIAN_ROLLINGBALLPHYSICS_H
