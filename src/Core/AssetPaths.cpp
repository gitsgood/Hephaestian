/**
 * @file AssetPaths.cpp
 * @brief Picks the repository's assets folder in Debug builds (HEPHAESTIAN_SOURCE_ASSETS_DIRECTORY, from CMake).
 */
#include "Hephaestian/Core/AssetPaths.h"

#include <system_error>

#include <Vulkanised/Logger.h>
#include <Vulkanised/utils/FileUtils.h>

namespace Hephaestian
{

namespace
{
	/**
	 * @brief The repository's assets folder, looked up once.
	 * @return It in Debug builds when it exists, otherwise an empty path (Release builds never know where the repo is).
	 */
	[[nodiscard]] const std::filesystem::path& getSourceAssetsDirectory()
	{
		static const std::filesystem::path SourceAssetsDirectory = []() -> std::filesystem::path
		{
#ifdef HEPHAESTIAN_SOURCE_ASSETS_DIRECTORY
			const std::filesystem::path RepositoryAssetsDirectory{ HEPHAESTIAN_SOURCE_ASSETS_DIRECTORY };
			std::error_code DirectoryCheckError;
			if (std::filesystem::is_directory(RepositoryAssetsDirectory, DirectoryCheckError))
			{
				LOG("Debug build: assets are read from the repository ({})", RepositoryAssetsDirectory.string());
				return RepositoryAssetsDirectory;
			}
			LOGW("Couldn't find the repository's assets folder ({}): using the copy next to the exe.", RepositoryAssetsDirectory.string());
#endif
			return {};
		}();
		return SourceAssetsDirectory;
	}
}

std::filesystem::path getGameAssetsDirectory()
{
	const std::filesystem::path& SourceAssetsDirectory = getSourceAssetsDirectory();
	return SourceAssetsDirectory.empty() ? Vulkanised::Utilities::File::getRuntimeAssetsDirectory() : SourceAssetsDirectory;
}

std::filesystem::path resolveAssetPath(const std::filesystem::path& AssetRelativePath)
{
	if (AssetRelativePath.is_absolute())
	{
		return AssetRelativePath;
	}
	if (const std::filesystem::path& SourceAssetsDirectory = getSourceAssetsDirectory(); !SourceAssetsDirectory.empty())
	{
		std::filesystem::path PathInsideRepository = SourceAssetsDirectory / AssetRelativePath;
		std::error_code ExistenceCheckError;
		if (std::filesystem::exists(PathInsideRepository, ExistenceCheckError))
		{
			return PathInsideRepository;
		}
	}
	return Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / AssetRelativePath;
}

}
