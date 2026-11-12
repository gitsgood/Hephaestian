#include "Hephaestian/ECS/Systems/RenderSystem.h"

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

void buildRenderInstancesFromModelRenderers(const entt::registry& Registry, const Vulkanised::VulkanRenderer& Renderer, std::vector<Vulkanised::RenderInstance>& RenderInstancesToFill)
{
	RenderInstancesToFill.clear();

	auto EntitiesWithModels = Registry.view<Components::WorldTransformComponent, Components::ModelRendererComponent>();

	EntitiesWithModels.each([&](const Components::WorldTransformComponent& WorldTransform, const Components::ModelRendererComponent& ModelRenderer)
	{
		if (!ModelRenderer.m_IsVisible || !Renderer.isModelIdentifierValid(ModelRenderer.m_ModelIdentifier))
		{
			return;
		}

		// A multi-mesh model becomes one instance per submesh. The renderer then sorts and batches them by mesh and texture,
		// so 500 entities sharing the same 3-mesh model still only cost 3 draw calls.
		const Vulkanised::ModelAsset& ModelToDraw = Renderer.getModelAsset(ModelRenderer.m_ModelIdentifier);
		for (const Vulkanised::ModelSubmesh& CurrentSubmesh : ModelToDraw.m_Submeshes)
		{
			RenderInstancesToFill.push_back({
				CurrentSubmesh.m_MeshIdentifier,
				ModelRenderer.m_TextureOverrideIdentifier.value_or(CurrentSubmesh.m_TextureIdentifier),	// A texture override replaces every piece's own texture
				WorldTransform.m_WorldTransformMatrix * CurrentSubmesh.m_LocalTransformRelativeToModelRoot
			});
		}
	});
}

}
