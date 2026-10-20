#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <fstream>
#include <filesystem>

// If we are in release mode, use the CMake generated headers
#ifdef NDEBUG
	inline constexpr uint32_t embedded_shader_vert[] =
	#include "shaders/embedded_shader_vert.h"
	;

	inline constexpr uint32_t embedded_shader_frag[] =
	#include "shaders/embedded_shader_frag.h"
	;
#endif

namespace Vulkanised
{
	namespace Utilities
	{
		namespace File
		{
			// Path constants. The definitions are set up in the CMakeLists file. It's where the logic on how the compiler knows lies.
			#if defined(BUILD_ENV_VISUAL_STUDIO)
				#define PATH "../../../"
			#elif defined(BUILD_ENV_QTCREATOR)
				#define PATH "../../"
			#elif defined(BUILD_ENV_VSCODE)
				#define PATH "../../" "../"		// When in VsCode, even on Mac, it only has one depth
			#else
				#define PATH "../../"			// fallback e.g. Mac
			#endif

			inline constexpr const char* c_Path{ PATH };
			inline constexpr const char* c_LogSettingsFileName{ "VulkanisedLoggerSettings.json" };
			inline constexpr const char* c_LogFileName{ "VulkanisedLog.log" };
			inline constexpr const char* c_LogSeparation{ "\n------------------------------------------------------------\n\n" };

			// Returns a file buffer. 
			// fileName will automatically be prefixed with c_Path
			[[nodiscard]] inline std::vector<uint32_t> readFile(std::string_view fileName)
			{
				#ifdef NDEBUG
					if (fileName.contains("vert"))
					{
						return std::vector<uint32_t>(std::begin(embedded_shader_vert), std::end(embedded_shader_vert));
					}
					else if (fileName.contains("frag"))
					{
						return std::vector<uint32_t>(std::begin(embedded_shader_frag), std::end(embedded_shader_frag));
					}
				#endif

				auto fileBuffer = [fileName](std::filesystem::path inFilePath) -> std::vector<uint32_t> {
					// Open stream from given file
					// std::ios::binary tells stream to read file as binary
					// std::ios::ate tells stream to start reading from end of file
					std::ifstream file(inFilePath, std::ios::binary | std::ios::ate);

					// Check if file stream succesfully opened
					if (!file.is_open())
					{
						throw std::runtime_error("Failed to open shader file: " + std::string(fileName) + "\nAttempted path: " + std::filesystem::absolute(inFilePath).string());
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
					};

				return std::vector<uint32_t>(fileBuffer(std::filesystem::path{ std::filesystem::path(c_Path) / fileName }));
			}
		}
	}
}

#endif // !FILEUTILS_H
