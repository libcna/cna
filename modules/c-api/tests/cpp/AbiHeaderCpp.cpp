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
static_assert(sizeof(CNA_BackBufferInfo) == 24U);
static_assert(sizeof(CNA_Vector2) == 8U);
static_assert(sizeof(CNA_Rectangle) == 16U);
static_assert(sizeof(CNA_SpriteSortMode) == sizeof(uint32_t));
static_assert(CNA_SPRITE_SORT_MODE_DEFERRED == UINT32_C(0));
static_assert(CNA_SPRITE_SORT_MODE_IMMEDIATE == UINT32_C(1));
static_assert(CNA_SPRITE_SORT_MODE_TEXTURE == UINT32_C(2));
static_assert(CNA_SPRITE_SORT_MODE_BACK_TO_FRONT == UINT32_C(3));
static_assert(CNA_SPRITE_SORT_MODE_FRONT_TO_BACK == UINT32_C(4));
static_assert(sizeof(CNA_SpriteEffects) == sizeof(uint32_t));
static_assert(CNA_SPRITE_EFFECT_NONE == UINT32_C(0));
static_assert(CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY == UINT32_C(1));
static_assert(CNA_SPRITE_EFFECT_FLIP_VERTICALLY == UINT32_C(2));
static_assert(sizeof(CNA_SpriteBatchBeginInfo) == 16U);
static_assert(sizeof(CNA_SpriteCommand) == 72U);
static_assert(sizeof(CNA_Key) == sizeof(uint32_t));
static_assert(CNA_KEY_NONE == UINT32_C(0));
static_assert(CNA_KEY_A == UINT32_C(65));
static_assert(CNA_KEY_F24 == UINT32_C(135));
static_assert(CNA_KEY_OEM_CLEAR == UINT32_C(254));
static_assert(sizeof(CNA_KeyboardState) == 40U);
static_assert(sizeof(CNA_MouseState) == 32U);
static_assert(CNA_MOUSE_BUTTON_LEFT == UINT32_C(1));
static_assert(CNA_MOUSE_BUTTON_X2 == UINT32_C(16));
static_assert(sizeof(CNA_PlayerIndex) == sizeof(uint32_t));
static_assert(CNA_PLAYER_INDEX_ONE == UINT32_C(0));
static_assert(CNA_PLAYER_INDEX_FOUR == UINT32_C(3));
static_assert(sizeof(CNA_GamePadDeadZone) == sizeof(uint32_t));
static_assert(CNA_GAMEPAD_DEAD_ZONE_NONE == UINT32_C(0));
static_assert(CNA_GAMEPAD_DEAD_ZONE_CIRCULAR == UINT32_C(2));
static_assert(CNA_GAMEPAD_BUTTON_DPAD_UP == UINT32_C(1));
static_assert(CNA_GAMEPAD_BUTTON_A == UINT32_C(0x00001000));
static_assert(CNA_GAMEPAD_BUTTON_ALL == UINT32_C(0x7fffffff));
static_assert(sizeof(CNA_GamePadAnalogState) == 24U);
static_assert(sizeof(CNA_GamePadState) == 48U);
static_assert(sizeof(CNA_TouchLocationState) == sizeof(uint32_t));
static_assert(CNA_TOUCH_LOCATION_INVALID == UINT32_C(0));
static_assert(CNA_TOUCH_LOCATION_MOVED == UINT32_C(3));
static_assert(CNA_TOUCH_MAX_TOUCHES == UINT32_C(8));
static_assert(sizeof(CNA_TouchLocation) == 32U);
static_assert(sizeof(CNA_TouchCapabilities) == 16U);
static_assert(sizeof(CNA_TouchState) == 272U);
static_assert(sizeof(CNA_AudioChannels) == sizeof(uint32_t));
static_assert(CNA_AUDIO_CHANNELS_MONO == UINT32_C(1));
static_assert(CNA_AUDIO_CHANNELS_STEREO == UINT32_C(2));
static_assert(sizeof(CNA_SoundState) == sizeof(uint32_t));
static_assert(CNA_SOUND_STATE_PLAYING == UINT32_C(0));
static_assert(CNA_SOUND_STATE_PAUSED == UINT32_C(1));
static_assert(CNA_SOUND_STATE_STOPPED == UINT32_C(2));
static_assert(sizeof(CNA_SoundEffectCreateInfo) == 24U);
static_assert(sizeof(CNA_SoundEffectInstanceInfo) == 32U);
