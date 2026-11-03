# external/simdutf/CMakeLists.txt
# simdutf v9.3.3 (https://github.com/simdutf/simdutf), the official amalgamated release files.
# Same story as simdjson: fastgltf v1.0.0 uses it (only for base64 decoding of embedded glTF buffers).

add_library(simdutf STATIC simdutf.cpp)
add_library(simdutf::simdutf ALIAS simdutf)

target_include_directories(simdutf SYSTEM PUBLIC .)
target_compile_features(simdutf PUBLIC cxx_std_20)

# Exactly what fastgltf configures: everything but base64 is switched off, to save compile time and binary size
target_compile_definitions(simdutf PUBLIC
    SIMDUTF_FEATURE_BASE64=1
    SIMDUTF_FEATURE_DETECT_ENCODING=0
    SIMDUTF_FEATURE_ASCII=0
    SIMDUTF_FEATURE_LATIN1=0
    SIMDUTF_FEATURE_UTF8=0
    SIMDUTF_FEATURE_UTF16=0
    SIMDUTF_FEATURE_UTF32=0
)
