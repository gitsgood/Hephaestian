cmake_minimum_required(VERSION 3.31)
project(Vulkanised)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force GLFW not to build its own tests, examples, or docs
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Tell CMake to look inside the GLFW folder
add_subdirectory(external/glfw-3.5.1)

# Glaze is a fast json library for cpp. It requires c++ 23
add_subdirectory(external/glaze)

# stb_image is header only, but I'd still like to add it like every other library, for consistency
add_subdirectory(external/stb_image)

# Ask CMake to find the Vulkan SDK installed on the computer
find_package(Vulkan REQUIRED)

# Dear ImGui (core + GLFW/Vulkan backends). After find_package(Vulkan), since its Vulkan backend links Vulkan::Vulkan
add_subdirectory(external/imgui)

# Vulkan Memory Allocator: every buffer and image gets its memory from it (also after find_package(Vulkan))
add_subdirectory(external/vma)

# In my windows I have GLM installed with Vulkan, but in case it isn't (my Mac, which has it via homebrew), we got this
if(NOT MSVC)
    find_package(glm REQUIRED)
endif()

# (The old BUILD_ENV_* IDE detection is gone: runtime files are now found relative to the executable itself,
#  see Utilities::File::getExecutableDirectory, so it no longer matters which IDE or build folder we're in.)

# Set source files...
set(SOURCES
    src/main.cpp
    include/Vulkanised/Utilities.h
    include/Vulkanised/utils/VulkanUtils.h include/Vulkanised/utils/TimeUtils.h   include/Vulkanised/utils/FileUtils.h   src/utils/FileUtils.cpp   include/Vulkanised/utils/CrossPlatUtils.h
    include/Vulkanised/Logger.h            src/Logger.cpp
    include/Vulkanised/VulkanRenderer.h    src/VulkanRenderer.cpp
    include/Vulkanised/Rendering/GpuMemory.h  src/Rendering/GpuMemory.cpp
    include/Vulkanised/Rendering/FrameStatistics.h
    include/Vulkanised/Rendering/FrameResources.h
    include/Vulkanised/Rendering/ResourceUploader.h  src/Rendering/ResourceUploader.cpp
    include/Vulkanised/Vertex.h
    include/Vulkanised/Mesh.h
)

# all shader and script files shown in IDE
file(GLOB SHADER_FILES CONFIGURE_DEPENDS
    "src/shaders/*.frag"
    "src/shaders/*.vert"
)

# Define your executable using the SOURCES variable
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${SHADER_FILES})

# Precompile header lets goo
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# Tell CMake to actually bother including the headers in my own project (HAS to be after "add_executable")
#target_include_directories(${PROJECT_NAME} PRIVATE include)

# Define include directories with SCOPES
target_include_directories(${PROJECT_NAME} 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        src/
    PRIVATE 
        #src/
)

# More is more.
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

# Absolutely force compiler verbosity, set warning level to 4 (maximum).
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    # Subsystem logic
    # Explicitly set the subsystem to CONSOLE for Windows, if it's on DEBUG config.
    target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>
    #$<$<CONFIG:Release>:/SUBSYSTEM:CONSOLE>    # For comparing debug and release speed with console output...
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# Tell CMake this specific target requires C++23
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

# Link the libraries (if not on Window, we'll probably not be getting glm with vulkan)
set(LIB_LIST
    glfw
    Vulkan::Vulkan
    glaze::glaze
    stb_image
    imgui       # PUBLIC like the rest, so whoever uses Vulkanised can build ImGui windows straight away
    vma
)

if(NOT MSVC)
    list(APPEND LIB_LIST glm::glm)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC ${LIB_LIST})

