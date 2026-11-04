#include "Hephaestian/Simulation/QuarryRollingBallDemo.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

#include <glm/gtc/constants.hpp>
#include <imgui.h>

#include <Vulkanised/Logger.h>

#include "Hephaestian/ECS/Components.h"
#include "Hephaestian/Splines/QuadraticBSplineBasis.h"

namespace Hephaestian::Simulation
{

namespace
{
	constexpr glm::vec3 c_WorldUpDirection{ 0.0f, 1.0f, 0.0f };
	constexpr float c_PathBeadRadiusInMetres{ 0.12f };
	constexpr int c_PathCurveSamplesPerKnotInterval{ 3 };

	[[nodiscard]] double calculateMillisecondsSince(std::chrono::steady_clock::time_point StartTime)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartTime).count();
	}
}

QuarryRollingBallDemo::QuarryRollingBallDemo(Vulkanised::VulkanRenderer& Renderer, Scene& GameScene)
	: m_Renderer(Renderer)
	, m_Scene(GameScene)
{
}

bool QuarryRollingBallDemo::loadTerrain(const std::filesystem::path& PointCloudFilePath)
{
	// 1. POINT CLOUD
	auto StepStartTime = std::chrono::steady_clock::now();
	const std::vector<glm::vec3> PointPositions = Terrain::loadPointPositionsFromObjFile(PointCloudFilePath);
	m_PointCloudLoadTimeInMilliseconds = calculateMillisecondsSince(StepStartTime);
	m_LoadedPointCount = PointPositions.size();
	if (PointPositions.empty())
	{
		LOGE("No points in {}, the quarry demo is disabled.", PointCloudFilePath.string());
		return false;
	}

	// 2. BARE-EARTH HEIGHTFIELD (ground filter + hole filling)
	StepStartTime = std::chrono::steady_clock::now();
	m_GroundHeightfield = Terrain::GroundHeightfield::buildFromPointCloud(PointPositions, Terrain::GroundHeightfieldSettings{});
	m_HeightfieldBuildTimeInMilliseconds = calculateMillisecondsSince(StepStartTime);

	// 3. REGULAR GRID TRIANGULATION -> the surface the ball rolls on (task 1)
	StepStartTime = std::chrono::steady_clock::now();
	{
		std::vector<glm::vec3> GroundVertexPositions;
		std::vector<std::uint32_t> GroundTriangleIndices;
		m_GroundHeightfield.buildGridTriangulation(GroundVertexPositions, GroundTriangleIndices);
		m_GroundSurface.buildFromTriangles(std::move(GroundVertexPositions), std::move(GroundTriangleIndices));
	}
	m_GroundTriangulationTimeInMilliseconds = calculateMillisecondsSince(StepStartTime);

	// 4. BI-QUADRATIC B-SPLINE SURFACE (task 2): the heightfield is the control grid.
	// Each control point sits at the Greville abscissae (xi_i, xi_j) * spacing, which makes the surface's ground position
	// exactly (u, v) * spacing: the UV plane IS the scan's rectangle, scaled.
	StepStartTime = std::chrono::steady_clock::now();
	{
		const Terrain::GroundPlaneFrame& Frame = m_GroundHeightfield.getGroundPlaneFrame();
		const float ControlPointSpacing = m_BSplineControlPointSpacingInMetres;
		const int KnotIntervalCountU = std::max(1, static_cast<int>(std::floor(Frame.m_LengthAlongU / ControlPointSpacing)));
		const int KnotIntervalCountV = std::max(1, static_cast<int>(std::floor(Frame.m_LengthAlongV / ControlPointSpacing)));
		const int ControlPointCountU = KnotIntervalCountU + Splines::c_QuadraticDegree;	// n = (number of intervals) + d
		const int ControlPointCountV = KnotIntervalCountV + Splines::c_QuadraticDegree;
		std::vector<float> KnotVectorU = Splines::makeClampedUniformQuadraticKnotVector(ControlPointCountU);
		std::vector<float> KnotVectorV = Splines::makeClampedUniformQuadraticKnotVector(ControlPointCountV);

		std::vector<glm::vec3> ControlPoints;
		ControlPoints.reserve(static_cast<std::size_t>(ControlPointCountU) * static_cast<std::size_t>(ControlPointCountV));
		for (int ControlPointIndexV{ 0 }; ControlPointIndexV < ControlPointCountV; ++ControlPointIndexV)	// Row-wise in u, like the book
		{
			for (int ControlPointIndexU{ 0 }; ControlPointIndexU < ControlPointCountU; ++ControlPointIndexU)
			{
				const glm::vec2 LocalPosition{ ControlPointSpacing * Splines::calculateQuadraticGrevilleAbscissa(KnotVectorU, ControlPointIndexU), ControlPointSpacing * Splines::calculateQuadraticGrevilleAbscissa(KnotVectorV, ControlPointIndexV) };
				const glm::vec2 WorldXZ = Frame.convertLocalToWorldXZ(LocalPosition);
				ControlPoints.emplace_back(WorldXZ.x, m_GroundHeightfield.sampleExtendedHeightAtLocalPosition(LocalPosition), WorldXZ.y);
			}
		}
		m_BSplineSurface.setControlGrid(ControlPointCountU, ControlPointCountV, std::move(KnotVectorU), std::move(KnotVectorV), std::move(ControlPoints));

		std::vector<glm::vec3> BSplineVertexPositions;
		std::vector<std::uint32_t> BSplineTriangleIndices;
		m_BSplineSurface.tessellateSurfaceIntoTriangles(m_BSplineEvaluationStepInParameterUnits, BSplineVertexPositions, BSplineTriangleIndices,
			[this, ControlPointSpacing](float ParameterU, float ParameterV) { return m_GroundHeightfield.isLocalPositionInsideScan({ ParameterU * ControlPointSpacing, ParameterV * ControlPointSpacing }); });	// Nothing outside the scan
		m_BSplineTriangleSurface.buildFromTriangles(std::move(BSplineVertexPositions), std::move(BSplineTriangleIndices));
	}
	m_BSplineBuildTimeInMilliseconds = calculateMillisecondsSince(StepStartTime);

	const Terrain::GroundHeightfieldStatistics& Statistics = m_GroundHeightfield.getStatistics();
	LOG("Quarry: {} points read in {:.0f} ms | heightfield {}x{} @ {:.2f} m in {:.0f} ms ({} canopy + {} low-noise cells removed, {} holes filled) | ground {} triangles in {:.0f} ms | B-spline {}x{} control points, {} triangles in {:.0f} ms",
		m_LoadedPointCount, m_PointCloudLoadTimeInMilliseconds,
		m_GroundHeightfield.getSampleCountAlongU(), m_GroundHeightfield.getSampleCountAlongV(), m_GroundHeightfield.getCellSizeInMetres(), m_HeightfieldBuildTimeInMilliseconds,
		Statistics.m_CanopyCellsRemoved, Statistics.m_LowNoiseCellsRemoved, Statistics.m_HoleCellsFilled,
		m_GroundSurface.getTriangleCount(), m_GroundTriangulationTimeInMilliseconds,
		m_BSplineSurface.getControlPointCountU(), m_BSplineSurface.getControlPointCountV(), m_BSplineTriangleSurface.getTriangleCount(), m_BSplineBuildTimeInMilliseconds);

	createRenderMeshes();
	setRollingSurface(RollingSurfaceChoice::GroundTriangulation);

	// (The light is a HephaestianSettings setting now: position and colour live in HephaestianSettings.json and the settings window)

	dropBallAt(findInterestingStartPositionXZ());
	return true;
}

