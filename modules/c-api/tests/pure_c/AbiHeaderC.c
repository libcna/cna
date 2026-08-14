// SPDX-License-Identifier: MS-PL

#include <CNA/C/abi.h>
#include <CNA/C/core.h>
#include <CNA/C/graphics.h>
#include <CNA/C/cna.h>

_Static_assert(CNA_ABI_VERSION == CNA_ABI_VERSION_ENCODE(0, 1, 0),
               "CNA C ABI version encoding must remain stable");
_Static_assert(sizeof(CNA_Result) == sizeof(uint32_t),
               "CNA_Result must have a fixed-width representation");
_Static_assert(sizeof(CNA_Handle) == sizeof(uint64_t),
               "CNA_Handle must have a fixed-width representation");
_Static_assert(sizeof(CNA_ErrorCategory) == sizeof(uint32_t),
               "CNA_ErrorCategory must have a fixed-width representation");
_Static_assert(sizeof(CNA_GameTime) == 24U,
               "CNA_GameTime layout must remain stable");
_Static_assert(sizeof(CNA_Color) == 4U,
               "CNA_Color layout must remain stable");
_Static_assert(sizeof(CNA_GraphicsCapability) == sizeof(uint32_t),
               "CNA_GraphicsCapability must have a fixed-width representation");
_Static_assert(sizeof(CNA_GraphicsRendererType) == sizeof(uint32_t),
               "CNA_GraphicsRendererType must have a fixed-width representation");
_Static_assert(sizeof(CNA_GraphicsCapabilityFlags) == sizeof(uint64_t),
               "CNA_GraphicsCapabilityFlags must have a fixed-width representation");
_Static_assert(sizeof(CNA_RendererInfo) == 32U,
               "CNA_RendererInfo layout must remain stable");
_Static_assert(CNA_GRAPHICS_RENDERER_SDL_RENDERER == UINT32_C(1) &&
                   CNA_GRAPHICS_RENDERER_PORTABLEGL == UINT32_C(46),
               "CNA renderer identities must remain stable");
_Static_assert(CNA_GRAPHICS_CAPABILITY_THREE_D == UINT32_C(0) &&
                   CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING == UINT32_C(12),
               "CNA graphics capability identities must remain stable");
_Static_assert(sizeof(CNA_SurfaceFormat) == sizeof(uint32_t),
               "CNA_SurfaceFormat must have a fixed-width representation");
_Static_assert(CNA_SURFACE_FORMAT_COLOR == UINT32_C(0) &&
                   CNA_SURFACE_FORMAT_USHORT_EXT == UINT32_C(26),
               "CNA surface-format identities must remain stable");
_Static_assert(sizeof(CNA_Texture2DCreateInfo) == 24U,
               "CNA_Texture2DCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_Texture2DInfo) == 24U,
               "CNA_Texture2DInfo layout must remain stable");
