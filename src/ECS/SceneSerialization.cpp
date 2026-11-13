// Scene <-> scene file (assets/Scenes/*.json). The rest of Scene lives in Scene.cpp.
#include "Hephaestian/ECS/Scene.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include <Vulkanised/Logger.h>
#include <Vulkanised/utils/FileUtils.h>

#include "Hephaestian/Assets/AssetLibrary.h"
#include "Hephaestian/ECS/Components.h"
#include "Hephaestian/ECS/Systems/SpriteSystems.h"
#include "Hephaestian/Settings/HephaestianSettings.h"

namespace Hephaestian
{

namespace
{
	[[nodiscard]] glm::vec3 convertArrayToVector(const std::array<float, 3>& ArrayToConvert) noexcept
	{
		return { ArrayToConvert[0], ArrayToConvert[1], ArrayToConvert[2] };
	}

	// 4 decimals are plenty for a hand-editable file, and they keep rounding noise like 44.99999 or -0.0000001 out of it
	[[nodiscard]] float roundForSceneFile(float ValueToRound) noexcept
	{
		const float RoundedValue = std::round(ValueToRound * 10000.0f) / 10000.0f;
		return RoundedValue == 0.0f ? 0.0f : RoundedValue;	// No "-0"
	}

	[[nodiscard]] std::array<float, 3> convertVectorToArrayForSceneFile(const glm::vec3& VectorToConvert) noexcept
	{
		return { roundForSceneFile(VectorToConvert.x), roundForSceneFile(VectorToConvert.y), roundForSceneFile(VectorToConvert.z) };
	}

	// Billboard modes as scene files spell them
	constexpr std::array<std::pair<std::string_view, Components::SpriteBillboardMode>, 3> c_SpriteBillboardModeNames{ {
		{ "none", Components::SpriteBillboardMode::None },
		{ "faceCamera", Components::SpriteBillboardMode::FaceCamera },
		{ "faceCameraUpright", Components::SpriteBillboardMode::FaceCameraUpright } } };

	[[nodiscard]] std::optional<Components::SpriteBillboardMode> parseSpriteBillboardModeName(std::string_view BillboardModeName) noexcept
	{
		for (const auto& [CurrentName, CurrentMode] : c_SpriteBillboardModeNames)
		{
			if (CurrentName == BillboardModeName) { return CurrentMode; }
		}
		return std::nullopt;
	}

	// Distance falloffs as scene files spell them
	[[nodiscard]] std::optional<Audio::DistanceFalloff> parseDistanceFalloffName(std::string_view FalloffName) noexcept
	{
		if (FalloffName == "inverse") { return Audio::DistanceFalloff::Inverse; }
		if (FalloffName == "linear") { return Audio::DistanceFalloff::Linear; }
		return std::nullopt;
	}

	[[nodiscard]] std::string_view getDistanceFalloffName(Audio::DistanceFalloff Falloff) noexcept
	{
		return Falloff == Audio::DistanceFalloff::Linear ? "linear" : "inverse";
	}

