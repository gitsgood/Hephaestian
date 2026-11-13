#ifndef HEPHAESTIAN_SCENEFILE_H
#define HEPHAESTIAN_SCENEFILE_H

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "glaze/glaze.hpp"

/*
* The on-disk layout of a scene (assets/Scenes/<name>.json). Plain data only: Scene::loadFromJsonFile turns it into
* entities and components, Scene::saveToJsonFile turns entities back into it.
*
*	{
*	  "assets": {
*	    "models":   { "Quarry": "Models/laz_mesh.obj" },		<- name: path relative to assets/
*	    "textures": { "Logo": "resources/Hephaestos.png" },
*	    "spriteSheets": { "Slime": "Sprites/Slime.spritesheet.json" }
*	  },
*	  "clearColour": [0.1, 0.1, 0.1],							<- optional: overrides HephaestianSettings while this scene is loaded
*	  "lightPosition": [20, 40, 15],							<- optional, same
*	  "lightColour": [1, 1, 1],									<- optional, same
*	  "entities": [
*	    { "id": "Player", "position": [0, 1.7, 3], "freeFlyController": { "movementSpeed": 12 } },
*	    { "id": "PlayerCamera", "parent": "Player", "camera": {} },
*	    { "id": "Box", "primitive": "cube", "texture": "Logo", "rotationInDegrees": [0, 45, 0],
*	      "constantRotation": { "axis": [0, 1, 0], "degreesPerSecond": 30 } },
*	    { "id": "Slime", "position": [2, 0, 0], "sprite": { "sheet": "Slime", "billboard": "faceCameraUpright" },
*	      "spriteAnimation": { "clip": "hop" } }
*	  ],
*	  "activeCamera": "PlayerCamera"
*	}
*
* Every block and field is optional (missing ones keep the defaults below), and unknown keys are ignored.
*/
namespace Hephaestian::SceneFile
{

struct SceneFileAssetTable
{
	std::map<std::string, std::string> m_ModelPathsByName;		// Sorted by name, so saved files stay stable and diff nicely
	std::map<std::string, std::string> m_TexturePathsByName;
	std::map<std::string, std::string> m_SpriteSheetPathsByName;	// .spritesheet.json sidecars or Aseprite JSON exports
};

struct SceneFileCameraBlock
{
	float m_VerticalFieldOfViewInDegrees{ 60.0f };
	float m_NearClippingPlaneDistance{ 0.1f };
	float m_FarClippingPlaneDistance{ 1000.0f };
};

struct SceneFileFreeFlyControllerBlock
{
	float m_MovementSpeedInUnitsPerSecond{ 3.0f };
	float m_SprintMovementSpeedMultiplier{ 3.0f };
	float m_MouseLookSensitivityInRadiansPerPixel{ 0.0025f };
};

struct SceneFileConstantRotationBlock
{
	std::array<float, 3> m_RotationAxis{ 0.0f, 1.0f, 0.0f };
	float m_RotationSpeedInDegreesPerSecond{ 45.0f };
};

// Draws one frame of a sprite sheet from the asset table (see SpriteRendererComponent)
struct SceneFileSpriteBlock
{
	std::string m_SpriteSheetAssetName;
	std::uint32_t m_FrameIndex{ 0 };
	std::array<float, 3> m_Tint{ 1.0f, 1.0f, 1.0f };
	std::array<float, 2> m_Pivot{ 0.5f, 0.0f };				// (0.5, 0) = bottom centre
	std::string m_BillboardModeName{ "none" };				// "none", "faceCamera" or "faceCameraUpright"
	bool m_IsFlippedHorizontally{ false };
	bool m_IsFlippedVertically{ false };
	bool m_IsLit{ false };
};

// Plays one of the sprite sheet's clips on the entity's sprite
struct SceneFileSpriteAnimationBlock
{
	std::string m_ClipName;
	float m_PlaybackSpeed{ 1.0f };
	bool m_IsPlaying{ true };
};

struct SceneFileEntity
{
	std::string m_SceneIdentifier;								// Unique within the scene
	std::optional<std::string> m_DisplayName;					// Defaults to the id
	std::optional<std::string> m_ParentSceneIdentifier;

	std::array<float, 3> m_Position{ 0.0f, 0.0f, 0.0f };			// Relative to the parent, if there is one
	std::array<float, 3> m_RotationInDegrees{ 0.0f, 0.0f, 0.0f };	// Euler angles (pitch x, yaw y, roll z)
	std::array<float, 3> m_Scale{ 1.0f, 1.0f, 1.0f };

