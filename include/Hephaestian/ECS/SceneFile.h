/**
 * @file SceneFile.h
 * @brief The on-disk layout of a scene (`assets/Scenes/<name>.json`).
 *
 * Plain data only: Scene::loadFromJsonFile turns it into entities and components, Scene::saveToJsonFile turns entities
 * back into it.
 *
 * @code{.json}
 * {
 *   "assets": {
 *     "models":   { "Quarry": "Models/laz_mesh.obj" },            <- name: path relative to assets/
 *     "textures": { "Logo": "resources/Hephaestos.png" },
 *     "spriteSheets": { "Slime": "Sprites/Slime.spritesheet.json" },
 *     "sounds": { "Theme": "Audio/Theme.mp3" }
 *   },
 *   "clearColour": [0.1, 0.1, 0.1],                               <- optional: overrides HephaestianSettings while this scene is loaded
 *   "lightPosition": [20, 40, 15],                                <- optional, same
 *   "lightColour": [1, 1, 1],                                     <- optional, same
 *   "audioVolume": 0.8,                                           <- optional, same (0 to 1)
 *   "entities": [
 *     { "id": "Player", "position": [0, 1.7, 3], "freeFlyController": { "movementSpeed": 12 } },
 *     { "id": "PlayerCamera", "parent": "Player", "camera": {} },
 *     { "id": "Box", "primitive": "cube", "texture": "Logo", "rotationInDegrees": [0, 45, 0],
 *       "constantRotation": { "axis": [0, 1, 0], "degreesPerSecond": 30 } },
 *     { "id": "Slime", "position": [2, 0, 0], "sprite": { "sheet": "Slime", "billboard": "faceCameraUpright" },
 *       "spriteAnimation": { "clip": "hop" } },
 *     { "id": "Music", "audio": { "sound": "Theme", "loop": true } },                       <- the same in both ears
 *     { "id": "Radio", "position": [3, 1, 0], "audio": { "sound": "Theme", "spatial": true,  <- in the world
 *       "minDistance": 2, "maxDistance": 25, "falloff": "linear" } },
 *     { "id": "Ears", "parent": "Player", "audioListener": {} }                             <- optional: the active camera otherwise
 *   ],
 *   "activeCamera": "PlayerCamera"
 * }
 * @endcode
 *
 * Every block and field is optional (missing ones keep the defaults below), and unknown keys are ignored.
 * Each member's JSON key is given first in its description.
 */
#ifndef HEPHAESTIAN_SCENEFILE_H
#define HEPHAESTIAN_SCENEFILE_H

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "glaze/glaze.hpp"

/**
 * @brief The scene file's structs, read and written by glaze.
 */
namespace Hephaestian::SceneFile
{

/**
 * @brief "assets": every asset the scene uses, by the name its entities refer to it with.
 *
 * Sorted by name (std::map), so saved files stay stable and diff nicely.
 */
struct SceneFileAssetTable
{
	std::map<std::string, std::string> m_ModelPathsByName;			///< "models": .gltf, .glb, .obj or .fbx
	std::map<std::string, std::string> m_TexturePathsByName;		///< "textures": images
	std::map<std::string, std::string> m_SpriteSheetPathsByName;	///< "spriteSheets": .spritesheet.json sidecars, Aseprite JSON exports, or folders
	std::map<std::string, std::string> m_SoundPathsByName;			///< "sounds": .mp3, .wav, .flac or .ogg
};

/**
 * @brief "camera": makes the entity a camera (see CameraComponent).
 */
struct SceneFileCameraBlock
{
	float m_VerticalFieldOfViewInDegrees{ 60.0f };	///< "verticalFieldOfViewInDegrees"
	float m_NearClippingPlaneDistance{ 0.1f };		///< "nearClippingPlane"
	float m_FarClippingPlaneDistance{ 1000.0f };	///< "farClippingPlane"
};

/**
 * @brief "freeFlyController": WASD + mouse look (see FreeFlyMovementControllerComponent).
 */
struct SceneFileFreeFlyControllerBlock
{
	float m_MovementSpeedInUnitsPerSecond{ 3.0f };				///< "movementSpeed"
	float m_SprintMovementSpeedMultiplier{ 3.0f };				///< "sprintMultiplier"
	float m_MouseLookSensitivityInRadiansPerPixel{ 0.0025f };	///< "mouseSensitivity"
};

/**
 * @brief "constantRotation": spins forever (see ConstantRotationComponent).
 */
struct SceneFileConstantRotationBlock
{
	std::array<float, 3> m_RotationAxis{ 0.0f, 1.0f, 0.0f };	///< "axis"
	float m_RotationSpeedInDegreesPerSecond{ 45.0f };			///< "degreesPerSecond"
};

/**
 * @brief "sprite": draws one frame of a sprite sheet from the asset table (see SpriteRendererComponent).
 */
struct SceneFileSpriteBlock
{
	std::string m_SpriteSheetAssetName;				///< "sheet": its name in the asset table
	std::uint32_t m_FrameIndex{ 0 };				///< "frame" (an animation overrides it)
	std::array<float, 3> m_Tint{ 1.0f, 1.0f, 1.0f };	///< "tint": sRGB, 0 to 1
	std::array<float, 2> m_Pivot{ 0.5f, 0.0f };		///< "pivot": (0.5, 0) = bottom centre
	std::string m_BillboardModeName{ "none" };		///< "billboard": "none", "faceCamera" or "faceCameraUpright"
	bool m_IsFlippedHorizontally{ false };			///< "flipX"
	bool m_IsFlippedVertically{ false };			///< "flipY"
	bool m_IsLit{ false };							///< "lit"
};

/**
 * @brief "spriteAnimation": plays one of the sprite sheet's clips on the entity's sprite (see SpriteAnimatorComponent).
 */
struct SceneFileSpriteAnimationBlock
{
	std::string m_ClipName;			///< "clip"
	float m_PlaybackSpeed{ 1.0f };	///< "speed"
	bool m_IsPlaying{ true };		///< "playing"
};

/**
 * @brief "audio": plays a sound from the asset table on this entity (see AudioSourceComponent).
 */
struct SceneFileAudioBlock
{
	std::string m_SoundAssetName;				///< "sound": its name in the asset table
	float m_Volume{ 1.0f };						///< "volume"
	float m_Pitch{ 1.0f };						///< "pitch"
	bool m_ShouldLoop{ false };					///< "loop"
	bool m_ShouldPlayOnStart{ true };			///< "playOnStart"
	bool m_IsSpatial{ false };					///< "spatial": true places it at the entity, panned and faded with distance
	float m_MinimumDistance{ 1.0f };			///< "minDistance"
	float m_MaximumDistance{ 50.0f };			///< "maxDistance"
	float m_Rolloff{ 1.0f };					///< "rolloff"
	std::string m_FalloffName{ "inverse" };		///< "falloff": "inverse" (like real sound) or "linear" (silent at the maximum distance)
};

/**
 * @brief "audioListener": makes this entity the ears instead of the active camera. Nothing to set: "audioListener": {}
 */
struct SceneFileAudioListenerBlock
{
};

/**
 * @brief One of "entities": an entity and every component it has.
 */
struct SceneFileEntity
{
	std::string m_SceneIdentifier;							///< "id": unique within the scene
	std::optional<std::string> m_DisplayName;				///< "name": defaults to the id
	std::optional<std::string> m_ParentSceneIdentifier;		///< "parent": another entity's id

