#ifndef HEPHAESTIAN_COMPONENTS_H
#define HEPHAESTIAN_COMPONENTS_H

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

/*
* Every component is plain data. All the logic lives in the systems (see ECS/Systems/).
*
* World conventions (same as GLM and the Vulkanised shaders): right-handed, +Y up, an unrotated entity looks down -Z.
*/
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

}

#endif // !HEPHAESTIAN_COMPONENTS_H