	[[nodiscard]] std::string_view getSpriteBillboardModeName(Components::SpriteBillboardMode BillboardMode) noexcept
	{
		for (const auto& [CurrentName, CurrentMode] : c_SpriteBillboardModeNames)
		{
			if (CurrentMode == BillboardMode) { return CurrentName; }
		}
		return "none";
	}
}

void Scene::clear()
{
	m_Registry.clear();
	m_ActiveCameraEntity = entt::null;
	m_HasWarnedAboutMissingActiveCamera = false;
	m_LoadedSceneFilePath.clear();
	m_SceneAssetTable = {};
	m_EnvironmentOverrides = {};
}

bool Scene::loadFromJsonFile(const std::filesystem::path& SceneFilePath, Assets::AssetLibrary& AssetLibraryToLoadFrom)
{
	// 0. READ THE FILE. If this fails nothing has been touched yet, so the current scene simply stays.
	SceneFile::SceneFileContents LoadedSceneFile{};
	std::string SceneFileBuffer;
	const glz::error_ctx SceneFileReadError = glz::read_file_json<glz::opts{ .error_on_unknown_keys = false }>(LoadedSceneFile, SceneFilePath.string(), SceneFileBuffer);
	if (SceneFileReadError)
	{
		LOGE("Couldn't load scene {}:\n{}", SceneFilePath.string(), glz::format_error(SceneFileReadError, SceneFileBuffer));
		return false;
	}

	clear();
	m_LoadedSceneFilePath = SceneFilePath;
	m_SceneAssetTable = LoadedSceneFile.m_Assets;
	m_EnvironmentOverrides = { LoadedSceneFile.m_ClearColour, LoadedSceneFile.m_LightWorldPosition, LoadedSceneFile.m_LightColour, LoadedSceneFile.m_AudioVolume };

	// 1. ASSETS: load (or reuse) everything in the asset table once, before any entity needs it
	std::unordered_map<std::string, std::uint32_t> ModelIdentifiersByAssetName;
	for (const auto& [ModelAssetName, ModelRelativePath] : m_SceneAssetTable.m_ModelPathsByName)
	{
		if (const std::optional<Assets::ImportedModel> LoadedModel = AssetLibraryToLoadFrom.getOrLoadModel(ModelRelativePath))
		{
			ModelIdentifiersByAssetName.emplace(ModelAssetName, LoadedModel->m_ModelIdentifier);
		}
		else
		{
			LOGW("Scene {}: model '{}' ({}) couldn't be loaded, entities using it won't be drawn.", SceneFilePath.filename().string(), ModelAssetName, ModelRelativePath);
		}
	}
	std::unordered_map<std::string, std::uint32_t> TextureIdentifiersByAssetName;
	for (const auto& [TextureAssetName, TextureRelativePath] : m_SceneAssetTable.m_TexturePathsByName)
	{
		if (const std::optional<std::uint32_t> LoadedTexture = AssetLibraryToLoadFrom.getOrLoadTexture(TextureRelativePath))
		{
			TextureIdentifiersByAssetName.emplace(TextureAssetName, LoadedTexture.value());
		}
		else
		{
			LOGW("Scene {}: texture '{}' ({}) couldn't be loaded, entities using it keep their own textures.", SceneFilePath.filename().string(), TextureAssetName, TextureRelativePath);
		}
	}
	std::unordered_map<std::string, std::shared_ptr<const Assets::SpriteSheet>> SpriteSheetsByAssetName;
	for (const auto& [SpriteSheetAssetName, SpriteSheetRelativePath] : m_SceneAssetTable.m_SpriteSheetPathsByName)
	{
		if (std::shared_ptr<const Assets::SpriteSheet> LoadedSpriteSheet = AssetLibraryToLoadFrom.getOrLoadSpriteSheet(SpriteSheetRelativePath))
		{
			SpriteSheetsByAssetName.emplace(SpriteSheetAssetName, std::move(LoadedSpriteSheet));
		}
		else
		{
			LOGW("Scene {}: sprite sheet '{}' ({}) couldn't be loaded, sprites using it won't be drawn.", SceneFilePath.filename().string(), SpriteSheetAssetName, SpriteSheetRelativePath);
		}
	}
	std::unordered_map<std::string, std::shared_ptr<const Audio::SoundAsset>> SoundsByAssetName;
	for (const auto& [SoundAssetName, SoundRelativePath] : m_SceneAssetTable.m_SoundPathsByName)
	{
		if (std::shared_ptr<const Audio::SoundAsset> LoadedSound = AssetLibraryToLoadFrom.getOrLoadSound(SoundRelativePath))
		{
			SoundsByAssetName.emplace(SoundAssetName, std::move(LoadedSound));
		}
		else
		{
			LOGW("Scene {}: sound '{}' ({}) couldn't be loaded, sources using it stay silent.", SceneFilePath.filename().string(), SoundAssetName, SoundRelativePath);
		}
	}

	// 2. ENTITIES: every entity and its components. Parents come after, so the file can list entities in any order.
	std::unordered_map<std::string, entt::entity> EntitiesBySceneIdentifier;
	for (std::size_t CurrentEntityIndex{ 0 }; CurrentEntityIndex < LoadedSceneFile.m_Entities.size(); ++CurrentEntityIndex)
	{
		const SceneFile::SceneFileEntity& EntityDescription = LoadedSceneFile.m_Entities[CurrentEntityIndex];

		std::string SceneIdentifier = EntityDescription.m_SceneIdentifier.empty() ? "Entity " + std::to_string(CurrentEntityIndex + 1) : EntityDescription.m_SceneIdentifier;
		if (EntitiesBySceneIdentifier.contains(SceneIdentifier))
		{
			LOGW("Scene {}: two entities share the id '{}', skipping the second one.", SceneFilePath.filename().string(), SceneIdentifier);
			continue;
		}

		const entt::entity NewEntity = createEntity(EntityDescription.m_DisplayName.value_or(SceneIdentifier));
		EntitiesBySceneIdentifier.emplace(SceneIdentifier, NewEntity);

		auto& NewEntityTransform = m_Registry.get<Components::TransformComponent>(NewEntity);
		NewEntityTransform.m_LocalPosition = convertArrayToVector(EntityDescription.m_Position);
		NewEntityTransform.m_LocalRotation = glm::quat(glm::radians(convertArrayToVector(EntityDescription.m_RotationInDegrees)));
		NewEntityTransform.m_LocalScale = convertArrayToVector(EntityDescription.m_Scale);

		Components::SceneSerializationComponent& SerializationRecord = m_Registry.emplace<Components::SceneSerializationComponent>(NewEntity);
		SerializationRecord.m_SceneIdentifier = SceneIdentifier;

		// What it draws: a model from the asset table, or a primitive
		std::optional<std::uint32_t> DrawnModelIdentifier;
		if (EntityDescription.m_ModelAssetName.has_value())
		{
			SerializationRecord.m_ModelAssetName = EntityDescription.m_ModelAssetName.value();
			if (const auto FoundModel = ModelIdentifiersByAssetName.find(SerializationRecord.m_ModelAssetName); FoundModel != ModelIdentifiersByAssetName.end())
			{
				DrawnModelIdentifier = FoundModel->second;
			}
			else if (!m_SceneAssetTable.m_ModelPathsByName.contains(SerializationRecord.m_ModelAssetName))
			{
				LOGW("Scene {}: entity '{}' uses model '{}', which isn't in the asset table.", SceneFilePath.filename().string(), SceneIdentifier, SerializationRecord.m_ModelAssetName);
			}
		}
		else if (EntityDescription.m_PrimitiveShapeName.has_value())
		{
			SerializationRecord.m_PrimitiveShapeName = EntityDescription.m_PrimitiveShapeName.value();
			if (const std::optional<Assets::PrimitiveShape> PrimitiveShape = Assets::parsePrimitiveShapeName(SerializationRecord.m_PrimitiveShapeName))
			{
				DrawnModelIdentifier = AssetLibraryToLoadFrom.getOrCreatePrimitiveModel(PrimitiveShape.value());
			}
			else
			{
				LOGW("Scene {}: entity '{}' asks for an unknown primitive '{}' (quad, cube or sphere).", SceneFilePath.filename().string(), SceneIdentifier, SerializationRecord.m_PrimitiveShapeName);
			}
		}

		if (DrawnModelIdentifier.has_value())
		{
			Components::ModelRendererComponent& NewModelRenderer = m_Registry.emplace<Components::ModelRendererComponent>(NewEntity, DrawnModelIdentifier.value());
			NewModelRenderer.m_IsVisible = EntityDescription.m_IsVisible.value_or(true);
			if (EntityDescription.m_TextureAssetName.has_value())
			{
				SerializationRecord.m_TextureAssetName = EntityDescription.m_TextureAssetName.value();
				if (const auto FoundTexture = TextureIdentifiersByAssetName.find(SerializationRecord.m_TextureAssetName); FoundTexture != TextureIdentifiersByAssetName.end())
				{
					NewModelRenderer.m_TextureOverrideIdentifier = FoundTexture->second;
				}
				else if (!m_SceneAssetTable.m_TexturePathsByName.contains(SerializationRecord.m_TextureAssetName))
				{
					LOGW("Scene {}: entity '{}' uses texture '{}', which isn't in the asset table.", SceneFilePath.filename().string(), SceneIdentifier, SerializationRecord.m_TextureAssetName);
				}
			}
		}
		else if (EntityDescription.m_TextureAssetName.has_value())
		{
			SerializationRecord.m_TextureAssetName = EntityDescription.m_TextureAssetName.value();	// Kept for saving, even if there's nothing to put it on
		}

		// Optional component blocks
		if (EntityDescription.m_Camera.has_value())
		{
			const SceneFile::SceneFileCameraBlock& CameraDescription = EntityDescription.m_Camera.value();
			m_Registry.emplace<Components::CameraComponent>(NewEntity, CameraDescription.m_VerticalFieldOfViewInDegrees, CameraDescription.m_NearClippingPlaneDistance, CameraDescription.m_FarClippingPlaneDistance);
		}
		if (EntityDescription.m_FreeFlyController.has_value())
		{
			const SceneFile::SceneFileFreeFlyControllerBlock& ControllerDescription = EntityDescription.m_FreeFlyController.value();
			Components::FreeFlyMovementControllerComponent& NewController = m_Registry.emplace<Components::FreeFlyMovementControllerComponent>(NewEntity);
			NewController.m_MovementSpeedInUnitsPerSecond = ControllerDescription.m_MovementSpeedInUnitsPerSecond;
			NewController.m_SprintMovementSpeedMultiplier = ControllerDescription.m_SprintMovementSpeedMultiplier;
			NewController.m_MouseLookSensitivityInRadiansPerPixel = ControllerDescription.m_MouseLookSensitivityInRadiansPerPixel;
		}
		if (EntityDescription.m_ConstantRotation.has_value())
		{
			const SceneFile::SceneFileConstantRotationBlock& RotationDescription = EntityDescription.m_ConstantRotation.value();
			m_Registry.emplace<Components::ConstantRotationComponent>(NewEntity, convertArrayToVector(RotationDescription.m_RotationAxis), RotationDescription.m_RotationSpeedInDegreesPerSecond);
		}

		// Sprite (and its animation), drawing a sheet from the asset table
		if (EntityDescription.m_Sprite.has_value())
		{
			const SceneFile::SceneFileSpriteBlock& SpriteDescription = EntityDescription.m_Sprite.value();
			SerializationRecord.m_SpriteSheetAssetName = SpriteDescription.m_SpriteSheetAssetName;	// Kept for saving, even if it didn't load

			Components::SpriteRendererComponent NewSprite{};
			if (const auto FoundSpriteSheet = SpriteSheetsByAssetName.find(SpriteDescription.m_SpriteSheetAssetName); FoundSpriteSheet != SpriteSheetsByAssetName.end())
			{
				NewSprite.m_SpriteSheet = FoundSpriteSheet->second;
			}
			else if (!m_SceneAssetTable.m_SpriteSheetPathsByName.contains(SpriteDescription.m_SpriteSheetAssetName))
			{
				LOGW("Scene {}: entity '{}' uses sprite sheet '{}', which isn't in the asset table.", SceneFilePath.filename().string(), SceneIdentifier, SpriteDescription.m_SpriteSheetAssetName);
			}
			NewSprite.m_FrameIndex = SpriteDescription.m_FrameIndex;
			NewSprite.m_Tint = convertArrayToVector(SpriteDescription.m_Tint);
			NewSprite.m_Pivot = { SpriteDescription.m_Pivot[0], SpriteDescription.m_Pivot[1] };
			if (const std::optional<Components::SpriteBillboardMode> BillboardMode = parseSpriteBillboardModeName(SpriteDescription.m_BillboardModeName))
			{
				NewSprite.m_BillboardMode = BillboardMode.value();
			}
			else
			{
				LOGW("Scene {}: entity '{}' has an unknown billboard mode '{}' (none, faceCamera or faceCameraUpright).", SceneFilePath.filename().string(), SceneIdentifier, SpriteDescription.m_BillboardModeName);
			}
			NewSprite.m_IsFlippedHorizontally = SpriteDescription.m_IsFlippedHorizontally;
			NewSprite.m_IsFlippedVertically = SpriteDescription.m_IsFlippedVertically;
			NewSprite.m_IsLit = SpriteDescription.m_IsLit;
			NewSprite.m_IsVisible = EntityDescription.m_IsVisible.value_or(true);
			const Components::SpriteRendererComponent& EmplacedSprite = m_Registry.emplace<Components::SpriteRendererComponent>(NewEntity, std::move(NewSprite));

			if (EntityDescription.m_SpriteAnimation.has_value())
			{
				// Only animated if the clip exists: otherwise the sprite keeps the frame it was given (an animator would
				// quietly play the sheet's first clip instead)
				const SceneFile::SceneFileSpriteAnimationBlock& AnimationDescription = EntityDescription.m_SpriteAnimation.value();
				Components::SpriteAnimatorComponent NewAnimator{};
				NewAnimator.m_PlaybackSpeed = AnimationDescription.m_PlaybackSpeed;
				if (Systems::playSpriteAnimationClip(NewAnimator, EmplacedSprite, AnimationDescription.m_ClipName))
				{
					NewAnimator.m_IsPlaying = AnimationDescription.m_IsPlaying;
					m_Registry.emplace<Components::SpriteAnimatorComponent>(NewEntity, NewAnimator);
				}
				else if (EmplacedSprite.m_SpriteSheet != nullptr)
				{
					LOGW("Scene {}: entity '{}' plays clip '{}', which sprite sheet '{}' doesn't have: it isn't animated.", SceneFilePath.filename().string(), SceneIdentifier, AnimationDescription.m_ClipName, SpriteDescription.m_SpriteSheetAssetName);
				}
			}
		}
		else if (EntityDescription.m_SpriteAnimation.has_value())
		{
			LOGW("Scene {}: entity '{}' has a spriteAnimation but no sprite to play it on.", SceneFilePath.filename().string(), SceneIdentifier);
		}

		// Audio source, playing a sound from the asset table (started by the audio system once the scene runs)
		if (EntityDescription.m_Audio.has_value())
		{
			const SceneFile::SceneFileAudioBlock& AudioDescription = EntityDescription.m_Audio.value();
			SerializationRecord.m_SoundAssetName = AudioDescription.m_SoundAssetName;	// Kept for saving, even if it didn't load

			Components::AudioSourceComponent NewSource{};
			if (const auto FoundSound = SoundsByAssetName.find(AudioDescription.m_SoundAssetName); FoundSound != SoundsByAssetName.end())
			{
				NewSource.m_Sound = FoundSound->second;
			}
			else if (!m_SceneAssetTable.m_SoundPathsByName.contains(AudioDescription.m_SoundAssetName))
			{
				LOGW("Scene {}: entity '{}' plays sound '{}', which isn't in the asset table.", SceneFilePath.filename().string(), SceneIdentifier, AudioDescription.m_SoundAssetName);
			}
			NewSource.m_Volume = AudioDescription.m_Volume;
			NewSource.m_Pitch = AudioDescription.m_Pitch;
			NewSource.m_ShouldLoop = AudioDescription.m_ShouldLoop;
			NewSource.m_ShouldPlayOnStart = AudioDescription.m_ShouldPlayOnStart;
			NewSource.m_IsSpatial = AudioDescription.m_IsSpatial;
			NewSource.m_MinimumDistance = AudioDescription.m_MinimumDistance;
			NewSource.m_MaximumDistance = AudioDescription.m_MaximumDistance;
			NewSource.m_Rolloff = AudioDescription.m_Rolloff;
			if (const std::optional<Audio::DistanceFalloff> Falloff = parseDistanceFalloffName(AudioDescription.m_FalloffName))
			{
				NewSource.m_Falloff = Falloff.value();
			}
			else
			{
				LOGW("Scene {}: entity '{}' has an unknown falloff '{}' (inverse or linear), using inverse.", SceneFilePath.filename().string(), SceneIdentifier, AudioDescription.m_FalloffName);
			}
			m_Registry.emplace<Components::AudioSourceComponent>(NewEntity, std::move(NewSource));
		}
		if (EntityDescription.m_AudioListener.has_value())
		{
			m_Registry.emplace<Components::AudioListenerComponent>(NewEntity);
		}
	}
	if (m_Registry.view<Components::AudioListenerComponent>().size() > 1)
	{
		LOGW("Scene {}: more than one entity has an audioListener, only the first is listened through.", SceneFilePath.filename().string());
	}

	// 3. PARENTS, now that every entity exists
	for (const SceneFile::SceneFileEntity& EntityDescription : LoadedSceneFile.m_Entities)
	{
		if (!EntityDescription.m_ParentSceneIdentifier.has_value())
		{
			continue;
		}
		const auto FoundChild = EntitiesBySceneIdentifier.find(EntityDescription.m_SceneIdentifier);
		const auto FoundParent = EntitiesBySceneIdentifier.find(EntityDescription.m_ParentSceneIdentifier.value());
		if (FoundChild == EntitiesBySceneIdentifier.end())
		{
			continue;	// Skipped above (duplicate id), already reported
		}
		if (FoundParent == EntitiesBySceneIdentifier.end())
		{
			LOGW("Scene {}: entity '{}' has an unknown parent '{}', it stays at the top level.", SceneFilePath.filename().string(), EntityDescription.m_SceneIdentifier, EntityDescription.m_ParentSceneIdentifier.value());
			continue;
		}
		attachEntityToParent(FoundChild->second, FoundParent->second);	// Refuses (and logs) cycles
	}

	// 4. ACTIVE CAMERA: the one asked for, or else the first camera in the file
	bool HasActiveCamera = false;
	if (LoadedSceneFile.m_ActiveCameraSceneIdentifier.has_value())
	{
		if (const auto FoundCamera = EntitiesBySceneIdentifier.find(LoadedSceneFile.m_ActiveCameraSceneIdentifier.value()); FoundCamera != EntitiesBySceneIdentifier.end())
		{
			HasActiveCamera = setActiveCameraEntity(FoundCamera->second);
		}
		else
		{
			LOGW("Scene {}: activeCamera '{}' isn't an entity in the scene.", SceneFilePath.filename().string(), LoadedSceneFile.m_ActiveCameraSceneIdentifier.value());
		}
	}
	if (!HasActiveCamera)
	{
		for (const SceneFile::SceneFileEntity& EntityDescription : LoadedSceneFile.m_Entities)
		{
			const auto FoundEntity = EntitiesBySceneIdentifier.find(EntityDescription.m_SceneIdentifier);
			if (EntityDescription.m_Camera.has_value() && FoundEntity != EntitiesBySceneIdentifier.end() && setActiveCameraEntity(FoundEntity->second))
			{
				break;
			}
		}
	}

	LOG("Loaded scene {}: {} entities, {} models, {} textures, {} sprite sheets, {} sounds", SceneFilePath.filename().string(), EntitiesBySceneIdentifier.size(),
		ModelIdentifiersByAssetName.size(), TextureIdentifiersByAssetName.size(), SpriteSheetsByAssetName.size(), SoundsByAssetName.size());

	// 4. UNLOAD WHAT THE PREVIOUS SCENE LEFT BEHIND, now that everything this one needs is loaded (so assets both use were
	//    simply reused). Whatever is in this scene's asset table stays, even if no entity uses it yet: game code may spawn
	//    some later. (SpriteSheetsByAssetName and SoundsByAssetName still hold this scene's sheets and sounds, which keeps them.)
	std::vector<std::uint32_t> TextureIdentifiersStillInUse;
	TextureIdentifiersStillInUse.reserve(TextureIdentifiersByAssetName.size());
	for (const auto& [TextureAssetName, TextureIdentifier] : TextureIdentifiersByAssetName)
	{
		TextureIdentifiersStillInUse.push_back(TextureIdentifier);
	}
	AssetLibraryToLoadFrom.releaseAssetsUnusedByScene(TextureIdentifiersStillInUse);
	return true;
}

bool Scene::saveToJsonFile(const std::filesystem::path& SceneFilePath) const
{
	SceneFile::SceneFileContents SceneFileToWrite{};
	SceneFileToWrite.m_Assets = m_SceneAssetTable;
	SceneFileToWrite.m_ClearColour = m_EnvironmentOverrides.m_ClearColour;
	SceneFileToWrite.m_LightWorldPosition = m_EnvironmentOverrides.m_LightWorldPosition;
	SceneFileToWrite.m_LightColour = m_EnvironmentOverrides.m_LightColour;
	SceneFileToWrite.m_AudioVolume = m_EnvironmentOverrides.m_AudioVolume;

	// Entities in creation order (lowest entity index first), which for a loaded scene is the file's own order
	std::vector<entt::entity> EntitiesToSave;
	for (const entt::entity CurrentEntity : m_Registry.view<const Components::NameComponent>())
	{
		EntitiesToSave.push_back(CurrentEntity);
	}
	std::ranges::sort(EntitiesToSave, [](entt::entity FirstEntity, entt::entity SecondEntity) { return entt::to_entity(FirstEntity) < entt::to_entity(SecondEntity); });

	// Every entity needs a unique id: the one it was loaded with, or one made from its name for entities made in code
	std::unordered_map<entt::entity, std::string> SceneIdentifiersByEntity;
	std::unordered_set<std::string> UsedSceneIdentifiers;
	for (const entt::entity CurrentEntity : EntitiesToSave)
	{
		if (const auto* SerializationRecord = m_Registry.try_get<Components::SceneSerializationComponent>(CurrentEntity))
		{
			SceneIdentifiersByEntity.emplace(CurrentEntity, SerializationRecord->m_SceneIdentifier);
			UsedSceneIdentifiers.insert(SerializationRecord->m_SceneIdentifier);
		}
	}
	for (const entt::entity CurrentEntity : EntitiesToSave)
	{
		if (SceneIdentifiersByEntity.contains(CurrentEntity))
		{
			continue;
		}
		const std::string& EntityName = m_Registry.get<Components::NameComponent>(CurrentEntity).m_EntityName;
		std::string GeneratedSceneIdentifier = EntityName;
		for (int DuplicateNumber{ 2 }; UsedSceneIdentifiers.contains(GeneratedSceneIdentifier); ++DuplicateNumber)
		{
			GeneratedSceneIdentifier = EntityName + " " + std::to_string(DuplicateNumber);
		}
		UsedSceneIdentifiers.insert(GeneratedSceneIdentifier);
		SceneIdentifiersByEntity.emplace(CurrentEntity, GeneratedSceneIdentifier);
	}

	for (const entt::entity CurrentEntity : EntitiesToSave)
	{
		SceneFile::SceneFileEntity EntityDescription{};
		EntityDescription.m_SceneIdentifier = SceneIdentifiersByEntity.at(CurrentEntity);

		const std::string& EntityName = m_Registry.get<Components::NameComponent>(CurrentEntity).m_EntityName;
		if (EntityName != EntityDescription.m_SceneIdentifier)
		{
			EntityDescription.m_DisplayName = EntityName;
		}
		if (const auto* ParentRecord = m_Registry.try_get<Components::ParentEntityComponent>(CurrentEntity))
		{
			if (const auto FoundParentIdentifier = SceneIdentifiersByEntity.find(ParentRecord->m_ParentEntity); FoundParentIdentifier != SceneIdentifiersByEntity.end())
			{
				EntityDescription.m_ParentSceneIdentifier = FoundParentIdentifier->second;
			}
		}

		if (const auto* EntityTransform = m_Registry.try_get<Components::TransformComponent>(CurrentEntity))
		{
			EntityDescription.m_Position = convertVectorToArrayForSceneFile(EntityTransform->m_LocalPosition);
			EntityDescription.m_RotationInDegrees = convertVectorToArrayForSceneFile(glm::degrees(glm::eulerAngles(EntityTransform->m_LocalRotation)));
			EntityDescription.m_Scale = convertVectorToArrayForSceneFile(EntityTransform->m_LocalScale);
		}

		// What it draws can only be written back by NAME, so it has to have come from a scene file
		const auto* SerializationRecord = m_Registry.try_get<Components::SceneSerializationComponent>(CurrentEntity);
		const auto* ModelRenderer = m_Registry.try_get<Components::ModelRendererComponent>(CurrentEntity);
		if (SerializationRecord != nullptr)
		{
			if (!SerializationRecord->m_ModelAssetName.empty()) { EntityDescription.m_ModelAssetName = SerializationRecord->m_ModelAssetName; }
			if (!SerializationRecord->m_PrimitiveShapeName.empty()) { EntityDescription.m_PrimitiveShapeName = SerializationRecord->m_PrimitiveShapeName; }
			if (!SerializationRecord->m_TextureAssetName.empty()) { EntityDescription.m_TextureAssetName = SerializationRecord->m_TextureAssetName; }
		}
		else if (ModelRenderer != nullptr)
		{
			LOGW("Saving scene: entity '{}' draws a model that wasn't loaded from a scene file, so it's saved without it.", EntityDescription.m_SceneIdentifier);
		}
		if (ModelRenderer != nullptr && !ModelRenderer->m_IsVisible)
		{
			EntityDescription.m_IsVisible = false;	// Only written when it differs from the default
		}

		// Sprite: also written back by the sheet's NAME in the asset table
		if (const auto* Sprite = m_Registry.try_get<Components::SpriteRendererComponent>(CurrentEntity))
		{
			if (SerializationRecord != nullptr && !SerializationRecord->m_SpriteSheetAssetName.empty())
			{
				SceneFile::SceneFileSpriteBlock SpriteDescription{};
				SpriteDescription.m_SpriteSheetAssetName = SerializationRecord->m_SpriteSheetAssetName;
				SpriteDescription.m_FrameIndex = Sprite->m_FrameIndex;
				SpriteDescription.m_Tint = convertVectorToArrayForSceneFile(Sprite->m_Tint);
				SpriteDescription.m_Pivot = { roundForSceneFile(Sprite->m_Pivot.x), roundForSceneFile(Sprite->m_Pivot.y) };
				SpriteDescription.m_BillboardModeName = std::string(getSpriteBillboardModeName(Sprite->m_BillboardMode));
				SpriteDescription.m_IsFlippedHorizontally = Sprite->m_IsFlippedHorizontally;
				SpriteDescription.m_IsFlippedVertically = Sprite->m_IsFlippedVertically;
				SpriteDescription.m_IsLit = Sprite->m_IsLit;
				EntityDescription.m_Sprite = SpriteDescription;
				if (!Sprite->m_IsVisible)
				{
					EntityDescription.m_IsVisible = false;
				}

				const auto* Animator = m_Registry.try_get<Components::SpriteAnimatorComponent>(CurrentEntity);
				if (Animator != nullptr && Sprite->m_SpriteSheet != nullptr && Animator->m_ClipIndex < Sprite->m_SpriteSheet->m_Clips.size())
				{
					EntityDescription.m_SpriteAnimation = SceneFile::SceneFileSpriteAnimationBlock{ Sprite->m_SpriteSheet->m_Clips[Animator->m_ClipIndex].m_ClipName,
						roundForSceneFile(Animator->m_PlaybackSpeed), Animator->m_IsPlaying };
				}
			}
			else
			{
				LOGW("Saving scene: entity '{}' has a sprite whose sheet wasn't loaded from a scene file, so it's saved without it.", EntityDescription.m_SceneIdentifier);
			}
		}

		// Audio source: also written back by the sound's NAME in the asset table
		if (const auto* Source = m_Registry.try_get<Components::AudioSourceComponent>(CurrentEntity))
		{
			if (SerializationRecord != nullptr && !SerializationRecord->m_SoundAssetName.empty())
			{
				SceneFile::SceneFileAudioBlock AudioDescription{};
				AudioDescription.m_SoundAssetName = SerializationRecord->m_SoundAssetName;
				AudioDescription.m_Volume = roundForSceneFile(Source->m_Volume);
				AudioDescription.m_Pitch = roundForSceneFile(Source->m_Pitch);
				AudioDescription.m_ShouldLoop = Source->m_ShouldLoop;
				AudioDescription.m_ShouldPlayOnStart = Source->m_ShouldPlayOnStart;
				AudioDescription.m_IsSpatial = Source->m_IsSpatial;
				AudioDescription.m_MinimumDistance = roundForSceneFile(Source->m_MinimumDistance);
				AudioDescription.m_MaximumDistance = roundForSceneFile(Source->m_MaximumDistance);
				AudioDescription.m_Rolloff = roundForSceneFile(Source->m_Rolloff);
				AudioDescription.m_FalloffName = std::string(getDistanceFalloffName(Source->m_Falloff));
				EntityDescription.m_Audio = AudioDescription;
			}
			else
			{
				LOGW("Saving scene: entity '{}' has an audio source whose sound wasn't loaded from a scene file, so it's saved without it.", EntityDescription.m_SceneIdentifier);
			}
		}
		if (m_Registry.all_of<Components::AudioListenerComponent>(CurrentEntity))
		{
			EntityDescription.m_AudioListener = SceneFile::SceneFileAudioListenerBlock{};
		}

		if (const auto* Camera = m_Registry.try_get<Components::CameraComponent>(CurrentEntity))
		{
			EntityDescription.m_Camera = SceneFile::SceneFileCameraBlock{ Camera->m_VerticalFieldOfViewInDegrees, Camera->m_NearClippingPlaneDistance, Camera->m_FarClippingPlaneDistance };
		}
		if (const auto* Controller = m_Registry.try_get<Components::FreeFlyMovementControllerComponent>(CurrentEntity))
		{
			EntityDescription.m_FreeFlyController = SceneFile::SceneFileFreeFlyControllerBlock{ Controller->m_MovementSpeedInUnitsPerSecond, Controller->m_SprintMovementSpeedMultiplier, Controller->m_MouseLookSensitivityInRadiansPerPixel };
		}
		if (const auto* ConstantRotation = m_Registry.try_get<Components::ConstantRotationComponent>(CurrentEntity))
		{
			EntityDescription.m_ConstantRotation = SceneFile::SceneFileConstantRotationBlock{ convertVectorToArrayForSceneFile(ConstantRotation->m_RotationAxis), ConstantRotation->m_RotationSpeedInDegreesPerSecond };
		}

		SceneFileToWrite.m_Entities.push_back(std::move(EntityDescription));
	}

	if (const auto FoundActiveCameraIdentifier = SceneIdentifiersByEntity.find(m_ActiveCameraEntity); FoundActiveCameraIdentifier != SceneIdentifiersByEntity.end())
	{
		SceneFileToWrite.m_ActiveCameraSceneIdentifier = FoundActiveCameraIdentifier->second;
	}

	std::error_code DirectoryCreationError;
	std::filesystem::create_directories(SceneFilePath.parent_path(), DirectoryCreationError);
	std::string SceneFileBuffer;
	const glz::error_ctx SceneFileWriteError = glz::write_file_json<glz::opts{ .prettify = true }>(SceneFileToWrite, SceneFilePath.string(), SceneFileBuffer);
	if (SceneFileWriteError)
	{
		LOGE("Couldn't save scene {}: {}", SceneFilePath.string(), glz::format_error(SceneFileWriteError, SceneFileBuffer));
		return false;
	}
	LOG("Saved scene to {}", SceneFilePath.string());
	return true;
}

bool Scene::saveOverLoadedSceneFile() const
{
	if (m_LoadedSceneFilePath.empty())
	{
		LOGW("There's no loaded scene file to save over.");
		return false;
	}

	bool WasEverySaveSuccessful = saveToJsonFile(m_LoadedSceneFilePath);

#ifdef HEPHAESTIAN_SOURCE_ASSETS_DIRECTORY
	// Debug builds know where the repository's assets folder is (CMake passes it in): save there too, since that's the copy
	// the build deploys next to the exe. Otherwise the next build would put the old version back.
	const std::filesystem::path PathInsideRuntimeAssets = m_LoadedSceneFilePath.lexically_relative(Vulkanised::Utilities::File::getRuntimeAssetsDirectory());
	const bool IsSceneInsideRuntimeAssets = !PathInsideRuntimeAssets.empty() && *PathInsideRuntimeAssets.begin() != "..";
	const std::filesystem::path SourceAssetsDirectory{ HEPHAESTIAN_SOURCE_ASSETS_DIRECTORY };
	std::error_code DirectoryCheckError;
	if (IsSceneInsideRuntimeAssets && std::filesystem::is_directory(SourceAssetsDirectory, DirectoryCheckError))
	{
		WasEverySaveSuccessful = saveToJsonFile(SourceAssetsDirectory / PathInsideRuntimeAssets) && WasEverySaveSuccessful;
	}
	else
	{
		LOGW("Couldn't find the repository's copy of {} ({}), only the copy next to the exe was saved.", m_LoadedSceneFilePath.filename().string(), SourceAssetsDirectory.string());
	}
#endif

	return WasEverySaveSuccessful;
}

void Scene::applyEnvironmentToRenderer(Vulkanised::VulkanRenderer& Renderer, const HephaestianSettings& EngineSettings) const
{
	// The scene's value when it sets one, otherwise the engine's own setting
	Renderer.setClearColour(convertArrayToVector(m_EnvironmentOverrides.m_ClearColour.value_or(EngineSettings.m_BackgroundClearColour)));
	Renderer.setLightPosition(convertArrayToVector(m_EnvironmentOverrides.m_LightWorldPosition.value_or(EngineSettings.m_LightWorldPosition)));
	Renderer.setLightColour(convertArrayToVector(m_EnvironmentOverrides.m_LightColour.value_or(EngineSettings.m_LightColour)));
}

}