# ---------------------------------------------------------------------------------------------------------------------
# SHADERS
# Everything generated goes into the BUILD tree, never the source tree. That way a clean build is truly clean, nothing
# generated can end up committed by accident, and Debug/Release (or two different IDEs) can't step on each other's files.
# ---------------------------------------------------------------------------------------------------------------------
set(VULKANISED_COMPILED_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")     # .spv files, deployed next to executables
set(VULKANISED_GENERATED_HEADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")  # embedded_*.h files, baked into Release builds
file(MAKE_DIRECTORY "${VULKANISED_COMPILED_SHADER_DIRECTORY}" "${VULKANISED_GENERATED_HEADER_DIRECTORY}/shaders")

set(SPV_OUTPUTS "")
set(GENERATED_HEADERS "")
set(EMBEDDED_SHADER_ARRAY_DEFINITIONS "")
set(EMBEDDED_SHADER_TABLE_ENTRIES "")

# Loop through every discovered shader
foreach(SHADER ${SHADER_FILES})
    # Extract just the filename (e.g., "shader.vert")
    get_filename_component(FILE_NAME ${SHADER} NAME)

    # --- Part A: SPIR-V Compilation (.spv) ---
    set(OUTPUT_SPV "${VULKANISED_COMPILED_SHADER_DIRECTORY}/${FILE_NAME}.spv")

    add_custom_command(
        OUTPUT ${OUTPUT_SPV}
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER}" -o "${OUTPUT_SPV}"
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V"
        VERBATIM
    )
    list(APPEND SPV_OUTPUTS ${OUTPUT_SPV})

    # --- Part B: Embedded Header Generation (.h) ---
    # Create a safe header name: "shader.vert" -> "embedded_shader_vert.h"
    string(REPLACE "." "_" SAFE_NAME ${FILE_NAME})
    set(HEADER_NAME "embedded_${SAFE_NAME}.h")
    set(HEADER_PATH "${VULKANISED_GENERATED_HEADER_DIRECTORY}/shaders/${HEADER_NAME}")

    add_custom_command(
        OUTPUT ${HEADER_PATH}
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER}" -mfmt=c -o "${HEADER_PATH}"
        DEPENDS ${SHADER}
        COMMENT "Generating embedded header ${HEADER_NAME}"
        VERBATIM
    )
    list(APPEND GENERATED_HEADERS ${HEADER_PATH})

    # --- Part C: Entry in the embedded shader registry (see below) ---
    string(APPEND EMBEDDED_SHADER_ARRAY_DEFINITIONS
        "\tinline constexpr std::uint32_t c_EmbeddedSpirv_${SAFE_NAME}[] =\n#include \"shaders/${HEADER_NAME}\"\n\t;\n\n")
    string(APPEND EMBEDDED_SHADER_TABLE_ENTRIES
        "\t\tEmbeddedShader{ \"${FILE_NAME}.spv\", c_EmbeddedSpirv_${SAFE_NAME} },\n")
endforeach()

# --- EMBEDDED SHADER REGISTRY ---
# One generated header that lists EVERY shader by its .spv file name, so Release builds can look any of them up by name
# (Utilities::File::readFile). Adding a new shader to src/shaders is all it takes, no C++ to touch.
# file(CONFIGURE) only rewrites it when the shader list actually changes, so it doesn't trigger needless rebuilds.
file(CONFIGURE OUTPUT "${VULKANISED_GENERATED_HEADER_DIRECTORY}/shaders/embedded_shader_registry.h" @ONLY CONTENT [=[
// GENERATED BY Vulkanised/CMakeLists.txt, DO NOT EDIT. Only used in Release builds (NDEBUG).
#ifndef VULKANISED_EMBEDDED_SHADER_REGISTRY_H
#define VULKANISED_EMBEDDED_SHADER_REGISTRY_H

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Vulkanised::EmbeddedShaders
{
@EMBEDDED_SHADER_ARRAY_DEFINITIONS@
	struct EmbeddedShader
	{
		std::string_view m_SpirvFileName;			// e.g. "shader.vert.spv", the same name as the file deployed to assets/shaders
		std::span<const std::uint32_t> m_SpirvWords;
	};

	inline constexpr std::array c_AllEmbeddedShaders{
@EMBEDDED_SHADER_TABLE_ENTRIES@	};
}

#endif // !VULKANISED_EMBEDDED_SHADER_REGISTRY_H
]=])

# Create a single target to trigger all shader work (prefixed, so it can't clash with a "CompileShaders" in a parent project)
add_custom_target(VulkanisedCompileShaders DEPENDS ${SPV_OUTPUTS} ${GENERATED_HEADERS})
add_dependencies(${PROJECT_NAME} VulkanisedCompileShaders)

# Tell the library about the generated headers
target_sources(${PROJECT_NAME} PRIVATE ${GENERATED_HEADERS})

# PUBLIC, because utils/FileUtils.h (a public header) includes "shaders/embedded_shader_*.h" in Release builds,
# so anything that includes Vulkanised headers needs to find them too
target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${VULKANISED_GENERATED_HEADER_DIRECTORY}>)

