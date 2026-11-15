/**
 * @file TransformHierarchySystem.h
 * @brief Local transforms and parents into world transforms.
 */
#ifndef HEPHAESTIAN_TRANSFORMHIERARCHYSYSTEM_H
#define HEPHAESTIAN_TRANSFORMHIERARCHYSYSTEM_H

#include <cstdint>

#include <entt/entt.hpp>

namespace Hephaestian::Systems
{

/**
 * @brief Resolves every WorldTransformComponent from the TransformComponent + ParentEntityComponent chain.
 * @param Registry The scene's entities.
 * @param CurrentFrameNumber Must change (go up) every call: it's how each entity gets resolved exactly once, parents
 * before children.
 */
void updateWorldTransformsFromHierarchy(entt::registry& Registry, std::uint64_t CurrentFrameNumber);

}

#endif // !HEPHAESTIAN_TRANSFORMHIERARCHYSYSTEM_H