	// What it draws: a model from the asset table OR a built-in primitive, optionally with a texture from the asset table
	std::optional<std::string> m_ModelAssetName;
	std::optional<std::string> m_PrimitiveShapeName;
	std::optional<std::string> m_TextureAssetName;
	std::optional<bool> m_IsVisible;

	std::optional<SceneFileCameraBlock> m_Camera;
	std::optional<SceneFileFreeFlyControllerBlock> m_FreeFlyController;
	std::optional<SceneFileConstantRotationBlock> m_ConstantRotation;
	std::optional<SceneFileSpriteBlock> m_Sprite;
	std::optional<SceneFileSpriteAnimationBlock> m_SpriteAnimation;
};

struct SceneFileContents
{
	SceneFileAssetTable m_Assets;
	std::optional<std::array<float, 3>> m_ClearColour;
	std::optional<std::array<float, 3>> m_LightWorldPosition;
	std::optional<std::array<float, 3>> m_LightColour;
	std::vector<SceneFileEntity> m_Entities;
	std::optional<std::string> m_ActiveCameraSceneIdentifier;
};

}

// JSON key names: readable in the file, m_ names in C++ (same approach as HephaestianSettings)
template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileAssetTable>
{
	using T = Hephaestian::SceneFile::SceneFileAssetTable;
	static constexpr auto value = glz::object("models", &T::m_ModelPathsByName, "textures", &T::m_TexturePathsByName, "spriteSheets", &T::m_SpriteSheetPathsByName);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileCameraBlock>
{
	using T = Hephaestian::SceneFile::SceneFileCameraBlock;
	static constexpr auto value = glz::object(
		"verticalFieldOfViewInDegrees", &T::m_VerticalFieldOfViewInDegrees,
		"nearClippingPlane", &T::m_NearClippingPlaneDistance,
		"farClippingPlane", &T::m_FarClippingPlaneDistance);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileFreeFlyControllerBlock>
{
	using T = Hephaestian::SceneFile::SceneFileFreeFlyControllerBlock;
	static constexpr auto value = glz::object(
		"movementSpeed", &T::m_MovementSpeedInUnitsPerSecond,
		"sprintMultiplier", &T::m_SprintMovementSpeedMultiplier,
		"mouseSensitivity", &T::m_MouseLookSensitivityInRadiansPerPixel);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileConstantRotationBlock>
{
	using T = Hephaestian::SceneFile::SceneFileConstantRotationBlock;
	static constexpr auto value = glz::object("axis", &T::m_RotationAxis, "degreesPerSecond", &T::m_RotationSpeedInDegreesPerSecond);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileSpriteBlock>
{
	using T = Hephaestian::SceneFile::SceneFileSpriteBlock;
	static constexpr auto value = glz::object(
		"sheet", &T::m_SpriteSheetAssetName,
		"frame", &T::m_FrameIndex,
		"tint", &T::m_Tint,
		"pivot", &T::m_Pivot,
		"billboard", &T::m_BillboardModeName,
		"flipX", &T::m_IsFlippedHorizontally,
		"flipY", &T::m_IsFlippedVertically,
		"lit", &T::m_IsLit);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileSpriteAnimationBlock>
{
	using T = Hephaestian::SceneFile::SceneFileSpriteAnimationBlock;
	static constexpr auto value = glz::object("clip", &T::m_ClipName, "speed", &T::m_PlaybackSpeed, "playing", &T::m_IsPlaying);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileEntity>
{
	using T = Hephaestian::SceneFile::SceneFileEntity;
	static constexpr auto value = glz::object(
		"id", &T::m_SceneIdentifier,
		"name", &T::m_DisplayName,
		"parent", &T::m_ParentSceneIdentifier,
		"position", &T::m_Position,
		"rotationInDegrees", &T::m_RotationInDegrees,
		"scale", &T::m_Scale,
		"model", &T::m_ModelAssetName,
		"primitive", &T::m_PrimitiveShapeName,
		"texture", &T::m_TextureAssetName,
		"visible", &T::m_IsVisible,
		"camera", &T::m_Camera,
		"freeFlyController", &T::m_FreeFlyController,
		"constantRotation", &T::m_ConstantRotation,
		"sprite", &T::m_Sprite,
		"spriteAnimation", &T::m_SpriteAnimation);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileContents>
{
	using T = Hephaestian::SceneFile::SceneFileContents;
	static constexpr auto value = glz::object(
		"assets", &T::m_Assets,
		"clearColour", &T::m_ClearColour,
		"lightPosition", &T::m_LightWorldPosition,
		"lightColour", &T::m_LightColour,
		"entities", &T::m_Entities,
		"activeCamera", &T::m_ActiveCameraSceneIdentifier);
};

#endif // !HEPHAESTIAN_SCENEFILE_H
