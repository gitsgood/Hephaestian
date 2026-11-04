#ifndef HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H
#define HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H

#include "Hephaestian/Settings/HephaestianSettingsManager.h"

namespace Hephaestian::UI
{

/*
* A small ImGui window for HephaestianSettings. It never touches the renderer itself: every edit goes through the settings
* manager, so subscribers react, the JSON gets saved, and external edits to the JSON show up here too.
*/
class HephaestianSettingsWidget
{
public:
	explicit HephaestianSettingsWidget(HephaestianSettingsManager& SettingsManagerToEdit) noexcept : m_SettingsManager(SettingsManagerToEdit) {}

	// Call between VulkanRenderer::beginImGuiFrame and SubmitFrame
	void drawSettingsWindow();

private:
	HephaestianSettingsManager& m_SettingsManager;

	// Continuous edits (dragging a colour) are applied live but only saved once the mouse is released
	bool m_HasUnsavedContinuousEdit{ false };
};

}

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGSWIDGET_H
