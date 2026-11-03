#include "Vulkanised/utils/FileUtils.h"

#include <system_error>

// Every OS has its own way of answering "where am I running from?". Kept in this one .cpp so <windows.h> (and its
// min/max macros) never leak into anyone else's headers.
#if defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#elif defined(__APPLE__)
	#include <mach-o/dyld.h>
#endif

namespace Vulkanised::Utilities::File
{

namespace
{
	[[nodiscard]] std::filesystem::path askOperatingSystemForExecutablePath() noexcept
	{
		#if defined(_WIN32)
			// Grow the buffer until the whole path fits (long paths can exceed MAX_PATH)
			std::wstring ExecutablePathBuffer(MAX_PATH, L'\0');
			while (true)
			{
				const DWORD WrittenCharacterCount = GetModuleFileNameW(nullptr, ExecutablePathBuffer.data(), static_cast<DWORD>(ExecutablePathBuffer.size()));
				if (WrittenCharacterCount == 0)
				{
					return {};
				}
				if (WrittenCharacterCount < ExecutablePathBuffer.size())
				{
					ExecutablePathBuffer.resize(WrittenCharacterCount);
					return std::filesystem::path(ExecutablePathBuffer);
				}
				ExecutablePathBuffer.resize(ExecutablePathBuffer.size() * 2);
			}
		#elif defined(__APPLE__)
			uint32_t ExecutablePathBufferSize{ 0 };
			_NSGetExecutablePath(nullptr, &ExecutablePathBufferSize);	// First call only tells us the size needed
			std::string ExecutablePathBuffer(ExecutablePathBufferSize, '\0');
			if (_NSGetExecutablePath(ExecutablePathBuffer.data(), &ExecutablePathBufferSize) != 0)
			{
				return {};
			}
			ExecutablePathBuffer.resize(std::char_traits<char>::length(ExecutablePathBuffer.c_str()));
			std::error_code CanonicalisationError;
			const std::filesystem::path CanonicalExecutablePath = std::filesystem::canonical(ExecutablePathBuffer, CanonicalisationError);	// Resolves symlinks and "../"
			return CanonicalisationError ? std::filesystem::path(ExecutablePathBuffer) : CanonicalExecutablePath;
		#else
			// Linux (and most other Unixes with procfs)
			std::error_code SymlinkReadError;
			const std::filesystem::path ExecutablePath = std::filesystem::read_symlink("/proc/self/exe", SymlinkReadError);
			return SymlinkReadError ? std::filesystem::path{} : ExecutablePath;
		#endif
	}
}

const std::filesystem::path& getExecutableDirectory() noexcept
{
	// Thread-safe one-time initialisation (C++11 magic statics)
	static const std::filesystem::path ExecutableDirectory = []() noexcept -> std::filesystem::path
	{
		const std::filesystem::path ExecutablePath = askOperatingSystemForExecutablePath();
		if (ExecutablePath.empty())
		{
			// Should never happen, but the working directory is a better last resort than crashing.
			// (No LOG macros here: the logger itself uses this function to find its files.)
			std::error_code CurrentPathError;
			return std::filesystem::current_path(CurrentPathError);
		}
		return ExecutablePath.parent_path();
	}();

	return ExecutableDirectory;
}

const std::filesystem::path& getRuntimeAssetsDirectory() noexcept
{
	static const std::filesystem::path RuntimeAssetsDirectory = getExecutableDirectory() / c_RuntimeAssetsFolderName;
	return RuntimeAssetsDirectory;
}

}
