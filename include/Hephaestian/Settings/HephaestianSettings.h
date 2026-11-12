#ifndef HEPHAESTIAN_HEPHAESTIANSETTINGS_H
#define HEPHAESTIAN_HEPHAESTIANSETTINGS_H

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "glaze/glaze.hpp"

namespace Hephaestian
{

// Engine-wide settings, read from HephaestianSettings.json next to the executable.
// Add a member here + a line in the glz::meta below, and it's in the file.
struct HephaestianSettings
{
	bool m_ShowInfiniteGrid{ true };
	std::array<float, 3> m_BackgroundClearColour{ 0.6f, 0.65f, 0.4f };	// RGB, 0 to 1. A plain array so the JSON stays a readable [r, g, b]

	// The scene's one point light. The further away it is, the more it behaves like the sun (parallel light over the whole scene).
	std::array<float, 3> m_LightWorldPosition{ 20.0f, 40.0f, 15.0f };
	std::array<float, 3> m_LightColour{ 1.0f, 1.0f, 1.0f };			// RGB, 0 to 1

	// The scene opened at startup, relative to the runtime assets folder ("--scene <path>" on the command line wins)
	std::string m_StartupSceneRelativePath{ "Scenes/Main.scene.json" };

	// Lets the settings manager tell whether anything actually changed (compares every member, so new ones are covered automatically)
	[[nodiscard]] bool operator==(const HephaestianSettings&) const = default;
};

// <executable folder>/HephaestianSettings.json (the build copies the repo's copy there, like VulkanisedLoggerSettings.json)
[[nodiscard]] std::filesystem::path getHephaestianSettingsFilePath();

// Reads the settings file. If it's missing it gets created with the defaults; if it's broken the defaults are used (and logged).
// Unknown keys are ignored and missing keys keep their defaults, so old files keep working as settings get added.
[[nodiscard]] HephaestianSettings loadHephaestianSettings();

// Just reads the file: std::nullopt if it's missing or doesn't parse (logged), no defaults and no file creation.
// This is what hot reloading uses, so a half-saved file can't reset everything.
[[nodiscard]] std::optional<HephaestianSettings> tryReadHephaestianSettingsFile();

// Writes the settings out (pretty printed)
bool saveHephaestianSettings(const HephaestianSettings& SettingsToSave);

}

// JSON key names. Keeps the file readable ("showInfiniteGrid") while the struct follows the m_ naming convention.
template <>
struct glz::meta<Hephaestian::HephaestianSettings>
{
	using T = Hephaestian::HephaestianSettings;
	static constexpr auto value = glz::object(
		"showInfiniteGrid", &T::m_ShowInfiniteGrid,
		"backgroundClearColour", &T::m_BackgroundClearColour,
		"lightPosition", &T::m_LightWorldPosition,
		"lightColour", &T::m_LightColour,
		"startupScene", &T::m_StartupSceneRelativePath
	);
};

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGS_H
