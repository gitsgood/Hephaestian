#ifndef HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H
#define HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H

#include <filesystem>
#include <vector>

#include "Hephaestian/Assets/AssetLibrary.h"
#include "Hephaestian/ECS/Scene.h"
#include "Hephaestian/Settings/HephaestianSettingsManager.h"

namespace Hephaestian::UI
{

/*
* The ImGui settings window:
*	- engine settings (grid, background colour, light), edited through the settings manager so the JSON stays in sync.
*	  If the loaded scene overrides one of them, its control is redirected to the SCENE instead (and says so), and the
*	  change is only kept on disk when "Save scene" is clicked.
*	- one button per scene file found in assets/Scenes at startup, to load it, and "Save scene" for the loaded one
*	- audio: volume (redirected to the scene when it overrides it, like the environment), mute, muting in the background
*	- renderer statistics (frame times, draws, culling, GPU memory), collapsed by default
*/
class HephaestianSettingsWidget
{
public:
	HephaestianSettingsWidget(HephaestianSettingsManager& SettingsManagerToEdit, Scene& SceneToManage, Assets::AssetLibrary& AssetLibraryForSceneLoading,
		Vulkanised::VulkanRenderer& RendererToInspect);

	// Call between VulkanRenderer::beginImGuiFrame and SubmitFrame
	void drawSettingsWindow();

private:
	void drawSceneSection();
	void drawEnvironmentSection();
	void drawAudioSection();
	void drawRendererStatisticsSection();

	HephaestianSettingsManager& m_SettingsManager;
	Scene& m_Scene;
	Assets::AssetLibrary& m_AssetLibrary;
	Vulkanised::VulkanRenderer& m_Renderer;

	// Found once, at startup
	std::vector<std::filesystem::path> m_SceneFilePaths;

	// Continuous edits (dragging a colour) are applied live but only saved once the mouse is released
	bool m_HasUnsavedContinuousEdit{ false };
};

}

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H
