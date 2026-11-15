/**
 * @file AssetPaths.h
 * @brief Where the game's assets are read from: the repository's assets folder in Debug builds, the copy next to the
 * executable otherwise.
 *
 * Debug builds read straight from `<repo>/assets`, so a scene or sprite sheet edited there is picked up by the next
 * (re)load, without building first. Anything that isn't in the repository's assets folder (shaders, resources/,
 * Vulkanised's Textures/) still comes from the copy next to the executable. Release builds only ever use that copy.
 */
#ifndef HEPHAESTIAN_ASSETPATHS_H
#define HEPHAESTIAN_ASSETPATHS_H

#include <filesystem>

namespace Hephaestian
{

/**
 * @brief The folder scenes are listed and loaded from, and asset paths are relative to.
 * @return `<repo>/assets` in Debug builds (when it exists), otherwise `<executable folder>/assets`.
 */
[[nodiscard]] std::filesystem::path getGameAssetsDirectory();

/**
 * @brief Turns an asset path from a scene file or the settings into an absolute one.
 *
 * Debug builds use the repository's file when there is one, and fall back to the copy next to the executable for
 * what only exists there (resources/, Textures/).
 * @param AssetRelativePath Relative to the assets folder (an absolute path is returned as it is).
 * @return The absolute path (not canonicalised).
 */
[[nodiscard]] std::filesystem::path resolveAssetPath(const std::filesystem::path& AssetRelativePath);

}

#endif // !HEPHAESTIAN_ASSETPATHS_H