void QuarryRollingBallDemo::createRenderMeshes()
{
	entt::registry& Registry = m_Scene.getRegistry();

	m_GroundMeshEntity = m_Scene.createEntity("Quarry ground (triangulated LiDAR)");
	Registry.emplace<Components::ModelRendererComponent>(m_GroundMeshEntity, createModelFromTriangleSurface(m_GroundSurface, { 0.42f, 0.36f, 0.28f }, { 0.80f, 0.76f, 0.64f }));

	m_BSplineMeshEntity = m_Scene.createEntity("Quarry bi-quadratic B-spline surface");
	Registry.emplace<Components::ModelRendererComponent>(m_BSplineMeshEntity, createModelFromTriangleSurface(m_BSplineTriangleSurface, { 0.30f, 0.38f, 0.50f }, { 0.70f, 0.80f, 0.92f }));

	m_BallModelIdentifier = createSphereModel(24, 16, { 0.90f, 0.15f, 0.10f }, { 0.95f, 0.95f, 0.95f });	// Stripes, so you can see it roll
	m_PathBeadModelIdentifier = createSphereModel(8, 6, { 1.0f, 0.85f, 0.1f }, { 1.0f, 0.70f, 0.05f });
}

std::uint32_t QuarryRollingBallDemo::createModelFromTriangleSurface(const Physics::TriangleSurface& Surface, const glm::vec3& LowColour, const glm::vec3& HighColour)
{
	const std::vector<glm::vec3>& VertexPositions = Surface.getVertexPositions();
	const std::vector<glm::vec3> VertexNormals = Surface.calculateSmoothVertexNormals();

	float LowestHeight = std::numeric_limits<float>::max();
	float HighestHeight = std::numeric_limits<float>::lowest();
	for (const glm::vec3& CurrentPosition : VertexPositions)
	{
		LowestHeight = std::min(LowestHeight, CurrentPosition.y);
		HighestHeight = std::max(HighestHeight, CurrentPosition.y);
	}
	const float HeightRange = std::max(HighestHeight - LowestHeight, 1e-3f);

	std::vector<Vulkanised::Vertex> MeshVertices;
	MeshVertices.reserve(VertexPositions.size());
	for (std::size_t CurrentVertexIndex{ 0 }; CurrentVertexIndex < VertexPositions.size(); ++CurrentVertexIndex)
	{
		// Colour by height, darker on steep faces: the terraces read much better than with lighting alone
		const glm::vec3& Position = VertexPositions[CurrentVertexIndex];
		const glm::vec3& Normal = VertexNormals[CurrentVertexIndex];
		const float SteepnessDarkening = 1.0f - 0.45f * (1.0f - std::clamp(Normal.y, 0.0f, 1.0f));
		const glm::vec3 VertexColour = glm::mix(LowColour, HighColour, (Position.y - LowestHeight) / HeightRange) * SteepnessDarkening;
		MeshVertices.push_back(Vulkanised::Vertex{ Position, VertexColour, glm::vec2(0.0f), Normal });
	}

	const std::uint32_t WhiteTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
	const std::uint32_t MeshIdentifier = m_Renderer.loadMesh(MeshVertices, Surface.getTriangleIndices(), static_cast<int>(WhiteTextureIdentifier));
	return m_Renderer.createModelFromSingleMesh(MeshIdentifier, WhiteTextureIdentifier);
}

