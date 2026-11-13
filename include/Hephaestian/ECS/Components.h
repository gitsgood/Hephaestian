#ifndef HEPHAESTIAN_COMPONENTS_H
#define HEPHAESTIAN_COMPONENTS_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <entt/entt.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Hephaestian/Audio/AudioEngine.h"

/*
* Every component is plain data. All the logic lives in the systems (see ECS/Systems/).
*
* World conventions (same as GLM and the Vulkanised shaders): right-handed, +Y up, an unrotated entity looks down -Z.
*/
namespace Hephaestian::Assets { struct SpriteSheet; }

namespace Hephaestian::Components
{

// Purely for humans (debugging, logging, eventually an editor)
struct NameComponent
{
	std::string m_EntityName;
};

// Where the entity is, RELATIVE TO ITS PARENT (or to the world if it has no ParentEntityComponent)
struct TransformComponent
{
	glm::vec3 m_LocalPosition{ 0.0f };
	glm::quat m_LocalRotation{ 1.0f, 0.0f, 0.0f, 0.0f };	// Identity (w, x, y, z)
	glm::vec3 m_LocalScale{ 1.0f };

	[[nodiscard]] glm::mat4 calculateLocalTransformMatrix() const noexcept
	{
		// Translate * Rotate * Scale, the usual order
		return glm::translate(glm::mat4(1.0f), m_LocalPosition) * glm::mat4_cast(m_LocalRotation) * glm::scale(glm::mat4(1.0f), m_LocalScale);
	}
};

// Where the entity ended up in the world, after its whole parent chain is applied.
// Written by TransformHierarchySystem only, read by everyone else. Never write to it yourself, edit TransformComponent instead.
struct WorldTransformComponent
{
	glm::mat4 m_WorldTransformMatrix{ 1.0f };
	std::uint64_t m_FrameNumberOfLastUpdate{ 0 };	// Lets the hierarchy system resolve every entity exactly once per frame, whatever the order
};

// Makes this entity's TransformComponent relative to another entity. This is how a camera gets attached to anything:
// give the camera entity a ParentEntityComponent pointing at the thing to follow, and its local transform becomes the offset.
// Use Scene::attachEntityToParent rather than adding this directly, it refuses cycles.
struct ParentEntityComponent
{
	entt::entity m_ParentEntity{ entt::null };
};

// Draws a (possibly multi-mesh) model created through VulkanRenderer::createModel / loadModel at this entity's world transform
struct ModelRendererComponent
{
	std::uint32_t m_ModelIdentifier{ 0 };
	bool m_IsVisible{ true };
	std::optional<std::uint32_t> m_TextureOverrideIdentifier;	// When set, every piece of the model uses this texture instead of its own
};

// What a scene file said about this entity, so the scene can be written back out the same way ("Save scene").
// Entities made in code don't need it: they get an id generated from their name when saved.
struct SceneSerializationComponent
{
	std::string m_SceneIdentifier;		// The entity's unique "id" in the scene file (parents and the active camera refer to it)
	std::string m_ModelAssetName;		// Name in the scene's asset table, or empty
	std::string m_PrimitiveShapeName;	// "quad", "cube", "sphere", or empty
	std::string m_TextureAssetName;		// Texture override's name in the asset table, or empty
	std::string m_SpriteSheetAssetName;	// Sprite sheet's name in the asset table, or empty
	std::string m_SoundAssetName;		// Audio source's sound name in the asset table, or empty
};

// Turns the entity into something that can be looked through. The entity's world transform IS the eye.
// Which camera is actually used is decided by Scene::setActiveCameraEntity.
struct CameraComponent
{
	float m_VerticalFieldOfViewInDegrees{ 60.0f };
	float m_NearClippingPlaneDistance{ 0.1f };
	float m_FarClippingPlaneDistance{ 1000.0f };
};

// Fly around with WASD + QE + mouse look. Put it on the camera entity for a free camera,
// or on a "player" entity with the camera parented to it for a camera that follows that player around.
struct FreeFlyMovementControllerComponent
{
	float m_MovementSpeedInUnitsPerSecond{ 3.0f };
	float m_SprintMovementSpeedMultiplier{ 3.0f };
	float m_MouseLookSensitivityInRadiansPerPixel{ 0.0025f };
	float m_MaximumPitchAngleInRadians{ glm::radians(89.0f) };	// Stops the camera flipping over when looking straight up or down
	bool m_OnlyLookAroundWhileMouseCursorIsCaptured{ true };

	// Current orientation, kept as angles because rebuilding a quaternion from yaw/pitch never accumulates roll
	float m_CurrentYawAngleInRadians{ 0.0f };	// Around world +Y
	float m_CurrentPitchAngleInRadians{ 0.0f };	// Around local +X

