#include "Hephaestian/Settings/HephaestianSettings.h"

#include <string>

#include <Vulkanised/Logger.h>
#include <Vulkanised/utils/FileUtils.h>

namespace Hephaestian
{

namespace
{
	constexpr const char* c_HephaestianSettingsFileName{ "HephaestianSettings.json" };
}

std::filesystem::path getHephaestianSettingsFilePath()
{
	return Vulkanised::Utilities::File::getExecutableDirectory() / c_HephaestianSettingsFileName;
}

HephaestianSettings loadHephaestianSettings()
{
	const std::filesystem::path SettingsFilePath = getHephaestianSettingsFilePath();

	std::error_code FileExistenceCheckError;
	if (!std::filesystem::is_regular_file(SettingsFilePath, FileExistenceCheckError))
	{
		LOGW("{} not found, creating it with the default settings.", SettingsFilePath.string());
		const HephaestianSettings DefaultSettings{};
		saveHephaestianSettings(DefaultSettings);
		return DefaultSettings;
	}

	if (const std::optional<HephaestianSettings> LoadedSettings = tryReadHephaestianSettingsFile())
	{
		LOG("Loaded {}", SettingsFilePath.string());
		return LoadedSettings.value();
	}

	LOGW("Using the default settings instead.");
	return HephaestianSettings{};
}

std::optional<HephaestianSettings> tryReadHephaestianSettingsFile()
{
	const std::filesystem::path SettingsFilePath = getHephaestianSettingsFilePath();
	HephaestianSettings ReadSettings{};
	std::string SettingsFileBuffer;

	const glz::error_ctx SettingsReadError = glz::read_file_json<glz::opts{ .error_on_unknown_keys = false }>(ReadSettings, SettingsFilePath.string(), SettingsFileBuffer);
	if (SettingsReadError)
	{
		// A half-read struct could be a mix of file and default values, so it's all or nothing
		LOGE("Couldn't read {}:\n{}", SettingsFilePath.string(), glz::format_error(SettingsReadError, SettingsFileBuffer));
		return std::nullopt;
	}
	return ReadSettings;
}

bool saveHephaestianSettings(const HephaestianSettings& SettingsToSave)
{
	const std::filesystem::path SettingsFilePath = getHephaestianSettingsFilePath();
	std::string SettingsFileBuffer;
	const glz::error_ctx SettingsWriteError = glz::write_file_json<glz::opts{ .prettify = true }>(SettingsToSave, SettingsFilePath.string(), SettingsFileBuffer);
	if (SettingsWriteError)
	{
		LOGE("Couldn't write {}: {}", SettingsFilePath.string(), glz::format_error(SettingsWriteError, SettingsFileBuffer));
		return false;
	}
	return true;
}

}