std::uint32_t QuarryRollingBallDemo::createSphereModel(int SegmentCountAroundEquator, int RingCount, const glm::vec3& FirstStripeColour, const glm::vec3& SecondStripeColour)
{
	auto calculatePointOnUnitSphere = [&](int RingIndex, int SegmentIndex)
	{
		const float PolarAngle = glm::pi<float>() * static_cast<float>(RingIndex) / static_cast<float>(RingCount);
		const float AzimuthAngle = glm::two_pi<float>() * static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCountAroundEquator);
		return glm::vec3(std::sin(PolarAngle) * std::cos(AzimuthAngle), std::cos(PolarAngle), std::sin(PolarAngle) * std::sin(AzimuthAngle));
	};

	// Each quad gets its own 4 vertices, so the stripes have crisp edges instead of blending across
	std::vector<Vulkanised::Vertex> SphereVertices;
	std::vector<std::uint32_t> SphereIndices;
	for (int RingIndex{ 0 }; RingIndex < RingCount; ++RingIndex)
	{
		for (int SegmentIndex{ 0 }; SegmentIndex < SegmentCountAroundEquator; ++SegmentIndex)
		{
			const glm::vec3 StripeColour = ((SegmentIndex / 3) % 2 == 0) ? FirstStripeColour : SecondStripeColour;
			const std::array<glm::vec3, 4> QuadCorners{
				calculatePointOnUnitSphere(RingIndex, SegmentIndex), calculatePointOnUnitSphere(RingIndex + 1, SegmentIndex),
				calculatePointOnUnitSphere(RingIndex + 1, SegmentIndex + 1), calculatePointOnUnitSphere(RingIndex, SegmentIndex + 1)
			};
			const std::uint32_t FirstVertexIndex = static_cast<std::uint32_t>(SphereVertices.size());
			for (const glm::vec3& CurrentCorner : QuadCorners)
			{
				SphereVertices.push_back(Vulkanised::Vertex{ CurrentCorner, StripeColour, glm::vec2(0.0f), CurrentCorner });	// On a unit sphere the normal is the position
			}

			// Outward-facing winding (the pipeline culls back faces), checked rather than assumed
			const glm::vec3 QuadCentre = (QuadCorners[0] + QuadCorners[1] + QuadCorners[2] + QuadCorners[3]) * 0.25f;
			const bool IsWindingOutward = glm::dot(glm::cross(QuadCorners[1] - QuadCorners[0], QuadCorners[2] - QuadCorners[0]), QuadCentre) > 0.0f
				|| glm::dot(glm::cross(QuadCorners[2] - QuadCorners[0], QuadCorners[3] - QuadCorners[0]), QuadCentre) > 0.0f;	// Poles collapse one corner
			if (IsWindingOutward)
			{
				SphereIndices.insert(SphereIndices.end(), { FirstVertexIndex, FirstVertexIndex + 1, FirstVertexIndex + 2, FirstVertexIndex, FirstVertexIndex + 2, FirstVertexIndex + 3 });
			}
			else
			{
				SphereIndices.insert(SphereIndices.end(), { FirstVertexIndex, FirstVertexIndex + 2, FirstVertexIndex + 1, FirstVertexIndex, FirstVertexIndex + 3, FirstVertexIndex + 2 });
			}
		}
	}

	const std::uint32_t WhiteTextureIdentifier = m_Renderer.getDefaultWhiteTextureIdentifier();
	const std::uint32_t MeshIdentifier = m_Renderer.loadMesh(SphereVertices, SphereIndices, static_cast<int>(WhiteTextureIdentifier));
	return m_Renderer.createModelFromSingleMesh(MeshIdentifier, WhiteTextureIdentifier);
}