	std::array<float, 3> m_Position{ 0.0f, 0.0f, 0.0f };			///< "position": relative to the parent, if there is one
	std::array<float, 3> m_RotationInDegrees{ 0.0f, 0.0f, 0.0f };	///< "rotationInDegrees": Euler angles (pitch x, yaw y, roll z)
	std::array<float, 3> m_Scale{ 1.0f, 1.0f, 1.0f };				///< "scale"

	// What it draws: a model from the asset table OR a built-in primitive, optionally with a texture from the asset table
	std::optional<std::string> m_ModelAssetName;		///< "model": a name in the asset table
	std::optional<std::string> m_PrimitiveShapeName;	///< "primitive": "quad", "cube", "sphere" or "sprite"
	std::optional<std::string> m_TextureAssetName;		///< "texture": replaces the model's own textures
	std::optional<bool> m_IsVisible;					///< "visible"

	std::optional<SceneFileCameraBlock> m_Camera;						///< "camera"
	std::optional<SceneFileFreeFlyControllerBlock> m_FreeFlyController;	///< "freeFlyController"
	std::optional<SceneFileConstantRotationBlock> m_ConstantRotation;	///< "constantRotation"
	std::optional<SceneFileSpriteBlock> m_Sprite;						///< "sprite"
	std::optional<SceneFileSpriteAnimationBlock> m_SpriteAnimation;		///< "spriteAnimation"
	std::optional<SceneFileAudioBlock> m_Audio;							///< "audio"
	std::optional<SceneFileAudioListenerBlock> m_AudioListener;			///< "audioListener"
};

/**
 * @brief A whole scene file.
 */
struct SceneFileContents
{
	SceneFileAssetTable m_Assets;								///< "assets"
	std::optional<std::array<float, 3>> m_ClearColour;			///< "clearColour"
	std::optional<std::array<float, 3>> m_LightWorldPosition;	///< "lightPosition"
	std::optional<std::array<float, 3>> m_LightColour;			///< "lightColour"
	std::optional<float> m_AudioVolume;							///< "audioVolume"
	std::vector<SceneFileEntity> m_Entities;					///< "entities"
	std::optional<std::string> m_ActiveCameraSceneIdentifier;	///< "activeCamera": an entity's id
};

}

/// @cond GLAZE_META
// JSON key names: readable in the file, m_ names in C++ (same approach as HephaestianSettings)
template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileAssetTable>
{
	using T = Hephaestian::SceneFile::SceneFileAssetTable;
	static constexpr auto value = glz::object("models", &T::m_ModelPathsByName, "textures", &T::m_TexturePathsByName, "spriteSheets", &T::m_SpriteSheetPathsByName,
		"sounds", &T::m_SoundPathsByName);
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
struct glz::meta<Hephaestian::SceneFile::SceneFileAudioBlock>
{
	using T = Hephaestian::SceneFile::SceneFileAudioBlock;
	static constexpr auto value = glz::object(
		"sound", &T::m_SoundAssetName,
		"volume", &T::m_Volume,
		"pitch", &T::m_Pitch,
		"loop", &T::m_ShouldLoop,
		"playOnStart", &T::m_ShouldPlayOnStart,
		"spatial", &T::m_IsSpatial,
		"minDistance", &T::m_MinimumDistance,
		"maxDistance", &T::m_MaximumDistance,
		"rolloff", &T::m_Rolloff,
		"falloff", &T::m_FalloffName);
};

template <>
struct glz::meta<Hephaestian::SceneFile::SceneFileAudioListenerBlock>
{
	static constexpr auto value = glz::object();
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
		"spriteAnimation", &T::m_SpriteAnimation,
		"audio", &T::m_Audio,
		"audioListener", &T::m_AudioListener);
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
		"audioVolume", &T::m_AudioVolume,
		"entities", &T::m_Entities,
		"activeCamera", &T::m_ActiveCameraSceneIdentifier);
};
/// @endcond

#endif // !HEPHAESTIAN_SCENEFILE_H
