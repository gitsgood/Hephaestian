# external/entt/CMakeLists.txt
# EnTT v4.0.0 (https://github.com/skypjack/entt), vendored as its official single header.

# Header only, so an INTERFACE library is all we need (same idea as stb_image, minus the .cpp)
add_library(EnTT INTERFACE)
add_library(EnTT::EnTT ALIAS EnTT)

# Tell anyone who links to this library where the header is, so "#include <entt/entt.hpp>" just works
# SYSTEM so our /W4 + warnings-as-errors never trip on third party code
target_include_directories(EnTT SYSTEM INTERFACE include)

# EnTT needs at least C++20 (we use 23 anyways)
target_compile_features(EnTT INTERFACE cxx_std_20)
