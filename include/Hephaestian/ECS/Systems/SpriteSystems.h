/**
 * @file SpriteSystems.h
 * @brief Sprites: animating them, playing clips by name, and turning them into render instances.
 */
#ifndef HEPHAESTIAN_SPRITESYSTEMS_H
#define HEPHAESTIAN_SPRITESYSTEMS_H

#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

/**
 * @brief Advances every playing SpriteAnimatorComponent and writes the frame it lands on into the entity's
 * SpriteRendererComponent.
 * @param Registry The scene's entities.
 * @param DeltaTimeInSeconds Time since the last frame (times each animator's playback speed).
 */
void updateSpriteAnimations(entt::registry& Registry, float DeltaTimeInSeconds);

/**
 * @brief Appends one RenderInstance per visible SpriteRendererComponent.
 *
 * Each is drawn with its sheet's blending (cutout or additive), billboarded as asked, and unlit unless lit.
 * @param Registry The scene's entities.
 * @param RenderInstancesToFill Not cleared: call it after the models were added.
 */
void appendRenderInstancesFromSpriteRenderers(const entt::registry& Registry, std::vector<Vulkanised::RenderInstance>& RenderInstancesToFill);

/**
 * @brief Switches an animator to the clip with that name in the sprite's sheet.
 * @param Animator The animator to switch.
 * @param Sprite The sprite it animates (for its sheet's clips).
 * @param ClipName The clip, as named in the sheet.
 * @param ShouldRestartIfAlreadyPlaying Restart it from its first frame even if it's the clip already playing (another
 * clip always starts from the beginning).
 * @return False (and nothing changes) if the sheet has no such clip.
 */
bool playSpriteAnimationClip(Components::SpriteAnimatorComponent& Animator, const Components::SpriteRendererComponent& Sprite, std::string_view ClipName, bool ShouldRestartIfAlreadyPlaying = false);

}

#endif // !HEPHAESTIAN_SPRITESYSTEMS_H
