# external/vma/CMakeLists.txt
# Vulkan Memory Allocator (AMD, MIT licence, see the top of include/vk_mem_alloc.h). Vendored from the Vulkan SDK
# (version 3.4.0), so builds don't depend on whichever SDK happens to be installed.

# The implementation is compiled once, in its own little library, like stb_image: its own warnings never reach ours
add_library(vma STATIC vma_impl.cpp)
target_include_directories(vma SYSTEM PUBLIC include)
target_link_libraries(vma PUBLIC Vulkan::Vulkan)

# We link the Vulkan loader directly, so VMA can call the Vulkan functions straight away (no function pointer table to fill)
target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=1 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)
set_target_properties(vma PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
