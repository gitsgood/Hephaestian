#include "Hephaestian/ECS/Systems/CameraSystem.h"

#include "Hephaestian/ECS/Components.h"

namespace Hephaestian::Systems
{

bool isEntityUsableAsCamera(const entt::registry& Registry, entt::entity CandidateCameraEntity)
{
	return Registry.valid(CandidateCameraEntity) && Registry.all_of<Components::CameraComponent, Components::WorldTransformComponent>(CandidateCameraEntity);
}

bool submitActiveCameraToRenderer(const entt::registry& Registry, entt::entity ActiveCameraEntity, Vulkanised::VulkanRenderer& Renderer)
{
	if (!isEntityUsableAsCamera(Registry, ActiveCameraEntity))
	{
		return false;
	}

	const auto& Camera = Registry.get<Components::CameraComponent>(ActiveCameraEntity);
	const glm::mat4& CameraWorldTransformMatrix = Registry.get<Components::WorldTransformComponent>(ActiveCameraEntity).m_WorldTransformMatrix;

	// The camera may be parented to something scaled (a scaled-up model, say). Scale must not leak into the view matrix,
	// or the whole world would look squashed, so rebuild a pure rotation + translation matrix from the world transform.
	glm::mat4 CameraWorldTransformWithoutScale{ 1.0f };
	CameraWorldTransformWithoutScale[0] = glm::vec4(glm::normalize(glm::vec3(CameraWorldTransformMatrix[0])), 0.0f);
	CameraWorldTransformWithoutScale[1] = glm::vec4(glm::normalize(glm::vec3(CameraWorldTransformMatrix[1])), 0.0f);
	CameraWorldTransformWithoutScale[2] = glm::vec4(glm::normalize(glm::vec3(CameraWorldTransformMatrix[2])), 0.0f);
	CameraWorldTransformWithoutScale[3] = CameraWorldTransformMatrix[3];

	// The view matrix is simply "the world, as seen from the camera", i.e. the inverse of where the camera is
	const glm::mat4 CameraViewMatrix = glm::inverse(CameraWorldTransformWithoutScale);
	const glm::vec3 CameraWorldPosition = glm::vec3(CameraWorldTransformMatrix[3]);

	Renderer.setCameraViewAndLens(CameraViewMatrix, CameraWorldPosition, glm::radians(Camera.m_VerticalFieldOfViewInDegrees), Camera.m_NearClippingPlaneDistance, Camera.m_FarClippingPlaneDistance);
	return true;
}

}
