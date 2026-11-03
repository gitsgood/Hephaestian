#ifndef HEPHAESTIAN_MOVEMENTSYSTEMS_H
#define HEPHAESTIAN_MOVEMENTSYSTEMS_H

#include <entt/entt.hpp>

#include "Hephaestian/Input/InputManager.h"

namespace Hephaestian::Systems
{

// Moves and rotates every entity with a FreeFlyMovementControllerComponent from keyboard and mouse input
void updateFreeFlyMovementControllers(entt::registry& Registry, const InputManager& Input, float DeltaTimeInSeconds);

// Spins every entity with a ConstantRotationComponent
void updateConstantRotations(entt::registry& Registry, float DeltaTimeInSeconds);

}

#endif // !HEPHAESTIAN_MOVEMENTSYSTEMS_H