const Physics::TriangleSurface& QuarryRollingBallDemo::getActiveSurface() const noexcept
{
	return m_RollingSurface == RollingSurfaceChoice::BSplineSurface ? m_BSplineTriangleSurface : m_GroundSurface;
}

void QuarryRollingBallDemo::setRollingSurface(RollingSurfaceChoice NewRollingSurface)
{
	m_RollingSurface = NewRollingSurface;
	m_PhysicsWorld.setSurface(&getActiveSurface());

	entt::registry& Registry = m_Scene.getRegistry();
	if (Registry.valid(m_GroundMeshEntity)) { Registry.get<Components::ModelRendererComponent>(m_GroundMeshEntity).m_IsVisible = NewRollingSurface == RollingSurfaceChoice::GroundTriangulation; }
	if (Registry.valid(m_BSplineMeshEntity)) { Registry.get<Components::ModelRendererComponent>(m_BSplineMeshEntity).m_IsVisible = NewRollingSurface == RollingSurfaceChoice::BSplineSurface; }

	// Move every ball onto the new surface where it is, keeping its speed along the new slope
	for (const entt::entity CurrentBallEntity : m_BallEntities)
	{
		const glm::vec3 PreviousPosition = Registry.get<Components::TransformComponent>(CurrentBallEntity).m_LocalPosition;
		const glm::vec3 PreviousVelocity = Registry.get<Physics::RollingBallComponent>(CurrentBallEntity).m_VelocityInMetresPerSecond;
		if (m_PhysicsWorld.placeBallOnSurface(Registry, CurrentBallEntity, { PreviousPosition.x, PreviousPosition.z }))
		{
			auto& Ball = Registry.get<Physics::RollingBallComponent>(CurrentBallEntity);
			Ball.m_VelocityInMetresPerSecond = PreviousVelocity - glm::dot(PreviousVelocity, Ball.m_CurrentTriangleNormal) * Ball.m_CurrentTriangleNormal;
		}
	}
	m_HasPathChangedSinceBeadsWereBuilt = true;	// Same UV path, different surface height under it
}

glm::vec2 QuarryRollingBallDemo::convertWorldXZToSurfaceParameter(const glm::vec2& WorldXZ) const noexcept
{
	return m_GroundHeightfield.getGroundPlaneFrame().convertWorldXZToLocal(WorldXZ) / m_BSplineControlPointSpacingInMetres;
}

glm::vec2 QuarryRollingBallDemo::convertSurfaceParameterToWorldXZ(const glm::vec2& SurfaceParameter) const noexcept
{
	return m_GroundHeightfield.getGroundPlaneFrame().convertLocalToWorldXZ(SurfaceParameter * m_BSplineControlPointSpacingInMetres);
}

