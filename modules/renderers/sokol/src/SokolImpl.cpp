// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-3: the single translation unit that instantiates sokol_gfx's STB-style
// implementation, plus the implementation half of the code-generated shader header. Everything
// else in this renderer includes the same two headers for their declarations only.
//
// Kept separate from SokolRenderer.cpp deliberately: sokol_gfx.h's implementation pulls in
// the platform's GL (or D3D11/Metal/WebGPU) headers and is a ~20k-line C body, so isolating it
// keeps it out of the renderer's own incremental rebuilds and stops its macros leaking into CNA
// code.

#define SOKOL_IMPL
#define SOKOL_SHDC_IMPL

// sokol_gfx.h reports internal errors and validation-layer messages through this callback rather
// than writing to stderr itself; sokol_log.h is upstream's ready-made implementation.
#include "sokol_log.h"
#include "sokol_gfx.h"

#include "shaders/sokol_shaders.hpp"
