# external/simdjson/CMakeLists.txt
# simdjson v5.1.0 (https://github.com/simdjson/simdjson), the official amalgamated release files.
# Only here because fastgltf needs it: these are the exact files (and SHA256 hashes) fastgltf v1.0.0 would otherwise
# download at configure time. Vendoring them keeps the build offline and reproducible, like every other dependency.

add_library(simdjson STATIC simdjson.cpp)
add_library(simdjson::simdjson ALIAS simdjson)

target_include_directories(simdjson SYSTEM PUBLIC .)
target_compile_features(simdjson PUBLIC cxx_std_20)