glm::vec2 QuarryRollingBallDemo::findInterestingStartPositionXZ() const
{
	// Across the middle of the quarry, find the highest ground (the rim), then start a few metres in, on the way down
	const int MiddleSampleIndexU = m_GroundHeightfield.getSampleCountAlongU() / 2;
	int RimSampleIndexV{ -1 };
	float RimHeight = std::numeric_limits<float>::lowest();
	for (int SampleIndexV{ 0 }; SampleIndexV < m_GroundHeightfield.getSampleCountAlongV(); ++SampleIndexV)
	{
		if (m_GroundHeightfield.isSampleValid(MiddleSampleIndexU, SampleIndexV) && m_GroundHeightfield.getSampleHeight(MiddleSampleIndexU, SampleIndexV) > RimHeight)
		{
			RimHeight = m_GroundHeightfield.getSampleHeight(MiddleSampleIndexU, SampleIndexV);
			RimSampleIndexV = SampleIndexV;
		}
	}

	const float CellSize = m_GroundHeightfield.getCellSizeInMetres();
	const Terrain::GroundPlaneFrame& Frame = m_GroundHeightfield.getGroundPlaneFrame();
	if (RimSampleIndexV < 0)
	{
		return Frame.convertLocalToWorldXZ({ Frame.m_LengthAlongU * 0.5f, Frame.m_LengthAlongV * 0.5f });
	}
	const float RimPositionV = (static_cast<float>(RimSampleIndexV) + 0.5f) * CellSize;
	const float InwardDirection = RimPositionV < Frame.m_LengthAlongV * 0.5f ? 1.0f : -1.0f;
	return Frame.convertLocalToWorldXZ({ (static_cast<float>(MiddleSampleIndexU) + 0.5f) * CellSize, RimPositionV + InwardDirection * 3.0f });
}

bool QuarryRollingBallDemo::dropBallAt(const glm::vec2& WorldXZ)
{
	entt::registry& Registry = m_Scene.getRegistry();
	const entt::entity NewBallEntity = m_Scene.createEntity("Rolling ball " + std::to_string(m_BallEntities.size() + 1));
	Registry.emplace<Components::ModelRendererComponent>(NewBallEntity, m_BallModelIdentifier);
	Physics::RollingBallComponent& NewBall = Registry.emplace<Physics::RollingBallComponent>(NewBallEntity);
	NewBall.m_RadiusInMetres = m_RadiusForNewBallsInMetres;

	if (!m_PhysicsWorld.placeBallOnSurface(Registry, NewBallEntity, WorldXZ))
	{
		m_Scene.destroyEntity(NewBallEntity);
		LOGW("No surface below ({:.1f}, {:.1f}), no ball dropped.", WorldXZ.x, WorldXZ.y);
		return false;
	}

	m_BallEntities.push_back(NewBallEntity);
	m_TrackedBallEntity = NewBallEntity;
	clearPath();
	return true;
}

void QuarryRollingBallDemo::removeAllBalls()
{
	for (const entt::entity CurrentBallEntity : m_BallEntities)
	{
		m_Scene.destroyEntity(CurrentBallEntity);
	}
	m_BallEntities.clear();
	m_TrackedBallEntity = entt::null;
	clearPath();
}

void QuarryRollingBallDemo::update(float DeltaTimeInSeconds)
{
	if (!isTerrainLoaded())
	{
		return;
	}

	const auto PhysicsStartTime = std::chrono::steady_clock::now();
	m_PhysicsWorld.advanceSimulation(m_Scene.getRegistry(), DeltaTimeInSeconds);
	m_LastPhysicsUpdateTimeInMilliseconds = calculateMillisecondsSince(PhysicsStartTime);

	recordTrackedBallPath();
	if (m_HasPathChangedSinceBeadsWereBuilt)
	{
		rebuildPathTrailBeads();
		m_HasPathChangedSinceBeadsWereBuilt = false;
	}
	updateFollowCamera(DeltaTimeInSeconds);
}

void QuarryRollingBallDemo::setCameraFollowsTrackedBall(bool ShouldCameraFollowTrackedBall)
{
	m_IsCameraFollowingTrackedBall = ShouldCameraFollowTrackedBall;
	if (ShouldCameraFollowTrackedBall && m_FollowCameraEntity == entt::null)
	{
		m_FollowCameraEntity = m_Scene.createEntity("Ball chase camera");
		m_Scene.getRegistry().emplace<Components::CameraComponent>(m_FollowCameraEntity, 65.0f);
	}

	if (ShouldCameraFollowTrackedBall)
	{
		updateFollowCamera(1000.0f);	// Snap straight into place instead of flying in from the origin
		m_Scene.setActiveCameraEntity(m_FollowCameraEntity);
	}
	else if (m_Scene.getRegistry().valid(m_PlayerCameraEntity))
	{
		m_Scene.setActiveCameraEntity(m_PlayerCameraEntity);
	}
}

