# external/ufbx/CMakeLists.txt
# ufbx v0.23.1 (https://github.com/ufbx/ufbx): a single C file that loads .fbx and .obj (+ .mtl), very fast.

add_library(ufbx STATIC ufbx.c)
add_library(ufbx::ufbx ALIAS ufbx)

# SYSTEM so our /W4 + warnings-as-errors never trip on third party code
target_include_directories(ufbx SYSTEM PUBLIC .)
