#ifndef HEPHAESTIAN_QUARRYROLLINGBALLDEMO_H
#define HEPHAESTIAN_QUARRYROLLINGBALLDEMO_H

#include <cstdint>
#include <filesystem>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/ECS/Scene.h"
#include "Hephaestian/Physics/RollingBallPhysics.h"
#include "Hephaestian/Physics/TriangleSurface.h"
#include "Hephaestian/Splines/BiquadraticBSplineSurface.h"
#include "Hephaestian/Splines/QuadraticBSplineCurve.h"
#include "Hephaestian/Terrain/GroundHeightfield.h"

namespace Hephaestian::Simulation
{

enum class RollingSurfaceChoice : int
{
	GroundTriangulation = 0,	// The regular grid triangulation of the filtered LiDAR ground (task 1)
	BSplineSurface = 1			// The bi-quadratic B-spline surface built on top of it (task 2)
};

/*
* Everything for the MAT301 quarry exercise in one place:
*	- LiDAR point cloud -> bare-earth heightfield -> regular triangulation (the surface the ball rolls on)
*	- the same heightfield as the control grid of a bi-quadratic B-spline surface (chapter 12)
*	- balls rolling by Newton's 2nd law (chapter 9) on either surface
*	- the tracked ball's path recorded in the UV plane and drawn as a quadratic B-spline curve on the surface
*
* The UV plane: the ground grid is aligned with the scan's own rectangle (GroundPlaneFrame), and the B-spline's control
* points sit at their Greville abscissae, so surface parameter (u, v) maps LINEARLY onto that rectangle:
*	local ground position (s, t) = (u, v) * m_BSplineControlPointSpacingInMetres
*/
class QuarryRollingBallDemo
{
public:
	QuarryRollingBallDemo(Vulkanised::VulkanRenderer& Renderer, Scene& GameScene);

	// Loads and processes the point cloud, creates both surfaces, their meshes and one ball. False if it couldn't.
	bool loadTerrain(const std::filesystem::path& PointCloudFilePath);
	[[nodiscard]] bool isTerrainLoaded() const noexcept { return !m_GroundSurface.isEmpty(); }

	// Physics + path recording. Call before Scene::updateSimulation, so the transform hierarchy sees this frame's positions.
	void update(float DeltaTimeInSeconds);

	// The "Physics" ImGui window (between beginImGuiFrame and SubmitFrame)
	void drawPhysicsWindow(const glm::vec3& CameraWorldPosition);

	// Drops a new ball at rest on the active surface below this XZ position, and starts tracking its path
	bool dropBallAt(const glm::vec2& WorldXZ);

	// A camera position high above the quarry, looking down into it
	void calculateSuggestedCameraPlacement(glm::vec3& OutCameraPosition, glm::quat& OutCameraRotation) const;

	void setRollingSurface(RollingSurfaceChoice NewRollingSurface);

	// Chase camera behind the tracked ball. The player camera is what it hands back to when switched off.
	void setPlayerCameraEntity(entt::entity PlayerCameraEntity) noexcept { m_PlayerCameraEntity = PlayerCameraEntity; }
	void setCameraFollowsTrackedBall(bool ShouldCameraFollowTrackedBall);

private:
	void updateFollowCamera(float DeltaTimeInSeconds);
	void createRenderMeshes();
	[[nodiscard]] std::uint32_t createModelFromTriangleSurface(const Physics::TriangleSurface& Surface, const glm::vec3& LowColour, const glm::vec3& HighColour);
	[[nodiscard]] std::uint32_t createSphereModel(int SegmentCountAroundEquator, int RingCount, const glm::vec3& FirstStripeColour, const glm::vec3& SecondStripeColour);
	[[nodiscard]] const Physics::TriangleSurface& getActiveSurface() const noexcept;
	[[nodiscard]] glm::vec2 findInterestingStartPositionXZ() const;
	void removeAllBalls();

	// UV plane <-> world
	[[nodiscard]] glm::vec2 convertWorldXZToSurfaceParameter(const glm::vec2& WorldXZ) const noexcept;
	[[nodiscard]] glm::vec2 convertSurfaceParameterToWorldXZ(const glm::vec2& SurfaceParameter) const noexcept;

	// Path of the tracked ball, as a quadratic B-spline in the UV plane
	void recordTrackedBallPath();
	void rebuildPathTrailBeads();
	void clearPath();

	Vulkanised::VulkanRenderer& m_Renderer;
	Scene& m_Scene;

	// Terrain + surfaces
	Terrain::GroundHeightfield m_GroundHeightfield;
	Physics::TriangleSurface m_GroundSurface;
	Splines::BiquadraticBSplineSurface m_BSplineSurface;
	Physics::TriangleSurface m_BSplineTriangleSurface;
	float m_BSplineControlPointSpacingInMetres{ 1.0f };
	float m_BSplineEvaluationStepInParameterUnits{ 0.5f };	// The book's h: 0.5 = a vertex every 0.5 m
	RollingSurfaceChoice m_RollingSurface{ RollingSurfaceChoice::GroundTriangulation };

	// Rendering
	entt::entity m_GroundMeshEntity{ entt::null };
	entt::entity m_BSplineMeshEntity{ entt::null };
	std::uint32_t m_BallModelIdentifier{ 0 };
	std::uint32_t m_PathBeadModelIdentifier{ 0 };

	// Physics
	Physics::RollingBallPhysicsWorld m_PhysicsWorld;
	std::vector<entt::entity> m_BallEntities;
	entt::entity m_TrackedBallEntity{ entt::null };
	float m_RadiusForNewBallsInMetres{ 0.5f };

	// Chase camera
	entt::entity m_PlayerCameraEntity{ entt::null };
	entt::entity m_FollowCameraEntity{ entt::null };
	bool m_IsCameraFollowingTrackedBall{ false };
	glm::vec2 m_FollowCameraDirectionXZ{ 1.0f, 0.0f };	// Last direction the ball was really moving in (kept while it's at rest)

	// Path recording
	Splines::QuadraticBSplineCurve m_TrackedBallPathCurve;
	double m_SimulatedTimeOfLastPathSample{ 0.0 };
	float m_PathSampleIntervalInSeconds{ 0.1f };
	float m_MinimumPathSampleDistanceInMetres{ 0.4f };
	std::size_t m_MaximumPathControlPoints{ 1000 };
	bool m_HasPathChangedSinceBeadsWereBuilt{ false };
	std::vector<entt::entity> m_PathBeadEntities;
	std::vector<glm::vec2> m_PathCurveSamplesScratch;

	// Load statistics (shown in the window)
	std::size_t m_LoadedPointCount{ 0 };
	double m_PointCloudLoadTimeInMilliseconds{ 0.0 };
	double m_HeightfieldBuildTimeInMilliseconds{ 0.0 };
	double m_GroundTriangulationTimeInMilliseconds{ 0.0 };
	double m_BSplineBuildTimeInMilliseconds{ 0.0 };
	double m_LastPhysicsUpdateTimeInMilliseconds{ 0.0 };
};

}

#endif // !HEPHAESTIAN_QUARRYROLLINGBALLDEMO_H
