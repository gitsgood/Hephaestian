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

# In my windows I have GLM installed with Vulkan, but in case it isn't (my Mac, which has it via homebrew), we got this
if(NOT MSVC)
    find_package(glm REQUIRED)
endif()

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
    include/Vulkanised/Utilities.h
    include/Vulkanised/utils/VulkanUtils.h include/Vulkanised/utils/TimeUtils.h   include/Vulkanised/utils/FileUtils.h   include/Vulkanised/utils/CrossPlatUtils.h
    include/Vulkanised/Logger.h            src/Logger.cpp
    include/Vulkanised/VulkanRenderer.h    src/VulkanRenderer.cpp
    include/Vulkanised/Vertex.h
    include/Vulkanised/Mesh.h              src/Mesh.cpp
)

# all shader and script files shown in IDE
file(GLOB SHADER_FILES
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
    PRIVATE 
        src/
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
    target_link_libraries(${PROJECT_NAME} glfw Vulkan::Vulkan glaze::glaze glm::glm stb_image)
else()
    target_link_libraries(${PROJECT_NAME} glfw Vulkan::Vulkan glaze::glaze stb_image)
endif()

# Define the directories
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")

# Ensure the output directory exists
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

if(APPLE)
    add_custom_target(CompileShaders
        COMMENT "Compiling Vulkan shaders for macOS"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER_SRC_DIR}/shader.vert" -o "${SHADER_SRC_DIR}/vert.spv"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER_SRC_DIR}/shader.frag" -o "${SHADER_SRC_DIR}/frag.spv"
    )
elseif(MSVC)
    add_custom_target(CompileShaders
        COMMENT "Compiling Vulkan shaders for Windows"
        COMMAND "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe" -V "${SHADER_SRC_DIR}/shader.vert" -o "${SHADER_SRC_DIR}/vert.spv"
        COMMAND "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe" -V "${SHADER_SRC_DIR}/shader.frag" -o "${SHADER_SRC_DIR}/frag.spv"
    )
endif()

# For the absolute off chance someone opens this project on XCode(Mac)...
# this would be useful for an eventual wgpu addition, for now it sits...
#if (XCODE)
#    set_target_properties(App PROPERTIES
#        XCODE_GENERATE_SCHEME ON
#        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
#    )
#endif()

add_dependencies(${PROJECT_NAME} CompileShaders)

# This tells anyone who includes this directory where the assets are
set(VULKANISED_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Path to Vulkanised assets")