cmake_minimum_required(VERSION 3.31)
project(Hephaestian)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Force GLFW not to build its own tests, examples, or docs
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# My very own rendering library (while it include its own stuff, it seems we still need to manually include them)
add_subdirectory(external/Vulkanised)

# EnTT, the ECS. Header only, vendored like stb_image
add_subdirectory(external/entt)

# MODEL IMPORTING
# - ufbx: .obj (+ .mtl) and .fbx
add_subdirectory(external/ufbx)
# - fastgltf: .gltf and .glb. It needs simdjson + simdutf, which we vendor ourselves instead of letting it download them
add_subdirectory(external/simdjson)
add_subdirectory(external/simdutf)
set(FASTGLTF_DOWNLOAD_SIMDJSON OFF CACHE BOOL "" FORCE)
set(FASTGLTF_DOWNLOAD_SIMDUTF OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/fastgltf SYSTEM)  # SYSTEM: its headers can't trip our warnings-as-errors

# Set source files...
set(SOURCES
    src/main.cpp
    include/Hephaestian/Input/InputManager.h                    src/Input/InputManager.cpp
    include/Hephaestian/ECS/Components.h
    include/Hephaestian/ECS/Scene.h                             src/ECS/Scene.cpp
    include/Hephaestian/ECS/Systems/TransformHierarchySystem.h  src/ECS/Systems/TransformHierarchySystem.cpp
    include/Hephaestian/ECS/Systems/MovementSystems.h           src/ECS/Systems/MovementSystems.cpp
    include/Hephaestian/ECS/Systems/CameraSystem.h              src/ECS/Systems/CameraSystem.cpp
    include/Hephaestian/ECS/Systems/RenderSystem.h              src/ECS/Systems/RenderSystem.cpp
    include/Hephaestian/Assets/ModelImporter.h                  src/Assets/ModelImporter.cpp
    include/Hephaestian/Settings/HephaestianSettings.h          src/Settings/HephaestianSettings.cpp
    include/Hephaestian/Settings/HephaestianSettingsManager.h   src/Settings/HephaestianSettingsManager.cpp
    include/Hephaestian/UI/HephaestianSettingsWidget.h          src/UI/HephaestianSettingsWidget.cpp
)

# Define your executable using the SOURCES variable
add_executable(${PROJECT_NAME} ${SOURCES})

# RUNTIME ASSETS (the workaround is dead, long live the workaround)
# Everything the game needs at runtime is copied to "<exe folder>/assets", and only when something actually changed.
# Shaders always come along. Add more folders to ASSET_DIRECTORIES as the game gets its own assets (e.g. assets/Models).
vulkanised_deploy_runtime_assets(${PROJECT_NAME}
    ASSET_DIRECTORIES
        "${VULKANISED_SOURCE_DIRECTORY}/Textures"           # -> assets/Textures
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/Models"         # -> assets/Models (every model in here gets loaded at startup)
    FILES_NEXT_TO_EXECUTABLE
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanisedLoggerSettings.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/HephaestianSettings.json"
)
# (Paths are resolved from the exe's own location, so the working directory you launch from no longer matters.)

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
set(LIB_LIST
    Vulkanised # This include everything it includes for itself, so: glaze, vulkan, glm, stb_image and glfw (not that it should need to...)
    EnTT::EnTT # The ECS
    fastgltf::fastgltf # glTF/GLB importing
    ufbx::ufbx # OBJ/FBX importing
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIB_LIST})