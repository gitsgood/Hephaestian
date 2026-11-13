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

# AUDIO
# - OpenAL Soft 1.25.2 (github.com/kcat/openal-soft): mixing, 3D sound, HRTF, on its own thread. LGPL, which is why it
#   stays a DLL (OpenAL32.dll / libopenal.so) next to the executable rather than being linked in.
#   Only the library itself: no utilities, examples, tests or install rules.
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_UPDATE_BUILD_VERSION OFF CACHE BOOL "" FORCE)	# Don't ask git about OpenAL Soft's version: it isn't a git checkout here
set(ALSOFT_ENABLE_MODULES OFF CACHE BOOL "" FORCE)		# C++20 modules are optional for it, and only make the build pickier
set(ALSOFT_EAX OFF CACHE BOOL "" FORCE)					# Legacy Creative EAX effects: not needed
set(LIBTYPE SHARED CACHE STRING "" FORCE)
add_subdirectory(external/openal-soft SYSTEM)
# Newer Apple clang (21+) trips OpenAL Soft's own forced -Werror=function-effects in its CoreAudio backend (its realtime
# callbacks are lambdas converted to plain C function pointers). That's their code, not ours: keep it a warning, and mute
# the deprecated-CoreAudio-API noise from the same file while we're at it. (Added after their flags, so these win.)
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(OpenAL PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=function-effects -Wno-function-effects>
        -Wno-deprecated-declarations
        -Wno-deprecated-anon-enum-enum-conversion
        -Wno-unused-variable
    )
endif()
# - Decoders (MP3, WAV, FLAC, Ogg Vorbis): OpenAL only plays raw samples
add_subdirectory(external/audio_decoders)

# Set source files...
set(SOURCES
    src/main.cpp
    include/Hephaestian/Input/InputManager.h                    src/Input/InputManager.cpp
    include/Hephaestian/ECS/Components.h
    include/Hephaestian/ECS/Scene.h                             src/ECS/Scene.cpp
    include/Hephaestian/ECS/SceneFile.h                         src/ECS/SceneSerialization.cpp
    include/Hephaestian/Assets/AssetLibrary.h                   src/Assets/AssetLibrary.cpp
    include/Hephaestian/Assets/SpriteSheet.h                    src/Assets/SpriteSheet.cpp
    include/Hephaestian/ECS/Systems/TransformHierarchySystem.h  src/ECS/Systems/TransformHierarchySystem.cpp
    include/Hephaestian/ECS/Systems/MovementSystems.h           src/ECS/Systems/MovementSystems.cpp
    include/Hephaestian/ECS/Systems/CameraSystem.h              src/ECS/Systems/CameraSystem.cpp
    include/Hephaestian/ECS/Systems/RenderSystem.h              src/ECS/Systems/RenderSystem.cpp
    include/Hephaestian/ECS/Systems/SpriteSystems.h             src/ECS/Systems/SpriteSystems.cpp
    include/Hephaestian/ECS/Systems/AudioSystems.h              src/ECS/Systems/AudioSystems.cpp
    include/Hephaestian/Audio/AudioEngine.h                      src/Audio/AudioEngine.cpp
    include/Hephaestian/Audio/AudioDecoder.h                     src/Audio/AudioDecoder.cpp
    include/Hephaestian/Assets/ModelImporter.h                  src/Assets/ModelImporter.cpp
    include/Hephaestian/Settings/HephaestianSettings.h          src/Settings/HephaestianSettings.cpp
    include/Hephaestian/Settings/HephaestianSettingsManager.h   src/Settings/HephaestianSettingsManager.cpp
    include/Hephaestian/UI/HephaestianSettingsWidget.h          src/UI/HephaestianSettingsWidget.cpp
)

# Define your executable using the SOURCES variable
add_executable(${PROJECT_NAME} ${SOURCES})

# Windows: the exe's own icon (Explorer, shortcuts, taskbar), also picked up by GLFW for the window.
# Regenerate resources/Hephaestian.ico if resources/Hephaestos.png changes.
if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE resources/Hephaestian.rc)
endif()

# RUNTIME ASSETS (the workaround is dead, long live the workaround)
# Everything the game needs at runtime is copied to "<exe folder>/assets", and only when something actually changed.
# Shaders always come along, and so does every folder inside our own assets/ (no need to list them one by one).
vulkanised_deploy_runtime_assets(${PROJECT_NAME}
    ASSET_ROOT_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"                # Every folder/file inside -> assets/<same name> (Models, Scenes, ...)
    ASSET_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"             # -> assets/resources (the engine icon: Linux window icon, usable as a texture too)
        "${VULKANISED_SOURCE_DIRECTORY}/Textures"           # -> assets/Textures (Vulkanised's test textures, outside our assets folder)
    FILES_NEXT_TO_EXECUTABLE
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanisedLoggerSettings.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/HephaestianSettings.json"
)
# (Paths are resolved from the exe's own location, so the working directory you launch from no longer matters.)

# PRECOMPILE HEADER WOO
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# Tell CMake to actually bother including the headers in my own project (HAS to be after "add_executable")
target_include_directories(${PROJECT_NAME} PRIVATE include)

# Debug builds only: where the repository's assets folder is, so "Save scene" can also update the copy the build deploys
# (otherwise the next build would mirror the old version back next to the exe). Release builds never know the repo path.
target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Debug>:HEPHAESTIAN_SOURCE_ASSETS_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\">")

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
    OpenAL::OpenAL # Audio (a DLL, see above)
    audio_decoders # MP3/WAV/FLAC/Ogg decoding
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIB_LIST})

# OpenAL Soft is a DLL: it has to sit next to the executable, like the assets do
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying OpenAL Soft next to the executable"
)