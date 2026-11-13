// The one place the decoders' implementations are compiled (everyone else only includes the headers)

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

// stb_vorbis is a .c file holding both halves: users define STB_VORBIS_HEADER_ONLY before including it
#include "stb_vorbis.c"