void QuarryRollingBallDemo::updateFollowCamera(float DeltaTimeInSeconds)
{
	entt::registry& Registry = m_Scene.getRegistry();
	if (!m_IsCameraFollowingTrackedBall || !Registry.valid(m_FollowCameraEntity) || !Registry.valid(m_TrackedBallEntity))
	{
		return;
	}

	const glm::vec3& BallPosition = Registry.get<Components::TransformComponent>(m_TrackedBallEntity).m_LocalPosition;
	const glm::vec3& BallVelocity = Registry.get<Physics::RollingBallComponent>(m_TrackedBallEntity).m_VelocityInMetresPerSecond;
	const glm::vec2 BallVelocityXZ{ BallVelocity.x, BallVelocity.z };
	if (glm::length(BallVelocityXZ) > 0.5f)
	{
		m_FollowCameraDirectionXZ = glm::normalize(BallVelocityXZ);
	}

	// Behind and above the ball, eased in so the camera doesn't jitter with every triangle the ball crosses
	glm::vec3 DesiredCameraPosition = BallPosition - glm::vec3(m_FollowCameraDirectionXZ.x, 0.0f, m_FollowCameraDirectionXZ.y) * 9.0f + c_WorldUpDirection * 4.5f;
	auto& CameraTransform = Registry.get<Components::TransformComponent>(m_FollowCameraEntity);
	const float EaseFactor = 1.0f - std::exp(-4.0f * DeltaTimeInSeconds);
	CameraTransform.m_LocalPosition = glm::mix(CameraTransform.m_LocalPosition, DesiredCameraPosition, EaseFactor);

	// Behind the ball can be INSIDE a slope (the ball rolled up against a wall): always stay a bit above the ground
	constexpr float c_MinimumCameraHeightAboveGroundInMetres{ 2.0f };
	if (const std::optional<float> GroundHeightUnderCamera = getActiveSurface().sampleHeight({ CameraTransform.m_LocalPosition.x, CameraTransform.m_LocalPosition.z }))
	{
		CameraTransform.m_LocalPosition.y = std::max(CameraTransform.m_LocalPosition.y, GroundHeightUnderCamera.value() + c_MinimumCameraHeightAboveGroundInMetres);
	}

	const glm::vec3 LookDirection = glm::normalize(BallPosition - CameraTransform.m_LocalPosition);
	const float YawAngle = std::atan2(-LookDirection.x, -LookDirection.z);
	const float PitchAngle = std::asin(std::clamp(LookDirection.y, -1.0f, 1.0f));
	CameraTransform.m_LocalRotation = glm::angleAxis(YawAngle, c_WorldUpDirection) * glm::angleAxis(PitchAngle, glm::vec3(1.0f, 0.0f, 0.0f));
}

void QuarryRollingBallDemo::recordTrackedBallPath()
{
	entt::registry& Registry = m_Scene.getRegistry();
	if (!Registry.valid(m_TrackedBallEntity) || m_TrackedBallPathCurve.getControlPoints().size() >= m_MaximumPathControlPoints)
	{
		return;
	}
	if (m_PhysicsWorld.getSimulatedTimeInSeconds() - m_SimulatedTimeOfLastPathSample < static_cast<double>(m_PathSampleIntervalInSeconds))
	{
		return;
	}
	m_SimulatedTimeOfLastPathSample = m_PhysicsWorld.getSimulatedTimeInSeconds();

	// The ball's ground position in the UV plane = a new control point (only once it has moved far enough to matter)
	const glm::vec3& BallPosition = Registry.get<Components::TransformComponent>(m_TrackedBallEntity).m_LocalPosition;
	const glm::vec2 BallSurfaceParameter = convertWorldXZToSurfaceParameter({ BallPosition.x, BallPosition.z });
	const std::vector<glm::vec2>& ExistingControlPoints = m_TrackedBallPathCurve.getControlPoints();
	const float MinimumParameterDistance = m_MinimumPathSampleDistanceInMetres / m_BSplineControlPointSpacingInMetres;
	if (ExistingControlPoints.empty() || glm::distance(ExistingControlPoints.back(), BallSurfaceParameter) >= MinimumParameterDistance)
	{
		m_TrackedBallPathCurve.appendControlPoint(BallSurfaceParameter);
		m_HasPathChangedSinceBeadsWereBuilt = true;
	}
}

