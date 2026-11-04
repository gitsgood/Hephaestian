#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

// If we are in release mode, use the CMake generated headers (generated into the build folder, see CMakeLists.txt).
// The registry holds every shader in src/shaders, looked up by .spv file name.
#ifdef NDEBUG
	#include "shaders/embedded_shader_registry.h"
#endif

namespace Vulkanised
{
	namespace Utilities
	{
		namespace File
		{
			// RUNTIME LAYOUT (kept up to date by vulkanised_deploy_runtime_assets in CMake):
			//	<executable folder>/
			//		Hephaestian.exe
			//		VulkanisedLoggerSettings.json
			//		VulkanisedLog.log
			//		assets/
			//			shaders/	(compiled .spv)
			//			Textures/
			//			...			(whatever else the game deploys)
			//
			// Everything is found relative to the EXECUTABLE, never to the working directory. The old way (guessing "../../../"
			// from which IDE built us) broke as soon as the exe was launched from anywhere else.

			inline constexpr const char* c_RuntimeAssetsFolderName{ "assets" };		// Must match VULKANISED_RUNTIME_ASSETS_FOLDER_NAME in CMake
			inline constexpr const char* c_ShadersFolderName{ "shaders" };
			inline constexpr const char* c_TexturesFolderName{ "Textures" };

			inline constexpr const char* c_LogSettingsFileName{ "VulkanisedLoggerSettings.json" };
			inline constexpr const char* c_LogFileName{ "VulkanisedLog.log" };
			inline constexpr const char* c_LogSeparation{ "\n------------------------------------------------------------\n\n" };

			// Absolute folder the running executable lives in. Asked to the OS once, then cached.
			[[nodiscard]] const std::filesystem::path& getExecutableDirectory() noexcept;

			// <executable folder>/assets
			[[nodiscard]] const std::filesystem::path& getRuntimeAssetsDirectory() noexcept;

			// Returns a file buffer, read from the given path (use getRuntimeAssetsDirectory() to build it).
			// In Release, the shaders come from the embedded headers instead of the disk.
			[[nodiscard]] inline std::vector<uint32_t> readFile(const std::filesystem::path& FilePath)
			{
				#ifdef NDEBUG
					// Exact file name match (the old "contains vert/frag" check would hand out shader.vert for ANY vertex shader)
					const std::string FileNameOnly{ FilePath.filename().string() };
					for (const auto& CurrentEmbeddedShader : EmbeddedShaders::c_AllEmbeddedShaders)
					{
						if (CurrentEmbeddedShader.m_SpirvFileName == FileNameOnly)
						{
							return std::vector<uint32_t>(CurrentEmbeddedShader.m_SpirvWords.begin(), CurrentEmbeddedShader.m_SpirvWords.end());
						}
					}
					// Not embedded: fall through and read it from disk like Debug does
				#endif

				// Open stream from given file
				// std::ios::binary tells stream to read file as binary
				// std::ios::ate tells stream to start reading from end of file
				std::ifstream file(FilePath, std::ios::binary | std::ios::ate);

				// Check if file stream succesfully opened
				if (!file.is_open())
				{
					throw std::runtime_error("Failed to open file: " + FilePath.filename().string() + "\nAttempted path: " + std::filesystem::absolute(FilePath).string());
				}

				// Get current read position and use to resize file buffer
				size_t fileSize = (size_t)file.tellg();
				std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

				// Move read position (seek to) the start of the file
				file.seekg(0);

				// Read the file data into the buffer (stream "fileSize" in total)
				file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

				// Close the stream
				file.close();

				return buffer;
			}
		}
	}
}

#endif // !FILEUTILS_H
