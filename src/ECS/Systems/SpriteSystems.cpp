#include "Hephaestian/ECS/Systems/SpriteSystems.h"

#include <algorithm>

#include <Vulkanised/Rendering/ColourSpace.h>

#include "Hephaestian/Assets/SpriteSheet.h"

namespace Hephaestian::Systems
{

void updateSpriteAnimations(entt::registry& Registry, float DeltaTimeInSeconds)
{
	auto AnimatedSprites = Registry.view<Components::SpriteAnimatorComponent, Components::SpriteRendererComponent>();
	AnimatedSprites.each([DeltaTimeInSeconds](Components::SpriteAnimatorComponent& Animator, Components::SpriteRendererComponent& Sprite)
	{
		if (Sprite.m_SpriteSheet == nullptr || Animator.m_ClipIndex >= Sprite.m_SpriteSheet->m_Clips.size())
		{
			return;
		}
		const Assets::SpriteAnimationClip& Clip = Sprite.m_SpriteSheet->m_Clips[Animator.m_ClipIndex];

		if (Animator.m_IsPlaying && !Animator.m_HasFinished)
		{
			Animator.m_ElapsedTimeInSeconds += DeltaTimeInSeconds * Animator.m_PlaybackSpeed;
			if (Clip.m_ShouldLoop)
			{
				// Kept within one loop, so the float never grows until it loses precision (and backwards playback wraps too)
				Animator.m_ElapsedTimeInSeconds = std::fmod(Animator.m_ElapsedTimeInSeconds, Clip.m_TotalDurationInSeconds);
				if (Animator.m_ElapsedTimeInSeconds < 0.0f)
				{
					Animator.m_ElapsedTimeInSeconds += Clip.m_TotalDurationInSeconds;
				}
			}
			else if (Animator.m_ElapsedTimeInSeconds >= Clip.m_TotalDurationInSeconds || (Animator.m_ElapsedTimeInSeconds <= 0.0f && Animator.m_PlaybackSpeed < 0.0f))
			{
				Animator.m_ElapsedTimeInSeconds = std::clamp(Animator.m_ElapsedTimeInSeconds, 0.0f, Clip.m_TotalDurationInSeconds);
				Animator.m_HasFinished = true;	// Holds its last frame
			}
		}
		Sprite.m_FrameIndex = Clip.getFrameIndexAtTime(Animator.m_ElapsedTimeInSeconds);
	});
}

void appendRenderInstancesFromSpriteRenderers(const entt::registry& Registry, std::vector<Vulkanised::RenderInstance>& RenderInstancesToFill)
{
	auto Sprites = Registry.view<Components::WorldTransformComponent, Components::SpriteRendererComponent>();
	Sprites.each([&RenderInstancesToFill](const Components::WorldTransformComponent& WorldTransform, const Components::SpriteRendererComponent& Sprite)
	{
		if (!Sprite.m_IsVisible || Sprite.m_SpriteSheet == nullptr)
		{
			return;
		}
		const Assets::SpriteSheet& Sheet = *Sprite.m_SpriteSheet;
		const Assets::SpriteSheetFrame& Frame = Sheet.m_Frames[std::min<std::size_t>(Sprite.m_FrameIndex, Sheet.m_Frames.size() - 1)];

		Vulkanised::RenderInstance SpriteInstance{};
		SpriteInstance.meshID = Sheet.m_QuadMeshIdentifier;
		SpriteInstance.textureID = Sheet.m_PageTextureIdentifiers[Frame.m_PageIndex];	// Long animations span several images

		// The 1 x 1 quad, stretched to the frame's size in world units (on top of the entity's own scale)
		const glm::vec2 FrameSizeInWorldUnits = Frame.m_SizeInPixels / Sheet.m_PixelsPerUnit;
		SpriteInstance.transform = WorldTransform.m_WorldTransformMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(FrameSizeInWorldUnits, 1.0f));

		// Flipping mirrors the texture coordinates rather than the geometry, so the triangles keep facing the right way
		glm::vec4 TextureCoordinateRect = Frame.m_TextureCoordinateRect;
		if (Sprite.m_IsFlippedHorizontally)
		{
			TextureCoordinateRect.x += TextureCoordinateRect.z;
			TextureCoordinateRect.z = -TextureCoordinateRect.z;
		}
		if (Sprite.m_IsFlippedVertically)
		{
			TextureCoordinateRect.y += TextureCoordinateRect.w;
			TextureCoordinateRect.w = -TextureCoordinateRect.w;
		}
		SpriteInstance.m_TextureCoordinateRect = TextureCoordinateRect;
		SpriteInstance.m_Tint = glm::vec4(Vulkanised::Rendering::convertSrgbColourToLinear(Sprite.m_Tint), 1.0f);

		// The quad spans -0.5..0.5 with the frame's top edge at +0.5, so pivot (0, 0) is the frame's bottom-left corner:
		// moving the quad by (0.5 - pivot) puts the pivot point on the entity's position
		SpriteInstance.m_PivotOffset = glm::vec2(0.5f) - Sprite.m_Pivot;

		SpriteInstance.m_Flags = Sheet.m_BlendMode == Assets::SpriteBlendMode::Additive ? Vulkanised::RenderInstanceFlags::c_Additive : Vulkanised::RenderInstanceFlags::c_AlphaCutout;
		switch (Sprite.m_BillboardMode)
		{
		case Components::SpriteBillboardMode::FaceCamera:			SpriteInstance.m_Flags |= Vulkanised::RenderInstanceFlags::c_FaceCamera; break;
		case Components::SpriteBillboardMode::FaceCameraUpright:	SpriteInstance.m_Flags |= Vulkanised::RenderInstanceFlags::c_FaceCameraUpright; break;
		case Components::SpriteBillboardMode::None:					break;
		}
		if (!Sprite.m_IsLit)
		{
			SpriteInstance.m_Flags |= Vulkanised::RenderInstanceFlags::c_Unlit;
		}

		RenderInstancesToFill.push_back(SpriteInstance);
	});
}

bool playSpriteAnimationClip(Components::SpriteAnimatorComponent& Animator, const Components::SpriteRendererComponent& Sprite, std::string_view ClipName, bool ShouldRestartIfAlreadyPlaying)
{
	if (Sprite.m_SpriteSheet == nullptr)
	{
		return false;
	}
	const std::optional<std::uint32_t> ClipIndex = Sprite.m_SpriteSheet->findClipIndex(ClipName);
	if (!ClipIndex.has_value())
	{
		return false;
	}
	if (ClipIndex.value() != Animator.m_ClipIndex || ShouldRestartIfAlreadyPlaying)
	{
		Animator.m_ClipIndex = ClipIndex.value();
		Animator.m_ElapsedTimeInSeconds = 0.0f;
		Animator.m_HasFinished = false;
	}
	Animator.m_IsPlaying = true;
	return true;
}

}