void QuarryRollingBallDemo::rebuildPathTrailBeads()
{
	entt::registry& Registry = m_Scene.getRegistry();
	m_TrackedBallPathCurve.sampleCurve(c_PathCurveSamplesPerKnotInterval, m_PathCurveSamplesScratch);

	// Grow the bead pool as needed (entities are reused, never destroyed while the path grows)
	while (m_PathBeadEntities.size() < m_PathCurveSamplesScratch.size())
	{
		const entt::entity NewBeadEntity = m_Scene.createEntity("Path bead");
		Registry.emplace<Components::ModelRendererComponent>(NewBeadEntity, m_PathBeadModelIdentifier);
		Registry.get<Components::TransformComponent>(NewBeadEntity).m_LocalScale = glm::vec3(c_PathBeadRadiusInMetres);
		m_PathBeadEntities.push_back(NewBeadEntity);
	}

	// The curve lives in the UV plane: lift every sample onto the surface the ball rolls on
	const Physics::TriangleSurface& ActiveSurface = getActiveSurface();
	int PreviousBeadTriangle = Physics::TriangleSurface::c_NoTriangle;
	for (std::size_t CurrentBeadIndex{ 0 }; CurrentBeadIndex < m_PathBeadEntities.size(); ++CurrentBeadIndex)
	{
		auto& BeadRenderer = Registry.get<Components::ModelRendererComponent>(m_PathBeadEntities[CurrentBeadIndex]);
		BeadRenderer.m_IsVisible = false;
		if (CurrentBeadIndex >= m_PathCurveSamplesScratch.size())
		{
			continue;
		}

		const glm::vec2 BeadWorldXZ = convertSurfaceParameterToWorldXZ(m_PathCurveSamplesScratch[CurrentBeadIndex]);
		const int BeadTriangle = ActiveSurface.findTriangleContainingPoint(BeadWorldXZ, PreviousBeadTriangle);	// Neighbouring beads: 0-1 steps
		if (BeadTriangle == Physics::TriangleSurface::c_NoTriangle)
		{
			continue;
		}
		PreviousBeadTriangle = BeadTriangle;
		const float SurfaceHeight = ActiveSurface.calculateHeightOnTriangle(BeadTriangle, BeadWorldXZ);
		Registry.get<Components::TransformComponent>(m_PathBeadEntities[CurrentBeadIndex]).m_LocalPosition = { BeadWorldXZ.x, SurfaceHeight + c_PathBeadRadiusInMetres, BeadWorldXZ.y };
		BeadRenderer.m_IsVisible = true;
	}
}

void QuarryRollingBallDemo::clearPath()
{
	m_TrackedBallPathCurve.clear();
	m_SimulatedTimeOfLastPathSample = -1.0e9;
	m_HasPathChangedSinceBeadsWereBuilt = true;
}

void QuarryRollingBallDemo::calculateSuggestedCameraPlacement(glm::vec3& OutCameraPosition, glm::quat& OutCameraRotation) const
{
	const Terrain::GroundPlaneFrame& Frame = m_GroundHeightfield.getGroundPlaneFrame();
	const glm::vec2 CentreLocal{ Frame.m_LengthAlongU * 0.5f, Frame.m_LengthAlongV * 0.5f };
	const glm::vec2 CentreXZ = Frame.convertLocalToWorldXZ(CentreLocal);
	const glm::vec3 LookAtTarget{ CentreXZ.x, m_GroundHeightfield.sampleExtendedHeightAtLocalPosition(CentreLocal), CentreXZ.y };

	// Back off across the quarry's short side and up, so the whole length is in view
	const glm::vec2 BackOffDirectionXZ = -Frame.m_AxisVXZ;
	OutCameraPosition = LookAtTarget + glm::vec3(BackOffDirectionXZ.x, 0.0f, BackOffDirectionXZ.y) * (Frame.m_LengthAlongV * 1.1f) + c_WorldUpDirection * 40.0f;

	// Same yaw/pitch convention as the free-fly controller, so it carries on smoothly from here
	const glm::vec3 LookDirection = glm::normalize(LookAtTarget - OutCameraPosition);
	const float YawAngle = std::atan2(-LookDirection.x, -LookDirection.z);
	const float PitchAngle = std::asin(std::clamp(LookDirection.y, -1.0f, 1.0f));
	OutCameraRotation = glm::angleAxis(YawAngle, c_WorldUpDirection) * glm::angleAxis(PitchAngle, glm::vec3(1.0f, 0.0f, 0.0f));
}

