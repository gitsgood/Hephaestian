#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <fstream>
#include <filesystem>

namespace Vulkanised
{
	namespace Utilities
	{
		namespace File
		{
			// Path constants. The definitions are set up in the CMakeLists file. It's where the logic on how the compiler knows lies.
			#if defined(BUILD_ENV_VISUAL_STUDIO)
			inline constexpr const char* c_Path{ "../../../" };
			#elif defined(BUILD_ENV_QTCREATOR)
			inline constexpr const char* c_Path{ "../../" };
			#elif defined(BUILD_ENV_VSCODE)
			inline constexpr const char* c_Path{ "../" }; // When in VsCode, even on Mac, it only has one depth
			#else
			inline constexpr const char* c_Path{ "../../" }; // fallback e.g. Mac
			#endif
			inline constexpr const char* c_LogSettingsFileName{ "VulkanisedLoggerSettings.json" };
			inline constexpr const char* c_LogFileName{ "VulkanisedLog.log" };

			inline constexpr const char* c_LogSeparation{ "\n------------------------------------------------------------\n\n" };

			// Returns a file buffer. 
			// fileName will automatically be prefixed with c_Path
			[[nodiscard]] inline std::vector<char> readFile(std::string_view fileName)
			{
				std::filesystem::path filePath{ std::filesystem::path(c_Path) / fileName };

				// Open stream from given file
				// std::ios::binary tells stream to read file as binary
				// std::ios::ate tells stream to start reading from end of file
				std::ifstream file(filePath, std::ios::binary | std::ios::ate);

				// Check if file stream succesfully opened
				if (!file.is_open())
				{
					throw std::runtime_error("Failed to open shader file: " + std::string(fileName) + "\nAttempted path: " + std::filesystem::absolute(filePath).string());
				}

				// Get current read position and use to resize file buffer
				size_t fileSize = (size_t)file.tellg();
				std::vector<char> fileBuffer(fileSize);

				// Move read position (seek to) the start of the file
				file.seekg(0);

				// Read the file data into the buffer (stream "fileSize" in total)
				file.read(fileBuffer.data(), fileSize);

				// Close the stream
				file.close();

				return fileBuffer;
			}
		}
	}
}

#endif // !FILEUTILS_H
