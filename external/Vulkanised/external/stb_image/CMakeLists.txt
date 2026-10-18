# external/stb_image/CMakeLists.txt

# Create a static library from the implementation file
add_library(stb_image STATIC stb_impl.cpp)

# Tell anyone who links to this library where the header is
# '.` means look in the current folder (external/stb_image)
target_include_directories(stb_image PUBLIC .)