	// Key bindings (GLFW key codes), so different controllers can use different keys
	int m_MoveForwardKey{ GLFW_KEY_W };
	int m_MoveBackwardKey{ GLFW_KEY_S };
	int m_MoveLeftKey{ GLFW_KEY_A };
	int m_MoveRightKey{ GLFW_KEY_D };
	int m_MoveUpKey{ GLFW_KEY_E };
	int m_MoveDownKey{ GLFW_KEY_Q };
	int m_SprintKey{ GLFW_KEY_LEFT_SHIFT };
};

// Spins the entity forever. Mostly here for demo purposes (it replaces the hand-written rotation that used to live in main)
struct ConstantRotationComponent
{
	glm::vec3 m_RotationAxis{ 0.0f, 1.0f, 0.0f };
	float m_RotationSpeedInDegreesPerSecond{ 45.0f };
};


// How a sprite turns towards the camera
enum class SpriteBillboardMode
{
	None,				// Placed like any other object, by its transform (signs, decals, a flat 2D-style layer). Visible from both sides.
	FaceCamera,			// Always fully facing the camera, whatever angle it's seen from (particles, effects, pickups)
	FaceCameraUpright	// Turns towards the camera around the world's vertical axis only (characters, trees: they never tilt)
};

// Draws one frame of a sprite sheet as a quad at this entity's world transform. Its size is the frame's size in pixels
// divided by the sheet's pixels per unit, times the transform's scale. Pixels under 50% alpha are cut out.
struct SpriteRendererComponent
{
	std::shared_ptr<const Assets::SpriteSheet> m_SpriteSheet;	// From AssetLibrary::getOrLoadSpriteSheet, shared by every sprite using it
	std::uint32_t m_FrameIndex{ 0 };							// Set by a SpriteAnimatorComponent, if there is one
	glm::vec3 m_Tint{ 1.0f };									// Multiplies the image's colours (picked in sRGB, like every colour)
	glm::vec2 m_Pivot{ 0.5f, 0.0f };							// The point of the frame sitting at the entity's position: (0.5, 0) = bottom centre (feet), (0.5, 0.5) = centre
	SpriteBillboardMode m_BillboardMode{ SpriteBillboardMode::None };
	bool m_IsFlippedHorizontally{ false };						// Mirrored left-right (a character walking the other way)
	bool m_IsFlippedVertically{ false };
	bool m_IsLit{ false };										// Off: shown exactly as drawn. On: lit like any other surface.
	bool m_IsVisible{ true };
};

// Plays the sprite sheet's animation clips on the SpriteRendererComponent of the same entity (it picks the frame)
struct SpriteAnimatorComponent
{
	std::uint32_t m_ClipIndex{ 0 };					// In the sprite sheet's clips (use Systems::playSpriteAnimationClip to change it by name)
	float m_ElapsedTimeInSeconds{ 0.0f };			// Time into the clip
	float m_PlaybackSpeed{ 1.0f };					// 2 = twice as fast, negative = backwards
	bool m_IsPlaying{ true };
	bool m_HasFinished{ false };					// A "once" clip reached its last frame
};

// Plays a sound from this entity: music (not spatial: the same in both ears, wherever you stand), or a sound placed in
// the world at the entity's position (spatial: panned and faded with distance, it follows the entity as it moves).
// Started by the audio system when m_ShouldPlayOnStart, or by Systems::playAudioSource. Destroying the entity stops it.
struct AudioSourceComponent
{
	std::shared_ptr<const Audio::SoundAsset> m_Sound;	// From AssetLibrary::getOrLoadSound
	float m_Volume{ 1.0f };
	float m_Pitch{ 1.0f };
	bool m_ShouldLoop{ false };
	bool m_ShouldPlayOnStart{ true };
	bool m_IsSpatial{ false };
	float m_MinimumDistance{ 1.0f };					// Spatial only: full volume up to here
	float m_MaximumDistance{ 50.0f };					// Spatial only: the far end of the fade (silent from here with linear falloff)
	float m_Rolloff{ 1.0f };
	Audio::DistanceFalloff m_Falloff{ Audio::DistanceFalloff::Inverse };

	// Run time
	Audio::VoiceHandle m_Voice;							// While it plays
	bool m_HasBeenStarted{ false };						// Play on start only happens once
};

// The ears. Without one in the scene, the listener follows the active camera; with one, it's this entity
// (its world position, and its forward (-Z) and up (+Y) directions).
struct AudioListenerComponent
{
};

}

#endif // !HEPHAESTIAN_COMPONENTS_H
