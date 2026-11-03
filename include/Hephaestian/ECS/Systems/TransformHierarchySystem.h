#ifndef HEPHAESTIAN_TRANSFORMHIERARCHYSYSTEM_H
#define HEPHAESTIAN_TRANSFORMHIERARCHYSYSTEM_H

#include <cstdint>

#include <entt/entt.hpp>

namespace Hephaestian::Systems
{

// Resolves every WorldTransformComponent from the TransformComponent + ParentEntityComponent chain.
// CurrentFrameNumber must change (go up) every call, it's how each entity gets resolved exactly once.
void updateWorldTransformsFromHierarchy(entt::registry& Registry, std::uint64_t CurrentFrameNumber);

}

#endif // !HEPHAESTIAN_TRANSFORMHIERARCHYSYSTEM_H
