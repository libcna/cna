// SPDX-License-Identifier: MS-PL

#include <CNA/C/abi.h>
#include <CNA/C/core.h>
#include <CNA/C/graphics.h>
#include <CNA/C/cna.h>

static_assert(CNA_ABI_VERSION == CNA_ABI_VERSION_ENCODE(0, 1, 0));
static_assert(sizeof(CNA_Result) == sizeof(uint32_t));
static_assert(sizeof(CNA_Handle) == sizeof(uint64_t));
static_assert(sizeof(CNA_ErrorCategory) == sizeof(uint32_t));
static_assert(sizeof(CNA_GameTime) == 24U);
static_assert(sizeof(CNA_Color) == 4U);
static_assert(sizeof(CNA_GraphicsCapability) == sizeof(uint32_t));
static_assert(sizeof(CNA_GraphicsRendererType) == sizeof(uint32_t));
static_assert(sizeof(CNA_GraphicsCapabilityFlags) == sizeof(uint64_t));
static_assert(sizeof(CNA_RendererInfo) == 32U);
static_assert(CNA_GRAPHICS_RENDERER_SDL_RENDERER == UINT32_C(1));
static_assert(CNA_GRAPHICS_RENDERER_PORTABLEGL == UINT32_C(46));
static_assert(CNA_GRAPHICS_CAPABILITY_THREE_D == UINT32_C(0));
static_assert(CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING == UINT32_C(12));
static_assert(sizeof(CNA_SurfaceFormat) == sizeof(uint32_t));
static_assert(CNA_SURFACE_FORMAT_COLOR == UINT32_C(0));
static_assert(CNA_SURFACE_FORMAT_USHORT_EXT == UINT32_C(26));
static_assert(sizeof(CNA_Texture2DCreateInfo) == 24U);
static_assert(sizeof(CNA_Texture2DInfo) == 24U);
