/**
 * @file HephaestianSettings.h
 * @brief The engine-wide settings (HephaestianSettings.json next to the executable): what they are, and reading and
 * writing the file.
 *
 * To add a setting: a member in HephaestianSettings, plus a line in its glz::meta at the bottom of this file, and it's
 * in the file. HephaestianSettingsManager is what the rest of the engine goes through at runtime.
 */
#ifndef HEPHAESTIAN_HEPHAESTIANSETTINGS_H
#define HEPHAESTIAN_HEPHAESTIANSETTINGS_H

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "glaze/glaze.hpp"

namespace Hephaestian
{

/**
 * @brief Engine-wide settings, read from HephaestianSettings.json next to the executable.
 *
 * A scene file can override the environment (clear colour, light) and the audio volume while it's loaded: see
 * SceneEnvironmentOverrides. Everything else here is the player's alone.
 */
struct HephaestianSettings
{
	bool m_ShowInfiniteGrid{ true };		///< Draws the editor's infinite ground grid
	bool m_IsVerticalSyncEnabled{ false };	///< One frame per display refresh (less power and heat) instead of as many as possible
	std::array<float, 3> m_BackgroundClearColour{ 0.6f, 0.65f, 0.4f };	///< RGB, 0 to 1. A plain array so the JSON stays a readable [r, g, b]

	// - The scene's one point light
	std::array<float, 3> m_LightWorldPosition{ 20.0f, 40.0f, 15.0f };	///< Where the light is. The further away, the more it behaves like the sun (parallel light over the whole scene).
	std::array<float, 3> m_LightColour{ 1.0f, 1.0f, 1.0f };			///< RGB, 0 to 1

	// - Audio: mute, focus muting and HRTF are the player's alone
	float m_AudioVolume{ 1.0f };					///< 0 to 1, everything (a scene's "audioVolume" overrides it while loaded)
	bool m_IsAudioMuted{ false };					///< Silences everything
	bool m_ShouldMuteAudioWhenUnfocused{ true };	///< Silent while the window is in the background or minimised (music keeps going)
	/// 3D sound for headphones: "auto" (on when Windows says headphones), "on" or "off" (see Audio::parseHrtfModeName).
	/// Text, so a typo can't break the whole file.
	std::string m_AudioHrtfMode{ "auto" };

	/// The scene opened at startup, relative to the runtime assets folder ("--scene <path>" on the command line wins)
	std::string m_StartupSceneRelativePath{ "Scenes/Main.scene.json" };

	/**
	 * @brief Compares every member, so the settings manager can tell whether anything actually changed (new members
	 * are covered automatically).
	 * @return True if every setting is the same.
	 */
	[[nodiscard]] bool operator==(const HephaestianSettings&) const = default;
};

/**
 * @brief Where the settings file lives.
 * @return `<executable folder>/HephaestianSettings.json` (the build copies the repo's copy there, like
 * VulkanisedLoggerSettings.json).
 */
[[nodiscard]] std::filesystem::path getHephaestianSettingsFilePath();

/**
 * @brief Reads the settings file, for startup.
 *
 * If it's missing it gets created with the defaults; if it's broken the defaults are used (and logged).
 * Unknown keys are ignored and missing keys keep their defaults, so old files keep working as settings get added.
 * @return The settings from the file, or the defaults.
 */
[[nodiscard]] HephaestianSettings loadHephaestianSettings();

/**
 * @brief Just reads the file: no defaults and no file creation.
 *
 * This is what hot reloading uses, so a half-saved file can't reset everything.
 * @return The settings, or std::nullopt if the file is missing or doesn't parse (logged).
 */
[[nodiscard]] std::optional<HephaestianSettings> tryReadHephaestianSettingsFile();

/**
 * @brief Writes the settings out, pretty printed.
 * @param SettingsToSave The settings to write to getHephaestianSettingsFilePath().
 * @return False if the file couldn't be written (logged).
 */
bool saveHephaestianSettings(const HephaestianSettings& SettingsToSave);

}

/// @cond GLAZE_META
// JSON key names. Keeps the file readable ("showInfiniteGrid") while the struct follows the m_ naming convention.
template <>
struct glz::meta<Hephaestian::HephaestianSettings>
{
	using T = Hephaestian::HephaestianSettings;
	static constexpr auto value = glz::object(
		"showInfiniteGrid", &T::m_ShowInfiniteGrid,
		"verticalSync", &T::m_IsVerticalSyncEnabled,
		"backgroundClearColour", &T::m_BackgroundClearColour,
		"lightPosition", &T::m_LightWorldPosition,
		"lightColour", &T::m_LightColour,
		"audioVolume", &T::m_AudioVolume,
		"audioMuted", &T::m_IsAudioMuted,
		"muteAudioWhenUnfocused", &T::m_ShouldMuteAudioWhenUnfocused,
		"audioHrtf", &T::m_AudioHrtfMode,
		"startupScene", &T::m_StartupSceneRelativePath
	);
};
/// @endcond

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGS_H
