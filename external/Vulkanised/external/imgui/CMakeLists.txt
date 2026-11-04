# external/imgui/CMakeLists.txt
# Dear ImGui v1.93.0 (https://github.com/ocornut/imgui): the core files plus the GLFW and Vulkan backends, nothing else.

add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_demo.cpp                  # ImGui::ShowDemoWindow(), the best ImGui documentation there is
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_vulkan.cpp
)
add_library(imgui::imgui ALIAS imgui)

# SYSTEM so our /W4 + warnings-as-errors never trip on third party code
target_include_directories(imgui SYSTEM PUBLIC . backends)

target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
