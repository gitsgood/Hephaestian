/**
 * @file HephaestianSettingsWidget.h
 * @brief The ImGui settings window: engine settings, scenes, audio and renderer statistics.
 */
#ifndef HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H
#define HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H

#include <filesystem>
#include <vector>

#include "Hephaestian/Assets/AssetLibrary.h"
#include "Hephaestian/ECS/Scene.h"
#include "Hephaestian/Settings/HephaestianSettingsManager.h"

/**
 * @brief The engine's ImGui windows.
 */
namespace Hephaestian::UI
{

/**
 * @brief The ImGui settings window.
 *
 * - Engine settings (grid, vertical sync, background colour, light), edited through the settings manager so the JSON
 *   stays in sync. If the loaded scene overrides one of them, its control is redirected to the SCENE instead (and says
 *   so), and the change is only kept on disk when "Save scene" is clicked.
 * - One button per scene file found in assets/Scenes at startup, to load it, and "Save scene" for the loaded one.
 * - Audio: volume (redirected to the scene when it overrides it, like the environment), mute, muting in the
 *   background, HRTF, and what the audio is doing.
 * - Renderer statistics (frame times, draws, culling, GPU memory, textures), collapsed by default.
 */
class HephaestianSettingsWidget
{
public:
	/**
	 * @brief Finds the scene files once, and is ready to draw.
	 * @param SettingsManagerToEdit The engine settings the controls edit.
	 * @param SceneToManage The scene whose overrides are edited, and which the scene buttons load into.
	 * @param AssetLibraryForSceneLoading Where loaded scenes get their assets.
	 * @param RendererToInspect Where the statistics come from.
	 */
	HephaestianSettingsWidget(HephaestianSettingsManager& SettingsManagerToEdit, Scene& SceneToManage, Assets::AssetLibrary& AssetLibraryForSceneLoading,
		Vulkanised::VulkanRenderer& RendererToInspect);

	/** @brief Draws the window. Call between VulkanRenderer::beginImGuiFrame and SubmitFrame. */
	void drawSettingsWindow();

private:
	/** @brief The scene buttons (one per file, and "Save scene"). */
	void drawSceneSection();
	/** @brief Grid, vertical sync, background colour and light (redirected to the scene where it overrides them). */
	void drawEnvironmentSection();
	/** @brief Volume, mute, focus muting, HRTF, and the audio device's figures. */
	void drawAudioSection();
	/** @brief Frame times, draws, culling, GPU memory and textures (collapsed by default). */
	void drawRendererStatisticsSection();

	HephaestianSettingsManager& m_SettingsManager;	///< The engine settings
	Scene& m_Scene;									///< The scene being edited and loaded into
	Assets::AssetLibrary& m_AssetLibrary;			///< For loading scenes
	Vulkanised::VulkanRenderer& m_Renderer;			///< For the statistics

	std::vector<std::filesystem::path> m_SceneFilePaths;	///< Found once, at startup

	bool m_HasUnsavedContinuousEdit{ false };	///< Continuous edits (dragging a colour) are applied live but only saved once the mouse is released
};

}

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H
