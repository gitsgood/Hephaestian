#include "Hephaestian/UI/HephaestianSettingsWidget.h"

#include <imgui.h>

namespace Hephaestian::UI
{

void HephaestianSettingsWidget::drawSettingsWindow()
{
	// Only the first time ever: after that, ImGui remembers where you left it (imgui.ini, next to the exe)
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_FirstUseEver);	// Wide enough for the "Background colour" label

	if (ImGui::Begin("Hephaestian Settings"))
	{
		// Read fresh every frame, so a change from anywhere else (the JSON on disk, a key press...) shows up here immediately
		const HephaestianSettings& CurrentSettings = m_SettingsManager.getCurrentSettings();

		// - Infinite grid: a click is a one-off edit, so it's saved straight away
		bool ShowInfiniteGrid = CurrentSettings.m_ShowInfiniteGrid;
		if (ImGui::Checkbox("Infinite grid", &ShowInfiniteGrid))
		{
			m_SettingsManager.modifySettings([ShowInfiniteGrid](HephaestianSettings& Settings) { Settings.m_ShowInfiniteGrid = ShowInfiniteGrid; });
		}

		// - Background colour: dragging changes it every frame, so apply live but hold off on writing the file
		std::array<float, 3> BackgroundClearColour = CurrentSettings.m_BackgroundClearColour;
		if (ImGui::ColorEdit3("Background colour", BackgroundClearColour.data()))
		{
			m_SettingsManager.setAutomaticallySaveRuntimeChanges(false);
			m_SettingsManager.modifySettings([&BackgroundClearColour](HephaestianSettings& Settings) { Settings.m_BackgroundClearColour = BackgroundClearColour; });
			m_SettingsManager.setAutomaticallySaveRuntimeChanges(true);
			m_HasUnsavedContinuousEdit = true;
		}
	}
	ImGui::End();

	// Nothing is being dragged/typed into any more (this also covers the colour picker popup): now it's worth saving
	if (m_HasUnsavedContinuousEdit && !ImGui::IsAnyItemActive())
	{
		m_SettingsManager.saveSettingsToFile();
		m_HasUnsavedContinuousEdit = false;
	}
}

}
