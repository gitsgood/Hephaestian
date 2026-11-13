# external/audio_decoders/CMakeLists.txt
# Single-file audio decoders, all public domain (or MIT-0), vendored like stb_image:
#	- dr_mp3, dr_wav, dr_flac (github.com/mackron/dr_libs, commit a466dbe, 2026-10-26): LICENSE-dr_libs
#	- stb_vorbis (github.com/nothings/stb, commit a5ccde4, 2026-10-27): LICENSE-stb
# OpenAL only plays raw samples, these turn files into them.

# The implementations are compiled once, in their own little library: their own warnings never reach ours
add_library(audio_decoders STATIC audio_decoders_impl.c)
target_include_directories(audio_decoders SYSTEM PUBLIC include)
