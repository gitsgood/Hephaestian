/**
 * @file MovementSystems.cpp
 * @brief Free-fly controllers (WASD, mouse look, sprint) and constant rotations.
 */
#include "Hephaestian/ECS/Systems/MovementSystems.h"

#include <cmath>

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

namespace
{
	constexpr glm::vec3 c_WorldUpDirection{ 0.0f, 1.0f, 0.0f };				///< Yaw turns around this
	constexpr glm::vec3 c_UnrotatedForwardDirection{ 0.0f, 0.0f, -1.0f };	///< Where an unrotated entity looks
	constexpr glm::vec3 c_UnrotatedRightDirection{ 1.0f, 0.0f, 0.0f };		///< Pitch turns around this (once yawed)
	constexpr glm::vec3 c_RollAxisDirection{ 0.0f, 0.0f, 1.0f };			///< The view axis, pointing back at the viewer: a positive roll around it is counter-clockwise
	constexpr glm::vec3 c_UnrotatedUpDirection{ 0.0f, 1.0f, 0.0f };			///< An unrotated entity's up: relative yaw turns around this

	/**
	 * @brief Reads yaw, pitch and roll back out of whatever rotation the entity currently has.
	 *
	 * Doing this every frame (rather than storing the angles in the component) means the controller never fights anyone
	 * else who sets the rotation, e.g. spawn code.
	 * @param Rotation The entity's rotation.
	 * @param[out] OutYawAngleInRadians Turn around world up (0 = looking down -Z).
	 * @param[out] OutPitchAngleInRadians Up or down (positive = up).
	 * @param[out] OutRollAngleInRadians "Turn" the whole view clockwise or counter-clockwise.
	 */
	void extractYawPitchAndRollFromRotation(const glm::quat& Rotation, float& OutYawAngleInRadians, float& OutPitchAngleInRadians, float& OutRollAngleInRadians) noexcept
	{
		const glm::vec3 ForwardDirection = Rotation * c_UnrotatedForwardDirection;
		OutYawAngleInRadians = std::atan2(-ForwardDirection.x, -ForwardDirection.z);
		OutPitchAngleInRadians = std::asin(glm::clamp(ForwardDirection.y, -1.0f, 1.0f));

		// Roll: undo yaw and pitch, and what's left is a turn around the view axis. Its angle is how far "up" has tilted.
		const glm::quat RotationWithoutRoll = glm::angleAxis(OutYawAngleInRadians, c_WorldUpDirection) * glm::angleAxis(OutPitchAngleInRadians, c_UnrotatedRightDirection);
		const glm::vec3 UpDirectionWithoutYawAndPitch = glm::inverse(RotationWithoutRoll) * Rotation * c_WorldUpDirection;
		OutRollAngleInRadians = std::atan2(-UpDirectionWithoutYawAndPitch.x, UpDirectionWithoutYawAndPitch.y);
	}

	/**
	 * @brief Builds a rotation WITH roll.
	 * @param YawAngleInRadians Turn around world up.
	 * @param PitchAngleInRadians Up or down, around the yawed right axis.
	 * @param RollAngleInRadians Clockwise or counter-clowise. Looks like it rotates the whole view.
	 * @return The rotation.
	 */
	[[nodiscard]] glm::quat buildRotationFromYawPitchAndRoll(float YawAngleInRadians, float PitchAngleInRadians, float RollAngleInRadians) noexcept
	{
		// Yaw around world up, then pitch around the yawed right axis, then roll around the resulting view axis
		return glm::angleAxis(YawAngleInRadians, c_WorldUpDirection)
			* glm::angleAxis(PitchAngleInRadians, c_UnrotatedRightDirection)
			* glm::angleAxis(RollAngleInRadians, c_RollAxisDirection);
	}

	/**
	 * @brief Turns a rotation around its own current axes (relative, spaceship-style controls).
	 * @param Rotation The entity's rotation.
	 * @param YawChangeInRadians Around its own up (positive = left).
	 * @param PitchChangeInRadians Around its own right (positive = up).
	 * @param RollChangeInRadians Around its own view axis (positive = counter-clockwise).
	 * @return The turned rotation.
	 */
	[[nodiscard]] glm::quat applyRelativeTurn(const glm::quat& Rotation, float YawChangeInRadians, float PitchChangeInRadians, float RollChangeInRadians) noexcept
	{
		// Multiplying on the RIGHT turns around the entity's own axes, wherever they point now
		const glm::quat TurnThisFrame = glm::angleAxis(YawChangeInRadians, c_UnrotatedUpDirection)
			* glm::angleAxis(PitchChangeInRadians, c_UnrotatedRightDirection)
			* glm::angleAxis(RollChangeInRadians, c_RollAxisDirection);

		// Normalise every frame: nothing ever rebuilds this rotation from scratch, so floating point drift would add up
		return glm::normalize(Rotation * TurnThisFrame);
	}
}