# ---------------------------------------------------------------------------------------------------------------------
# RUNTIME ASSET DEPLOYMENT
# At runtime, Vulkanised looks for everything in "<folder of the executable>/assets" (see Utilities::File).
# Any executable using Vulkanised calls vulkanised_deploy_runtime_assets() once, and the build keeps that folder up to date.
# ---------------------------------------------------------------------------------------------------------------------
set(VULKANISED_RUNTIME_ASSETS_FOLDER_NAME "assets" CACHE INTERNAL "Folder next to the executable that holds every runtime asset (must match Utilities::File::c_RuntimeAssetsFolderName)")
set(VULKANISED_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Vulkanised's source directory, e.g. for its test Textures folder")
set_property(GLOBAL PROPERTY VULKANISED_COMPILED_SHADER_FILES ${SPV_OUTPUTS})

# Kept so anything still using the old name doesn't silently get an empty path
set(VULKANISED_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Deprecated, use VULKANISED_SOURCE_DIRECTORY")

# vulkanised_deploy_runtime_assets(<executable target>
#     [ASSET_ROOT_DIRECTORIES <dir>...]      # EVERYTHING inside is deployed, no names needed: each sub-folder goes to
#                                            # assets/<folder name>/, each loose file to assets/<file name>
#                                            # (e.g. <repo>/assets with Models/ and Engine/ -> assets/Models, assets/Engine)
#     [ASSET_DIRECTORIES <dir>...]           # single folders, copied to assets/<folder name>/ (e.g. .../Textures -> assets/Textures)
#     [FILES_NEXT_TO_EXECUTABLE <file>...]   # loose files copied right beside the executable (e.g. the logger settings)
# )
# Vulkanised's compiled shaders are always deployed to assets/shaders/.
# The deployed assets/ folder is a MIRROR of its sources: the step re-runs whenever a file is edited, added, moved or
# deleted, or a folder appears or disappears (CONFIGURE_DEPENDS + a fingerprint of everything deployed), and then rebuilds
# assets/ from scratch, so nothing removed, moved or renamed in the source can linger next to the executable.
# Two sources deploying the same name (e.g. a Textures folder in both) is a configure error, rather than one silently winning.
function(vulkanised_deploy_runtime_assets TARGET_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 DEPLOY "" "" "ASSET_ROOT_DIRECTORIES;ASSET_DIRECTORIES;FILES_NEXT_TO_EXECUTABLE")

    set(EXECUTABLE_DIRECTORY "$<TARGET_FILE_DIR:${TARGET_NAME}>")
    set(ASSETS_DESTINATION "${EXECUTABLE_DIRECTORY}/${VULKANISED_RUNTIME_ASSETS_FOLDER_NAME}")
    set(DEPLOY_COMMANDS "")
    set(DEPLOY_DEPENDENCIES "")
    set(DEPLOYED_ASSET_NAMES "shaders")         # Names already taken inside assets/, to catch two sources clashing
    set(LOOSE_ASSET_FILES "")

    # 0. Expand the asset roots into their folders and loose files (non-recursive: one level, the rest comes with each folder)
    foreach(ASSET_ROOT_DIRECTORY ${DEPLOY_ASSET_ROOT_DIRECTORIES})
        get_filename_component(ABSOLUTE_ASSET_ROOT_DIRECTORY "${ASSET_ROOT_DIRECTORY}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${ABSOLUTE_ASSET_ROOT_DIRECTORY}")
            message(FATAL_ERROR "vulkanised_deploy_runtime_assets: asset root directory '${ABSOLUTE_ASSET_ROOT_DIRECTORY}' does not exist")
        endif()
        # LIST_DIRECTORIES + CONFIGURE_DEPENDS: a folder added or removed here re-runs CMake by itself on the next build
        file(GLOB ASSET_ROOT_ELEMENTS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${ABSOLUTE_ASSET_ROOT_DIRECTORY}/*")
        foreach(ASSET_ROOT_ELEMENT ${ASSET_ROOT_ELEMENTS})
            if(IS_DIRECTORY "${ASSET_ROOT_ELEMENT}")
                list(APPEND DEPLOY_ASSET_DIRECTORIES "${ASSET_ROOT_ELEMENT}")
            else()
                list(APPEND LOOSE_ASSET_FILES "${ASSET_ROOT_ELEMENT}")
            endif()
        endforeach()
    endforeach()

    # The whole assets/ folder is rebuilt from scratch every time this step runs: that's what makes it a mirror
    list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ASSETS_DESTINATION}")
    list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E make_directory "${ASSETS_DESTINATION}")

    # 1. Vulkanised's own compiled shaders
    get_property(COMPILED_SHADER_FILES GLOBAL PROPERTY VULKANISED_COMPILED_SHADER_FILES)
    list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E make_directory "${ASSETS_DESTINATION}/shaders")
    list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${COMPILED_SHADER_FILES} "${ASSETS_DESTINATION}/shaders")
    list(APPEND DEPLOY_DEPENDENCIES ${COMPILED_SHADER_FILES})

    # 2. Whole asset folders
    foreach(ASSET_DIRECTORY ${DEPLOY_ASSET_DIRECTORIES})
        get_filename_component(ABSOLUTE_ASSET_DIRECTORY "${ASSET_DIRECTORY}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${ABSOLUTE_ASSET_DIRECTORY}")
            message(FATAL_ERROR "vulkanised_deploy_runtime_assets: asset directory '${ABSOLUTE_ASSET_DIRECTORY}' does not exist")
        endif()
        get_filename_component(ASSET_FOLDER_NAME "${ABSOLUTE_ASSET_DIRECTORY}" NAME)
        if("${ASSET_FOLDER_NAME}" IN_LIST DEPLOYED_ASSET_NAMES)
            message(FATAL_ERROR "vulkanised_deploy_runtime_assets: two sources both want to deploy 'assets/${ASSET_FOLDER_NAME}' (the second is '${ABSOLUTE_ASSET_DIRECTORY}'). Rename one of them.")
        endif()
        list(APPEND DEPLOYED_ASSET_NAMES "${ASSET_FOLDER_NAME}")

        file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${ABSOLUTE_ASSET_DIRECTORY}/*")
        list(APPEND DEPLOY_DEPENDENCIES ${ASSET_FILES})
        list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ABSOLUTE_ASSET_DIRECTORY}" "${ASSETS_DESTINATION}/${ASSET_FOLDER_NAME}")
    endforeach()

    # 3. Loose files at the top of an asset root
    foreach(LOOSE_ASSET_FILE ${LOOSE_ASSET_FILES})
        get_filename_component(LOOSE_ASSET_FILE_NAME "${LOOSE_ASSET_FILE}" NAME)
        if("${LOOSE_ASSET_FILE_NAME}" IN_LIST DEPLOYED_ASSET_NAMES)
            message(FATAL_ERROR "vulkanised_deploy_runtime_assets: two sources both want to deploy 'assets/${LOOSE_ASSET_FILE_NAME}' (the second is '${LOOSE_ASSET_FILE}'). Rename one of them.")
        endif()
        list(APPEND DEPLOYED_ASSET_NAMES "${LOOSE_ASSET_FILE_NAME}")
        list(APPEND DEPLOY_DEPENDENCIES "${LOOSE_ASSET_FILE}")
        list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E copy "${LOOSE_ASSET_FILE}" "${ASSETS_DESTINATION}/${LOOSE_ASSET_FILE_NAME}")
    endforeach()

    # Deleting a file (or adding an empty folder) doesn't make anything "newer", so timestamps alone would never re-run the
    # step. A fingerprint of everything deployed, in the command itself, does: when it changes, the build system re-runs it.
    string(SHA1 DEPLOYED_FILE_LIST_FINGERPRINT "${DEPLOYED_ASSET_NAMES};${DEPLOY_DEPENDENCIES}")
    list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E echo "Deployed asset list ${DEPLOYED_FILE_LIST_FINGERPRINT}")

    # 4. Loose files beside the executable
    foreach(LOOSE_FILE ${DEPLOY_FILES_NEXT_TO_EXECUTABLE})
        get_filename_component(ABSOLUTE_LOOSE_FILE "${LOOSE_FILE}" ABSOLUTE)
        list(APPEND DEPLOY_DEPENDENCIES "${ABSOLUTE_LOOSE_FILE}")
        list(APPEND DEPLOY_COMMANDS COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ABSOLUTE_LOOSE_FILE}" "${EXECUTABLE_DIRECTORY}")
    endforeach()

    # The stamp file is what makes this incremental: it's only rebuilt (= everything re-synced) when a dependency is newer
    set(DEPLOY_STAMP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_runtime_assets_$<CONFIG>.stamp")
    add_custom_command(
        OUTPUT "${DEPLOY_STAMP_FILE}"
        ${DEPLOY_COMMANDS}
        COMMAND "${CMAKE_COMMAND}" -E touch "${DEPLOY_STAMP_FILE}"
        DEPENDS ${DEPLOY_DEPENDENCIES}
        COMMENT "Deploying runtime assets for ${TARGET_NAME}..."
        VERBATIM
    )
    add_custom_target(${TARGET_NAME}DeployRuntimeAssets DEPENDS "${DEPLOY_STAMP_FILE}")

    # Shaders have to exist before they can be copied (this was the race in the old copy step)
    add_dependencies(${TARGET_NAME}DeployRuntimeAssets VulkanisedCompileShaders)
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}DeployRuntimeAssets)
endfunction()
