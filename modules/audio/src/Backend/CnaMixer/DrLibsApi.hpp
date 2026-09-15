// SPDX-License-Identifier: MS-PL
#pragma once

// dr_mp3's and dr_flac's declarations, with the configuration their one implementation file
// (DrLibs.cpp) is compiled with: memory decoding only -- the mixer reads files itself.

#ifndef DR_MP3_NO_STDIO
#define DR_MP3_NO_STDIO
#endif
#ifndef DR_FLAC_NO_STDIO
#define DR_FLAC_NO_STDIO
#endif
#include "dr_mp3.h"
#include "dr_flac.h"
