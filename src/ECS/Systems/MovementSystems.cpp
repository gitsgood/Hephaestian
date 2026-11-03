#include "Hephaestian/ECS/Systems/MovementSystems.h"

#include <cmath>

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

namespace
{
	constexpr glm::vec3 c_WorldUpDirection{ 0.0f, 1.0f, 0.0f };
	constexpr glm::vec3 c_UnrotatedForwardDirection{ 0.0f, 0.0f, -1.0f };
	constexpr glm::vec3 c_UnrotatedRightDirection{ 1.0f, 0.0f, 0.0f };

	// Reads yaw and pitch back out of whatever rotation the entity currently has. Doing this every frame (rather than storing
	// the angles in the component) means the controller never fights anyone else who sets the rotation, e.g. spawn code.
	void extractYawAndPitchFromRotation(const glm::quat& Rotation, float& OutYawAngleInRadians, float& OutPitchAngleInRadians) noexcept
	{
		const glm::vec3 ForwardDirection = Rotation * c_UnrotatedForwardDirection;
		OutYawAngleInRadians = std::atan2(-ForwardDirection.x, -ForwardDirection.z);
		OutPitchAngleInRadians = std::asin(glm::clamp(ForwardDirection.y, -1.0f, 1.0f));
	}

	[[nodiscard]] glm::quat buildRotationFromYawAndPitch(float YawAngleInRadians, float PitchAngleInRadians) noexcept
	{
		// Yaw around the world up axis first, then pitch around the (already yawed) local right axis. No roll, ever.
		return glm::angleAxis(YawAngleInRadians, c_WorldUpDirection) * glm::angleAxis(PitchAngleInRadians, c_UnrotatedRightDirection);
	}
}

void updateFreeFlyMovementControllers(entt::registry& Registry, const InputManager& Input, float DeltaTimeInSeconds)
{
	auto EntitiesWithFreeFlyControllers = Registry.view<Components::TransformComponent, const Components::FreeFlyMovementControllerComponent>();

	EntitiesWithFreeFlyControllers.each([&](Components::TransformComponent& Transform, const Components::FreeFlyMovementControllerComponent& Controller)
	{
		// 1. LOOK AROUND
		const bool IsAllowedToLookAround = !Controller.m_OnlyLookAroundWhileMouseCursorIsCaptured || Input.isMouseCursorCaptured();
		const glm::vec2 MouseMovementDelta = Input.getMouseMovementDeltaThisFrame();

		if (IsAllowedToLookAround && (MouseMovementDelta.x != 0.0f || MouseMovementDelta.y != 0.0f))
		{
			float CurrentYawAngleInRadians{ 0.0f };
			float CurrentPitchAngleInRadians{ 0.0f };
			extractYawAndPitchFromRotation(Transform.m_LocalRotation, CurrentYawAngleInRadians, CurrentPitchAngleInRadians);

			// Mouse right turns right (negative yaw in a right-handed, Y-up world), mouse down looks down (GLFW's Y grows downwards)
			CurrentYawAngleInRadians -= MouseMovementDelta.x * Controller.m_MouseLookSensitivityInRadiansPerPixel;
			CurrentPitchAngleInRadians -= MouseMovementDelta.y * Controller.m_MouseLookSensitivityInRadiansPerPixel;
			CurrentPitchAngleInRadians = glm::clamp(CurrentPitchAngleInRadians, -Controller.m_MaximumPitchAngleInRadians, Controller.m_MaximumPitchAngleInRadians);

			Transform.m_LocalRotation = buildRotationFromYawAndPitch(CurrentYawAngleInRadians, CurrentPitchAngleInRadians);
		}

		// 2. MOVE
		const glm::vec3 ForwardDirection = Transform.m_LocalRotation * c_UnrotatedForwardDirection;
		const glm::vec3 RightDirection = Transform.m_LocalRotation * c_UnrotatedRightDirection;

		glm::vec3 RequestedMovementDirection{ 0.0f };
		if (Input.isKeyHeld(Controller.m_MoveForwardKey))	{ RequestedMovementDirection += ForwardDirection; }
		if (Input.isKeyHeld(Controller.m_MoveBackwardKey))	{ RequestedMovementDirection -= ForwardDirection; }
		if (Input.isKeyHeld(Controller.m_MoveRightKey))		{ RequestedMovementDirection += RightDirection; }
		if (Input.isKeyHeld(Controller.m_MoveLeftKey))		{ RequestedMovementDirection -= RightDirection; }
		if (Input.isKeyHeld(Controller.m_MoveUpKey))		{ RequestedMovementDirection += c_WorldUpDirection; }
		if (Input.isKeyHeld(Controller.m_MoveDownKey))		{ RequestedMovementDirection -= c_WorldUpDirection; }

		// Normalise so going diagonally isn't faster than going straight
		const float RequestedMovementLength = glm::length(RequestedMovementDirection);
		if (RequestedMovementLength > 0.0001f)
		{
			const float SpeedMultiplier = Input.isKeyHeld(Controller.m_SprintKey) ? Controller.m_SprintMovementSpeedMultiplier : 1.0f;
			const float DistanceToTravelThisFrame = Controller.m_MovementSpeedInUnitsPerSecond * SpeedMultiplier * DeltaTimeInSeconds;
			Transform.m_LocalPosition += (RequestedMovementDirection / RequestedMovementLength) * DistanceToTravelThisFrame;
		}
	});
}

void updateConstantRotations(entt::registry& Registry, float DeltaTimeInSeconds)
{
	auto EntitiesWithConstantRotations = Registry.view<Components::TransformComponent, const Components::ConstantRotationComponent>();

	EntitiesWithConstantRotations.each([DeltaTimeInSeconds](Components::TransformComponent& Transform, const Components::ConstantRotationComponent& ConstantRotation)
	{
		const float RotationAxisLength = glm::length(ConstantRotation.m_RotationAxis);
		if (RotationAxisLength < 0.0001f)
		{
			return;	// A zero axis would give us NaNs
		}

		const float RotationAngleThisFrameInRadians = glm::radians(ConstantRotation.m_RotationSpeedInDegreesPerSecond * DeltaTimeInSeconds);
		const glm::quat RotationThisFrame = glm::angleAxis(RotationAngleThisFrameInRadians, ConstantRotation.m_RotationAxis / RotationAxisLength);

		// Normalise every frame, otherwise floating point drift slowly turns the rotation into a scale
		Transform.m_LocalRotation = glm::normalize(Transform.m_LocalRotation * RotationThisFrame);
	});
}

}
