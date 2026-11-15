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
 * @brief Moves and rotates every entity with a FreeFlyMovementControllerComponent from keyboard and mouse input.
 * @param Registry The scene's entities.
 * @param Input This frame's keyboard and mouse state.
 * @param DeltaTimeInSeconds Time since the last frame.
 */
void updateFreeFlyMovementControllers(entt::registry& Registry, const InputManager& Input, float DeltaTimeInSeconds);

/**
 * @brief Spins every entity with a ConstantRotationComponent.
 * @param Registry The scene's entities.
 * @param DeltaTimeInSeconds Time since the last frame.
 */
void updateConstantRotations(entt::registry& Registry, float DeltaTimeInSeconds);

}

#endif // !HEPHAESTIAN_MOVEMENTSYSTEMS_H
