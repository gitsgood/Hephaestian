#include "Hephaestian/UI/HephaestianSettingsWidget.h"

#include <array>
#include <functional>

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

		// Dragging a colour or a position changes it every frame: apply live, but hold off on writing the file until released
		auto applyContinuousEdit = [this](const std::function<void(HephaestianSettings&)>& EditToApply)
		{
			m_SettingsManager.setAutomaticallySaveRuntimeChanges(false);
			m_SettingsManager.modifySettings(EditToApply);
			m_SettingsManager.setAutomaticallySaveRuntimeChanges(true);
			m_HasUnsavedContinuousEdit = true;
		};

		// - Background colour
		std::array<float, 3> BackgroundClearColour = CurrentSettings.m_BackgroundClearColour;
		if (ImGui::ColorEdit3("Background colour", BackgroundClearColour.data()))
		{
			applyContinuousEdit([&BackgroundClearColour](HephaestianSettings& Settings) { Settings.m_BackgroundClearColour = BackgroundClearColour; });
		}

		// - Light: world position (drag a number, or double-click it to type one) and colour
		ImGui::SeparatorText("Light");
		std::array<float, 3> LightWorldPosition = CurrentSettings.m_LightWorldPosition;
		if (ImGui::DragFloat3("Light position", LightWorldPosition.data(), 1.0f, -5000.0f, 5000.0f, "%.1f"))
		{
			applyContinuousEdit([&LightWorldPosition](HephaestianSettings& Settings) { Settings.m_LightWorldPosition = LightWorldPosition; });
		}
		std::array<float, 3> LightColour = CurrentSettings.m_LightColour;
		if (ImGui::ColorEdit3("Light colour", LightColour.data()))
		{
			applyContinuousEdit([&LightColour](HephaestianSettings& Settings) { Settings.m_LightColour = LightColour; });
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
