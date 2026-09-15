// SPDX-License-Identifier: MS-PL
#pragma once

// stb_vorbis's declarations, with the configuration its one implementation file
// (StbVorbis.cpp) is compiled with: memory decoding only -- the mixer reads files itself -- and
// no push-data API.

#ifndef STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_STDIO
#endif
#ifndef STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_PUSHDATA_API
#endif
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