void QuarryRollingBallDemo::drawPhysicsWindow(const glm::vec3& CameraWorldPosition)
{
	ImGui::SetNextWindowPos(ImVec2(16.0f, 190.0f), ImGuiCond_FirstUseEver);	// Under the settings window
	ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Physics"))
	{
		ImGui::End();
		return;
	}
	if (!isTerrainLoaded())
	{
		ImGui::TextUnformatted("No terrain loaded (assets/Terrain/laz_mesh.obj).");
		ImGui::End();
		return;
	}

	entt::registry& Registry = m_Scene.getRegistry();
	const Terrain::GroundHeightfieldStatistics& Statistics = m_GroundHeightfield.getStatistics();

	if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Point cloud: %zu points (%.0f ms)", m_LoadedPointCount, m_PointCloudLoadTimeInMilliseconds);
		ImGui::Text("Ground grid: %d x %d @ %.2f m (%.0f ms)\n  removed %zu canopy + %zu low-noise cells, filled %zu holes",
			m_GroundHeightfield.getSampleCountAlongU(), m_GroundHeightfield.getSampleCountAlongV(), m_GroundHeightfield.getCellSizeInMetres(),
			m_HeightfieldBuildTimeInMilliseconds, Statistics.m_CanopyCellsRemoved, Statistics.m_LowNoiseCellsRemoved, Statistics.m_HoleCellsFilled);
		ImGui::Text("Ground triangles: %zu (%.0f ms)", m_GroundSurface.getTriangleCount(), m_GroundTriangulationTimeInMilliseconds);
		ImGui::Text("B-spline: %d x %d control points, %zu triangles (%.0f ms)",
			m_BSplineSurface.getControlPointCountU(), m_BSplineSurface.getControlPointCountV(), m_BSplineTriangleSurface.getTriangleCount(), m_BSplineBuildTimeInMilliseconds);

		int RollingSurfaceIndex = static_cast<int>(m_RollingSurface);
		const char* const RollingSurfaceNames[] = { "Ground triangulation", "Bi-quadratic B-spline surface" };
		if (ImGui::Combo("Rolling surface", &RollingSurfaceIndex, RollingSurfaceNames, 2))
		{
			setRollingSurface(static_cast<RollingSurfaceChoice>(RollingSurfaceIndex));
		}
		ImGui::Checkbox("Show ground mesh", &Registry.get<Components::ModelRendererComponent>(m_GroundMeshEntity).m_IsVisible);
		ImGui::SameLine();
		ImGui::Checkbox("Show B-spline mesh", &Registry.get<Components::ModelRendererComponent>(m_BSplineMeshEntity).m_IsVisible);
	}

	if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		Physics::RollingBallPhysicsSettings& PhysicsSettings = m_PhysicsWorld.getSettings();
		ImGui::Checkbox("Paused", &PhysicsSettings.m_IsPaused);
		ImGui::SliderFloat("Friction (mu)", &PhysicsSettings.m_FrictionCoefficient, 0.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Gravity (m/s^2)", &PhysicsSettings.m_GravitationalAccelerationInMetresPerSecondSquared, 0.0f, 30.0f, "%.2f");
		ImGui::SliderFloat("Radius of new balls (m)", &m_RadiusForNewBallsInMetres, 0.1f, 3.0f, "%.2f");

		if (ImGui::Button("Drop ball below camera"))
		{
			dropBallAt({ CameraWorldPosition.x, CameraWorldPosition.z });
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove all balls"))
		{
			removeAllBalls();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear path"))
		{
			clearPath();
		}
		bool IsCameraFollowingTrackedBall = m_IsCameraFollowingTrackedBall;
		if (ImGui::Checkbox("Camera follows the ball", &IsCameraFollowingTrackedBall))
		{
			setCameraFollowsTrackedBall(IsCameraFollowingTrackedBall);
		}

		ImGui::Text("Balls: %zu | physics: %d steps of %.1f ms this frame, %.3f ms CPU",
			m_BallEntities.size(), m_PhysicsWorld.getStepsTakenLastFrame(), PhysicsSettings.m_FixedTimeStepInSeconds * 1000.0f, m_LastPhysicsUpdateTimeInMilliseconds);

		if (Registry.valid(m_TrackedBallEntity))
		{
			const auto& TrackedBall = Registry.get<Physics::RollingBallComponent>(m_TrackedBallEntity);
			const glm::vec3& TrackedBallPosition = Registry.get<Components::TransformComponent>(m_TrackedBallEntity).m_LocalPosition;
			ImGui::Text("Tracked ball: %.2f m/s at height %.2f m, triangle %d, %llu triangle changes%s",
				glm::length(TrackedBall.m_VelocityInMetresPerSecond), TrackedBallPosition.y, TrackedBall.m_CurrentTriangleIndex,
				static_cast<unsigned long long>(TrackedBall.m_TriangleTransitionCount), TrackedBall.m_IsOnSurface ? "" : " (rolled off the edge)");
		}
		ImGui::Text("Path (quadratic B-spline in UV): %zu control points", m_TrackedBallPathCurve.getControlPoints().size());
	}

	ImGui::End();
}

}
