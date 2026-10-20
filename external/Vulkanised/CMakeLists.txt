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
elseif($ENV{VSCODE_CMAKE_TOOLS}) # Extension should be detectable like that
    add_compile_definitions(BUILD_ENV_VSCODE)
    message(STATUS "=== VS Code detected ===")
elseif(${CMAKE_BINARY_DIR} MATCHES ".*build/.*")
    add_compile_definitions(BUILD_ENV_QTCREATOR)
    message(STATUS "=== Qt Creator detected ===")
endif()

# Set source files...
set(SOURCES
    src/main.cpp
    include/Vulkanised/Utilities.h
    include/Vulkanised/utils/VulkanUtils.h include/Vulkanised/utils/TimeUtils.h   include/Vulkanised/utils/FileUtils.h   include/Vulkanised/utils/CrossPlatUtils.h
    include/Vulkanised/Logger.h            src/Logger.cpp
    include/Vulkanised/VulkanRenderer.h    src/VulkanRenderer.cpp
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
)

if(NOT MSVC)
    list(APPEND LIB_LIST glm::glm)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC ${LIB_LIST})

# Define the directories
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/bin")
# Ensure the output directory exists
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SPV_OUTPUTS "")
set(GENERATED_HEADERS "")

# Loop through every discovered shader
foreach(SHADER ${SHADER_FILES})
    # Extract just the filename (e.g., "shader.vert")
    get_filename_component(FILE_NAME ${SHADER} NAME)
    
    # --- Part A: SPIR-V Compilation (.spv) ---
    set(OUTPUT_SPV "${SHADER_BIN_DIR}/${FILE_NAME}.spv")
    
    add_custom_command(
        OUTPUT ${OUTPUT_SPV}
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER}" -o ${OUTPUT_SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V"
    )
    list(APPEND SPV_OUTPUTS ${OUTPUT_SPV})

    # --- Part B: Embedded Header Generation (.h) ---
    # Create a safe header name: "shader.vert" -> "embedded_shader_vert.h"
    string(REPLACE "." "_" SAFE_NAME ${FILE_NAME})
    set(HEADER_NAME "embedded_${SAFE_NAME}.h")
    set(HEADER_PATH "${SHADER_SRC_DIR}/${HEADER_NAME}")
    
    add_custom_command(
        OUTPUT ${HEADER_PATH}
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER}" -mfmt=c -o ${HEADER_PATH}
        DEPENDS ${SHADER}
        COMMENT "Generating embedded header ${HEADER_NAME}"
    )
    list(APPEND GENERATED_HEADERS ${HEADER_PATH})
endforeach()

# Create a single target to trigger all shader work
add_custom_target(CompileShaders DEPENDS ${SPV_OUTPUTS} ${GENERATED_HEADERS})
add_dependencies(${PROJECT_NAME} CompileShaders)

# Tell the executable about the generated headers
target_sources(${PROJECT_NAME} PRIVATE ${GENERATED_HEADERS})

# Make sure your target depends on this generated file
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# This tells anyone who includes this directory where the assets are
set(VULKANISED_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Path to Vulkanised assets")
