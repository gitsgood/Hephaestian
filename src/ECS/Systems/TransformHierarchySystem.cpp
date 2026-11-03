#include "Hephaestian/ECS/Systems/TransformHierarchySystem.h"

#include <Vulkanised/Logger.h>

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

namespace
{
	// Scene::attachEntityToParent already refuses cycles, this is only the last line of defence against a hand-made one
	constexpr std::uint32_t c_MaximumHierarchyDepthBeforeAssumingACycle{ 256 };

	// Resolves one entity's world matrix, resolving its parents first if they haven't been this frame.
	// Parents are visited at most once per frame thanks to m_FrameNumberOfLastUpdate, so the whole pass stays O(entities).
	const glm::mat4& resolveWorldTransformOfEntity(entt::registry& Registry, entt::entity EntityToResolve, std::uint64_t CurrentFrameNumber, std::uint32_t CurrentHierarchyDepth)
	{
		auto& WorldTransformOfEntity = Registry.get<Components::WorldTransformComponent>(EntityToResolve);
		if (WorldTransformOfEntity.m_FrameNumberOfLastUpdate == CurrentFrameNumber)
		{
			return WorldTransformOfEntity.m_WorldTransformMatrix;
		}

		const glm::mat4 LocalTransformMatrix = Registry.get<Components::TransformComponent>(EntityToResolve).calculateLocalTransformMatrix();

		glm::mat4 ParentWorldTransformMatrix{ 1.0f };
		if (const auto* ParentOfEntity = Registry.try_get<Components::ParentEntityComponent>(EntityToResolve))
		{
			const entt::entity ParentEntity = ParentOfEntity->m_ParentEntity;
			const bool IsParentUsable = Registry.valid(ParentEntity) && Registry.all_of<Components::TransformComponent, Components::WorldTransformComponent>(ParentEntity);

			if (IsParentUsable && CurrentHierarchyDepth < c_MaximumHierarchyDepthBeforeAssumingACycle)
			{
				ParentWorldTransformMatrix = resolveWorldTransformOfEntity(Registry, ParentEntity, CurrentFrameNumber, CurrentHierarchyDepth + 1);
			}
			else if (CurrentHierarchyDepth >= c_MaximumHierarchyDepthBeforeAssumingACycle)
			{
				LOGW("Transform hierarchy deeper than {} levels, assuming a parenting cycle and treating entity {} as a root.", c_MaximumHierarchyDepthBeforeAssumingACycle, entt::to_integral(EntityToResolve));
			}
			// An invalid parent just means the entity behaves like a root, no need to make noise about it every frame
		}

		WorldTransformOfEntity.m_FrameNumberOfLastUpdate = CurrentFrameNumber;
		WorldTransformOfEntity.m_WorldTransformMatrix = ParentWorldTransformMatrix * LocalTransformMatrix;
		return WorldTransformOfEntity.m_WorldTransformMatrix;
	}
}

void updateWorldTransformsFromHierarchy(entt::registry& Registry, std::uint64_t CurrentFrameNumber)
{
	auto EntitiesWithTransforms = Registry.view<Components::TransformComponent, Components::WorldTransformComponent>();
	for (const entt::entity CurrentEntity : EntitiesWithTransforms)
	{
		resolveWorldTransformOfEntity(Registry, CurrentEntity, CurrentFrameNumber, 0);
	}
}

}
