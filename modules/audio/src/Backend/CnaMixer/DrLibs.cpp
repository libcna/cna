// SPDX-License-Identifier: MS-PL
//
// The one translation unit that compiles dr_mp3 and dr_flac (third_party/dr_libs, v0.7.3 and
// v0.13.3, public domain / MIT No Attribution), for the CNA mixer's MP3 and FLAC decoding
// (plans/plan_x11.md X11-0161). Built with warnings off: it is third-party code, kept
// byte-identical to upstream.

#include "Backend/CnaMixer/DrLibsApi.hpp"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
