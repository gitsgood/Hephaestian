/**
 * @file MovementSystems.h
 * @brief Things that move every frame on their own: free-fly controllers and constant rotations.
 */
#ifndef HEPHAESTIAN_MOVEMENTSYSTEMS_H
#define HEPHAESTIAN_MOVEMENTSYSTEMS_H

#include <entt/entt.hpp>

#include "Hephaestian/Input/InputManager.h"

namespace Hephaestian::Systems
{

/**
* @brief How free-fly controllers turn ("cameraControlScheme" in HephaestianSettings).
*/
enum class CameraControlScheme
{
	WorldCoordinates,	///< Mouse yaw turns around the world's vertical axis, pitch stops short of straight up or down, roll is a tilt on top (first-person style)
	Relative			///< Every turn is around the view's own axes, with no limits (spaceship style)
};

/**
* @brief Reads a control scheme's settings name.
* @param ControlSchemeName "worldCoordinates" or "relative".
* @return The scheme, or std::nullopt for anything else.
*/
[[nodiscard]] std::optional<CameraControlScheme> parseCameraControlSchemeName(std::string_view ControlSchemeName) noexcept;

/**
* @brief A control scheme's settings name (the reverse of parseCameraControlSchemeName).
* @param ControlScheme The scheme.
* @return Its name.
*/
[[nodiscard]] std::string_view getCameraControlSchemeName(CameraControlScheme ControlScheme) noexcept;

/**
 * @brief Moves and rotates every entity with a FreeFlyMovementControllerComponent from keyboard and mouse input.
 * @param Registry The scene's entities.
 * @param Input This frame's keyboard and mouse state.
 * @param ControlScheme How to turn (see CameraControlScheme).
 * @param DeltaTimeInSeconds Time since the last frame.
 */
void updateFreeFlyMovementControllers(entt::registry& Registry, const InputManager& Input, CameraControlScheme ControlScheme, float DeltaTimeInSeconds);

/**
 * @brief Spins every entity with a ConstantRotationComponent.
 * @param Registry The scene's entities.
 * @param DeltaTimeInSeconds Time since the last frame.
 */
void updateConstantRotations(entt::registry& Registry, float DeltaTimeInSeconds);

}

#endif // !HEPHAESTIAN_MOVEMENTSYSTEMS_H
