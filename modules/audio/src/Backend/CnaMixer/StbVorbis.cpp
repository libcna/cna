// SPDX-License-Identifier: MS-PL
//
// The one translation unit that compiles stb_vorbis (third_party/stb/stb_vorbis.c, v1.22, public
// domain / MIT), for the CNA mixer's Ogg Vorbis decoding. Built with warnings off: it is
// third-party code, kept byte-identical to upstream.

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
