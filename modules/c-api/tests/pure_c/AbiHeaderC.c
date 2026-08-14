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
_Static_assert(sizeof(CNA_BackBufferInfo) == 24U,
               "CNA_BackBufferInfo layout must remain stable");
_Static_assert(sizeof(CNA_Vector2) == 8U && sizeof(CNA_Rectangle) == 16U,
               "CNA two-dimensional value layouts must remain stable");
_Static_assert(sizeof(CNA_SpriteSortMode) == sizeof(uint32_t) &&
                   CNA_SPRITE_SORT_MODE_DEFERRED == UINT32_C(0) &&
                   CNA_SPRITE_SORT_MODE_IMMEDIATE == UINT32_C(1) &&
                   CNA_SPRITE_SORT_MODE_TEXTURE == UINT32_C(2) &&
                   CNA_SPRITE_SORT_MODE_BACK_TO_FRONT == UINT32_C(3) &&
                   CNA_SPRITE_SORT_MODE_FRONT_TO_BACK == UINT32_C(4),
               "CNA SpriteBatch sort identities must remain stable");
_Static_assert(sizeof(CNA_SpriteEffects) == sizeof(uint32_t) &&
                   CNA_SPRITE_EFFECT_NONE == UINT32_C(0) &&
                   CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY == UINT32_C(1) &&
                   CNA_SPRITE_EFFECT_FLIP_VERTICALLY == UINT32_C(2),
               "CNA SpriteBatch effect bits must remain stable");
_Static_assert(sizeof(CNA_SpriteBatchBeginInfo) == 16U,
               "CNA_SpriteBatchBeginInfo layout must remain stable");
_Static_assert(sizeof(CNA_SpriteCommand) == 72U,
               "CNA_SpriteCommand layout must remain stable");
_Static_assert(sizeof(CNA_Key) == sizeof(uint32_t),
               "CNA_Key must have a fixed-width representation");
_Static_assert(CNA_KEY_NONE == UINT32_C(0) && CNA_KEY_A == UINT32_C(65) &&
                   CNA_KEY_F24 == UINT32_C(135) && CNA_KEY_OEM_CLEAR == UINT32_C(254),
               "Representative CNA keyboard identities must remain stable");
_Static_assert(sizeof(CNA_KeyboardState) == 40U,
               "CNA_KeyboardState layout must remain stable");
_Static_assert(sizeof(CNA_MouseState) == 32U,
               "CNA_MouseState layout must remain stable");
_Static_assert(CNA_MOUSE_BUTTON_LEFT == UINT32_C(1) &&
                   CNA_MOUSE_BUTTON_X2 == UINT32_C(16),
               "CNA mouse button bits must remain stable");
_Static_assert(sizeof(CNA_PlayerIndex) == sizeof(uint32_t) &&
                   CNA_PLAYER_INDEX_ONE == UINT32_C(0) &&
                   CNA_PLAYER_INDEX_FOUR == UINT32_C(3),
               "CNA player identities must remain stable");
_Static_assert(sizeof(CNA_GamePadDeadZone) == sizeof(uint32_t) &&
                   CNA_GAMEPAD_DEAD_ZONE_NONE == UINT32_C(0) &&
                   CNA_GAMEPAD_DEAD_ZONE_CIRCULAR == UINT32_C(2),
               "CNA gamepad dead-zone identities must remain stable");
_Static_assert(CNA_GAMEPAD_BUTTON_DPAD_UP == UINT32_C(1) &&
                   CNA_GAMEPAD_BUTTON_A == UINT32_C(0x00001000) &&
                   CNA_GAMEPAD_BUTTON_ALL == UINT32_C(0x7fffffff),
               "CNA gamepad button bits must remain stable");
_Static_assert(sizeof(CNA_GamePadAnalogState) == 24U,
               "CNA_GamePadAnalogState layout must remain stable");
_Static_assert(sizeof(CNA_GamePadState) == 48U,
               "CNA_GamePadState layout must remain stable");
_Static_assert(sizeof(CNA_TouchLocationState) == sizeof(uint32_t) &&
                   CNA_TOUCH_LOCATION_INVALID == UINT32_C(0) &&
                   CNA_TOUCH_LOCATION_MOVED == UINT32_C(3),
               "CNA touch-location identities must remain stable");
_Static_assert(CNA_TOUCH_MAX_TOUCHES == UINT32_C(8),
               "CNA touch snapshot capacity must remain stable");
_Static_assert(sizeof(CNA_TouchLocation) == 32U,
               "CNA_TouchLocation layout must remain stable");
_Static_assert(sizeof(CNA_TouchCapabilities) == 16U,
               "CNA_TouchCapabilities layout must remain stable");
_Static_assert(sizeof(CNA_TouchState) == 272U,
               "CNA_TouchState layout must remain stable");
