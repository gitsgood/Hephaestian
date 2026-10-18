cmake_minimum_required(VERSION 3.31)
project(Hephaestian)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force GLFW not to build its own tests, examples, or docs
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Tell CMake to look inside the GLFW folder
#add_subdirectory(external/glfw-3.5.1)

# Glaze is a fast json library for cpp. It requires c++ 23
#add_subdirectory(external/glaze)

# stb_image is header only, but I'd still like to add it like every other library, for consistency
#add_subdirectory(external/stb_image)

# My very own rendering library
add_subdirectory(external/Vulkanised)

# Detect build environment based on CMAKE_BINARY_DIR
# Used to set correct PATH and some clean up
if(${CMAKE_BINARY_DIR} MATCHES ".*out/.*")
    add_compile_definitions(BUILD_ENV_VISUAL_STUDIO)
    message(STATUS "=== Visual Studio detected ===")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build")
        file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/build")
    endif()
elseif($ENV{VSCODE_CMAKE_TOOLS}) # Extension should be detectable like that
    add_compile_definitions(BUILD_ENV_VSCODE)
    message(STATUS "=== VS Code detected ===")
    # I added these folders in the gitignore, so this shouldn't even be necessary, but might as well...
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/CMakePresets.json")
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPresets.json")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out")
        file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/out")
    endif()
elseif(${CMAKE_BINARY_DIR} MATCHES ".*build/.*")
    add_compile_definitions(BUILD_ENV_QTCREATOR)
    message(STATUS "=== Qt Creator detected ===")
    #Deleting preset files not used in Qt - generated in VS 2022
    #This should not fail even if the files are not present
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/CMakePresets.json")
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPresets.json")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out")
        file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/out")
    endif()
endif()

# Set source files...
set(SOURCES
    src/main.cpp
)

# Define your executable using the SOURCES variable
add_executable(${PROJECT_NAME} ${SOURCES})

# ABSOLUTE WORKAROUND
# TODO: MAKE THIS BETTER FFS

# 1. Create a dedicated target for copying assets
add_custom_target(CopyVulkanAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VULKANISED_ASSETS_PATH}/Textures" "${CMAKE_CURRENT_SOURCE_DIR}/Textures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VULKANISED_ASSETS_PATH}/src/shaders" "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
    COMMENT "Copying Vulkanised assets to output directory..."
)

# 2. Tell your game to depend on this target
# This means: "Before you build the game, you MUST run CopyVulkanAssets"
add_dependencies(${PROJECT_NAME} CopyVulkanAssets)

# PRECOMPILE HEADER WOO
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# Tell CMake to actually bother including the headers in my own project (HAS to be after "add_executable")
target_include_directories(${PROJECT_NAME} PRIVATE include)

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
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# Tell CMake this specific target requires C++23
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

# Explicitly set the subsystem to CONSOLE for Windows, if it's on DEBUG config.
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
        $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>
        #$<$<CONFIG:Release>:/SUBSYSTEM:CONSOLE>    # For comparing debug and release speed with console output...
    )
endif()

# Link the libraries (if not on Window, we'll probably not be getting glm with vulkan)
if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} glfw glaze::glaze glm::glm stb_image Vulkanised)
else()
    target_link_libraries(${PROJECT_NAME} glfw glaze::glaze stb_image Vulkanised)
endif()

# For the absolute off chance someone opens this project on XCode(Mac)...
# this would be useful for an eventual wgpu addition, for now it sits...
#if (XCODE)
#    set_target_properties(App PROPERTIES
#        XCODE_GENERATE_SCHEME ON
#        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
#    )
#endif()