std::optional<CameraControlScheme> parseCameraControlSchemeName(std::string_view ControlSchemeName) noexcept
{
	if (ControlSchemeName == "worldCoordinates") { return CameraControlScheme::WorldCoordinates; }
	if (ControlSchemeName == "relative") { return CameraControlScheme::Relative; }
	return std::nullopt;
}

std::string_view getCameraControlSchemeName(CameraControlScheme ControlScheme) noexcept
{
	switch (ControlScheme)
	{
		case CameraControlScheme::Relative: return "relative";
		default: return "worldCoordinates";
	}
}

void updateFreeFlyMovementControllers(entt::registry& Registry, const InputManager& Input, CameraControlScheme ControlScheme, float DeltaTimeInSeconds)
{
	auto EntitiesWithFreeFlyControllers = Registry.view<Components::TransformComponent, const Components::FreeFlyMovementControllerComponent>();

	EntitiesWithFreeFlyControllers.each([&](Components::TransformComponent& Transform, const Components::FreeFlyMovementControllerComponent& Controller)
	{
		// 1. LOOK AROUND (mouse) AND ROLL (keys)
		const bool IsAllowedToLookAround = !Controller.m_OnlyLookAroundWhileMouseCursorIsCaptured || Input.isMouseCursorCaptured();
		const glm::vec2 MouseMovementDelta = Input.getMouseMovementDeltaThisFrame();
		const bool HasMouseLookMoved = IsAllowedToLookAround && (MouseMovementDelta.x != 0.0f || MouseMovementDelta.y != 0.0f);

		float RequestedRollDirection{ 0.0f };	// +1 counter-clockwise, -1 clockwise, 0 for neither (or both)
		if (Input.isKeyHeld(Controller.m_RollCounterClockwiseKey)) { RequestedRollDirection += 1.0f; }
		if (Input.isKeyHeld(Controller.m_RollClockwiseKey)) { RequestedRollDirection -= 1.0f; }

		// The same turn for both modes; they only differ in how it's applied
		const float YawChangeInRadians = HasMouseLookMoved ? -MouseMovementDelta.x * Controller.m_MouseLookSensitivityInRadiansPerPixel : 0.0f;
		const float PitchChangeInRadians = HasMouseLookMoved ? -MouseMovementDelta.y * Controller.m_MouseLookSensitivityInRadiansPerPixel : 0.0f;
		const float RollChangeInRadians = RequestedRollDirection * Controller.m_RollSpeedInRadiansPerSecond * DeltaTimeInSeconds;

		if (HasMouseLookMoved || RequestedRollDirection != 0.0f)
		{
			if (ControlScheme == CameraControlScheme::Relative)
			{
				Transform.m_LocalRotation = applyRelativeTurn(Transform.m_LocalRotation, YawChangeInRadians, PitchChangeInRadians, RollChangeInRadians);
			}
			else
			{
				float CurrentYawAngleInRadians{ 0.0f };
				float CurrentPitchAngleInRadians{ 0.0f };
				float CurrentRollAngleInRadians{ 0.0f };
				extractYawPitchAndRollFromRotation(Transform.m_LocalRotation, CurrentYawAngleInRadians, CurrentPitchAngleInRadians, CurrentRollAngleInRadians);

				// Mouse right turns right (negative yaw in a right-handed, Y-up world), mouse down looks down (GLFW's Y grows downwards)
				CurrentYawAngleInRadians += YawChangeInRadians;
				CurrentPitchAngleInRadians = glm::clamp(CurrentPitchAngleInRadians + PitchChangeInRadians, -Controller.m_MaximumPitchAngleInRadians, Controller.m_MaximumPitchAngleInRadians);
				CurrentRollAngleInRadians += RollChangeInRadians;

				Transform.m_LocalRotation = buildRotationFromYawPitchAndRoll(CurrentYawAngleInRadians, CurrentPitchAngleInRadians, CurrentRollAngleInRadians);
			}
		}

		// 2. MOVE
		const glm::vec3 ForwardDirection = Transform.m_LocalRotation * c_UnrotatedForwardDirection;
		const glm::vec3 RightDirection = Transform.m_LocalRotation * c_UnrotatedRightDirection;
		const glm::vec3 UpDirection = (ControlScheme == CameraControlScheme::Relative) ? Transform.m_LocalRotation * c_UnrotatedUpDirection : c_WorldUpDirection;

		glm::vec3 RequestedMovementDirection{ 0.0f };
		if (Input.isKeyHeld(Controller.m_MoveForwardKey))	{ RequestedMovementDirection += ForwardDirection; }
		if (Input.isKeyHeld(Controller.m_MoveBackwardKey))	{ RequestedMovementDirection -= ForwardDirection; }
		if (Input.isKeyHeld(Controller.m_MoveRightKey))		{ RequestedMovementDirection += RightDirection; }
		if (Input.isKeyHeld(Controller.m_MoveLeftKey))		{ RequestedMovementDirection -= RightDirection; }
		if (Input.isKeyHeld(Controller.m_MoveUpKey))		{ RequestedMovementDirection += UpDirection; }
		if (Input.isKeyHeld(Controller.m_MoveDownKey))		{ RequestedMovementDirection -= UpDirection; }

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
