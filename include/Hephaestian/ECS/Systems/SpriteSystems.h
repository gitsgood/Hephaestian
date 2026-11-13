#ifndef HEPHAESTIAN_SPRITESYSTEMS_H
#define HEPHAESTIAN_SPRITESYSTEMS_H

#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

// Advances every playing SpriteAnimatorComponent and writes the frame it lands on into the entity's SpriteRendererComponent
void updateSpriteAnimations(entt::registry& Registry, float DeltaTimeInSeconds);

// Appends one RenderInstance per visible SpriteRendererComponent (alpha cutout, billboarded as asked, unlit unless lit).
// Doesn't clear the vector: call it after the models were added.
void appendRenderInstancesFromSpriteRenderers(const entt::registry& Registry, std::vector<Vulkanised::RenderInstance>& RenderInstancesToFill);

// Switches the animator to the clip with that name in the sprite's sheet. Restarts it if asked, or if it's another clip.
// Returns false (and changes nothing) if the sheet has no such clip.
bool playSpriteAnimationClip(Components::SpriteAnimatorComponent& Animator, const Components::SpriteRendererComponent& Sprite, std::string_view ClipName, bool ShouldRestartIfAlreadyPlaying = false);

}

#endif // !HEPHAESTIAN_SPRITESYSTEMS_H
