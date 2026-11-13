#include "Hephaestian/UI/HephaestianSettingsWidget.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>

#include <imgui.h>

#include <Vulkanised/utils/FileUtils.h>

namespace Hephaestian::UI
{

namespace
{
	constexpr const char* c_SceneOverrideLabelSuffix{ " (scene is overriding this, changes will apply to the scene)." };
	constexpr float c_ControlWidthInPixels{ 260.0f };	// Colour pickers and position drags

	// "Main.scene.json" -> "Main": what the load button says
	[[nodiscard]] std::string makeSceneButtonLabel(const std::filesystem::path& SceneFilePath)
	{
		std::string SceneName = SceneFilePath.filename().string();
		for (const std::string_view ExtensionToStrip : { ".json", ".scene" })
		{
			if (SceneName.size() > ExtensionToStrip.size() && SceneName.ends_with(ExtensionToStrip))
			{
				SceneName.resize(SceneName.size() - ExtensionToStrip.size());
			}
		}
		return SceneName;
	}

	enum class EnvironmentControlKind { ColourPicker, PositionDrag };
}

HephaestianSettingsWidget::HephaestianSettingsWidget(HephaestianSettingsManager& SettingsManagerToEdit, Scene& SceneToManage, Assets::AssetLibrary& AssetLibraryForSceneLoading,
	Vulkanised::VulkanRenderer& RendererToInspect)
	: m_SettingsManager(SettingsManagerToEdit)
	, m_Scene(SceneToManage)
	, m_AssetLibrary(AssetLibraryForSceneLoading)
	, m_Renderer(RendererToInspect)
{
	// Every .json in assets/Scenes gets a load button. Looked up once, at startup.
	const std::filesystem::path ScenesDirectory = Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / "Scenes";
	std::error_code DirectoryIterationError;
	for (const std::filesystem::directory_entry& CurrentDirectoryEntry : std::filesystem::directory_iterator(ScenesDirectory, DirectoryIterationError))
	{
		if (CurrentDirectoryEntry.is_regular_file() && CurrentDirectoryEntry.path().extension() == ".json")
		{
			m_SceneFilePaths.push_back(CurrentDirectoryEntry.path());
		}
	}
	std::ranges::sort(m_SceneFilePaths);
}

void HephaestianSettingsWidget::drawSettingsWindow()
{
	// Only the first time ever: after that, ImGui remembers where you left it (imgui.ini, next to the exe)
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	// The window fits its contents, so a long label (like the scene override note) is never cut off
	if (ImGui::Begin("Hephaestian Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::PushItemWidth(c_ControlWidthInPixels);	// Fixed: auto-resizing and window-relative widths would feed each other
		drawSceneSection();
		drawEnvironmentSection();
		drawRendererStatisticsSection();
		ImGui::PopItemWidth();
	}
	ImGui::End();

	// Nothing is being dragged/typed into any more (this also covers the colour picker popup): now it's worth saving
	if (m_HasUnsavedContinuousEdit && !ImGui::IsAnyItemActive())
	{
		m_SettingsManager.saveSettingsToFile();
		m_HasUnsavedContinuousEdit = false;
	}
}

void HephaestianSettingsWidget::drawSceneSection()
{
	ImGui::SeparatorText("Scenes");
	if (m_SceneFilePaths.empty())
	{
		ImGui::TextUnformatted("No scene files in assets/Scenes.");
	}

	// One button per scene file. Clicking the loaded one reloads it from disk (throwing away unsaved changes).
	const std::filesystem::path LoadedSceneFilePath = m_Scene.getLoadedSceneFilePath();	// A copy: loading below replaces it
	for (const std::filesystem::path& CurrentSceneFilePath : m_SceneFilePaths)
	{
		std::error_code EquivalenceCheckError;
		const bool IsCurrentSceneLoaded = !LoadedSceneFilePath.empty() && std::filesystem::equivalent(CurrentSceneFilePath, LoadedSceneFilePath, EquivalenceCheckError);
		const std::string ButtonLabel = makeSceneButtonLabel(CurrentSceneFilePath) + (IsCurrentSceneLoaded ? " (loaded)" : "") + "##" + CurrentSceneFilePath.string();
		if (ImGui::Button(ButtonLabel.c_str()))
		{
			m_Scene.loadFromJsonFile(CurrentSceneFilePath, m_AssetLibrary);
		}
		ImGui::SameLine();
	}
	ImGui::NewLine();

	// Saving is always a deliberate click: nothing writes to a scene file on its own
	ImGui::BeginDisabled(LoadedSceneFilePath.empty());
	if (ImGui::Button("Save scene"))
	{
		m_Scene.saveOverLoadedSceneFile();
	}
	ImGui::EndDisabled();
	if (!LoadedSceneFilePath.empty())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", LoadedSceneFilePath.filename().string().c_str());
	}
}

void HephaestianSettingsWidget::drawEnvironmentSection()
{
	ImGui::SeparatorText("Rendering");

	// Read fresh every frame, so a change from anywhere else (the JSON on disk, a scene load...) shows up here immediately
	const HephaestianSettings& CurrentSettings = m_SettingsManager.getCurrentSettings();
	SceneEnvironmentOverrides& SceneOverrides = m_Scene.getEnvironmentOverrides();

	// - Infinite grid: a click is a one-off edit, so it's saved straight away
	bool ShowInfiniteGrid = CurrentSettings.m_ShowInfiniteGrid;
	if (ImGui::Checkbox("Infinite grid", &ShowInfiniteGrid))
	{
		m_SettingsManager.modifySettings([ShowInfiniteGrid](HephaestianSettings& Settings) { Settings.m_ShowInfiniteGrid = ShowInfiniteGrid; });
	}
	ImGui::SameLine();
	bool IsVerticalSyncEnabled = CurrentSettings.m_IsVerticalSyncEnabled;
	if (ImGui::Checkbox("Vertical sync", &IsVerticalSyncEnabled))
	{
		m_SettingsManager.modifySettings([IsVerticalSyncEnabled](HephaestianSettings& Settings) { Settings.m_IsVerticalSyncEnabled = IsVerticalSyncEnabled; });
	}

	// Dragging a colour or a position changes it every frame: apply live, but hold off on writing the file until released
	auto applyContinuousSettingsEdit = [this](const std::function<void(HephaestianSettings&)>& EditToApply)
	{
		m_SettingsManager.setAutomaticallySaveRuntimeChanges(false);
		m_SettingsManager.modifySettings(EditToApply);
		m_SettingsManager.setAutomaticallySaveRuntimeChanges(true);
		m_HasUnsavedContinuousEdit = true;
	};

	// A value the scene overrides is edited IN THE SCENE (kept on disk only by "Save scene"); otherwise in the engine settings
	auto drawEnvironmentControl = [&](const char* BaseLabel, EnvironmentControlKind ControlKind, std::optional<std::array<float, 3>>& SceneOverrideValue,
		const std::array<float, 3>& EngineSettingValue, std::array<float, 3> HephaestianSettings::* EngineSettingMember)
	{
		const bool IsSceneOverriding = SceneOverrideValue.has_value();
		const std::string ControlLabel = std::string(BaseLabel) + (IsSceneOverriding ? c_SceneOverrideLabelSuffix : "");
		std::array<float, 3> EditedValue = IsSceneOverriding ? SceneOverrideValue.value() : EngineSettingValue;

		const bool WasValueChanged = ControlKind == EnvironmentControlKind::ColourPicker
			? ImGui::ColorEdit3(ControlLabel.c_str(), EditedValue.data())
			: ImGui::DragFloat3(ControlLabel.c_str(), EditedValue.data(), 1.0f, -5000.0f, 5000.0f, "%.1f");
		if (!WasValueChanged)
		{
			return;
		}

		if (IsSceneOverriding)
		{
			SceneOverrideValue = EditedValue;
		}
		else
		{
			applyContinuousSettingsEdit([&EditedValue, EngineSettingMember](HephaestianSettings& Settings) { Settings.*EngineSettingMember = EditedValue; });
		}
	};

	drawEnvironmentControl("Background colour", EnvironmentControlKind::ColourPicker, SceneOverrides.m_ClearColour, CurrentSettings.m_BackgroundClearColour, &HephaestianSettings::m_BackgroundClearColour);

	// Light: world position (drag a number, or double-click it to type one) and colour
	ImGui::SeparatorText("Light");
	drawEnvironmentControl("Light position", EnvironmentControlKind::PositionDrag, SceneOverrides.m_LightWorldPosition, CurrentSettings.m_LightWorldPosition, &HephaestianSettings::m_LightWorldPosition);
	drawEnvironmentControl("Light colour", EnvironmentControlKind::ColourPicker, SceneOverrides.m_LightColour, CurrentSettings.m_LightColour, &HephaestianSettings::m_LightColour);
}

void HephaestianSettingsWidget::drawRendererStatisticsSection()
{
	if (!ImGui::CollapsingHeader("Renderer statistics"))
	{
		return;
	}

	const Vulkanised::Rendering::FrameStatistics& Statistics = m_Renderer.getFrameStatistics();
	const std::string GpuName{ m_Renderer.getGpuName() };
	ImGui::TextDisabled("%s", GpuName.c_str());

	const float FramesPerSecond = Statistics.m_FrameIntervalInMilliseconds > 0.0f ? 1000.0f / Statistics.m_FrameIntervalInMilliseconds : 0.0f;
	ImGui::Text("Frame: %.2f ms (%.0f FPS)", Statistics.m_FrameIntervalInMilliseconds, FramesPerSecond);
	ImGui::Text("CPU submit: %.3f ms   CPU waiting on GPU: %.3f ms", Statistics.m_CpuSubmitTimeInMilliseconds, Statistics.m_CpuWaitForGpuTimeInMilliseconds);
	if (Statistics.m_IsGpuTimingAvailable)
	{
		ImGui::Text("GPU frame: %.3f ms", Statistics.m_GpuFrameTimeInMilliseconds);
	}
	else
	{
		ImGui::TextDisabled("GPU frame: not measurable on this GPU");
	}

	ImGui::Text("Instances: %u submitted, %u culled, %u drawn", Statistics.m_SubmittedInstanceCount, Statistics.m_CulledInstanceCount, Statistics.m_DrawnInstanceCount);
	ImGui::Text("Indirect draws: %u (one per visible mesh, all in one call)", Statistics.m_DrawCallCount);

	// Debug toggles, not saved: culling on/off, and freezing it (fly out and see what the frozen camera keeps)
	bool IsFrustumCullingEnabled = m_Renderer.isFrustumCullingEnabled();
	if (ImGui::Checkbox("Frustum culling", &IsFrustumCullingEnabled))
	{
		m_Renderer.setFrustumCullingEnabled(IsFrustumCullingEnabled);
	}
	ImGui::SameLine();
	bool IsFrustumCullingFrozen = m_Renderer.isFrustumCullingFrozen();
	if (ImGui::Checkbox("Freeze culling", &IsFrustumCullingFrozen))
	{
		m_Renderer.setFrustumCullingFrozen(IsFrustumCullingFrozen);
	}

	constexpr double c_BytesPerMegabyte{ 1024.0 * 1024.0 };
	const Vulkanised::Rendering::GpuMemoryStatistics MemoryStatistics = m_Renderer.getGpuMemoryStatistics();
	ImGui::Text("GPU memory: %.1f MB used, %.1f MB reserved, %.0f MB budget", MemoryStatistics.m_UsedBytes / c_BytesPerMegabyte,
		MemoryStatistics.m_ReservedBytes / c_BytesPerMegabyte, MemoryStatistics.m_DeviceLocalBudgetBytes / c_BytesPerMegabyte);
	ImGui::Text("Allocations: %u resources in %u memory blocks", MemoryStatistics.m_AllocationCount, MemoryStatistics.m_DeviceMemoryBlockCount);
	ImGui::Text("Textures: %u loaded", m_Renderer.getLiveTextureCount());
}

}
