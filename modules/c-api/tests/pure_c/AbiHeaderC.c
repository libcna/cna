// SPDX-License-Identifier: MS-PL

#include <CNA/C/abi.h>
#include <CNA/C/core.h>
#include <CNA/C/graphics.h>
#include <CNA/C/graphics_resource.h>
#include <CNA/C/cna.h>

#include <stddef.h>

_Static_assert(CNA_ABI_VERSION == CNA_ABI_VERSION_ENCODE(0, 12, 0),
               "CNA C ABI version encoding must remain stable");
_Static_assert(sizeof(CNA_Result) == sizeof(uint32_t),
               "CNA_Result must have a fixed-width representation");
_Static_assert(sizeof(CNA_Handle) == sizeof(uint64_t),
               "CNA_Handle must have a fixed-width representation");
_Static_assert(sizeof(CNA_GraphicsResourceTag) == sizeof(uint64_t),
               "CNA_GraphicsResourceTag must have a fixed-width representation");
_Static_assert(sizeof(CNA_GraphicsResourceEventRegistrationHandle) == sizeof(uint64_t),
               "CNA graphics-resource registration handles must remain stable");
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
_Static_assert(sizeof(CNA_RendererFeature) == sizeof(uint32_t) &&
                   CNA_RENDERER_FEATURE_THREE_DIMENSIONAL_PIPELINE == UINT32_C(0) &&
                   CNA_RENDERER_FEATURE_SHADER_EFFECT_SOURCE_EXECUTION == UINT32_C(8) &&
                   CNA_RENDERER_FEATURE_COMPUTE_IMAGE_BINDING == UINT32_C(19) &&
                   CNA_RENDERER_FEATURE_MAXIMUM == UINT32_C(29),
               "CNA detailed renderer-feature identities must remain stable");
_Static_assert(sizeof(CNA_RendererFeatureSupport) == sizeof(uint32_t) &&
                   CNA_RENDERER_FEATURE_SUPPORT_UNKNOWN == UINT32_C(0) &&
                   CNA_RENDERER_FEATURE_SUPPORT_RESTRICTED == UINT32_C(3),
               "CNA renderer-feature support identities must remain stable");
_Static_assert(sizeof(CNA_RendererLimit) == sizeof(uint32_t) &&
                   CNA_RENDERER_LIMIT_MAX_TEXTURE_DIMENSION == UINT32_C(0) &&
                   CNA_RENDERER_LIMIT_MAXIMUM == UINT32_C(9),
               "CNA renderer-limit identities must remain stable");
_Static_assert(sizeof(CNA_RendererFormatUsageFlags) == sizeof(uint32_t) &&
                   CNA_RENDERER_FORMAT_USAGE_COLOR_TRANSFER == (UINT32_C(1) << 12) &&
                   CNA_RENDERER_FORMAT_USAGE_ALL == UINT32_C(0x1fff),
               "CNA renderer-format usage flags must remain stable");
_Static_assert(sizeof(CNA_EffectParameterClass) == sizeof(uint32_t) &&
                   CNA_EFFECT_PARAMETER_CLASS_SCALAR == UINT32_C(0) &&
                   CNA_EFFECT_PARAMETER_CLASS_VECTOR == UINT32_C(1) &&
                   CNA_EFFECT_PARAMETER_CLASS_MATRIX == UINT32_C(2) &&
                   CNA_EFFECT_PARAMETER_CLASS_OBJECT == UINT32_C(3) &&
                   CNA_EFFECT_PARAMETER_CLASS_STRUCT == UINT32_C(4),
               "CNA effect-parameter class identities must remain stable");
_Static_assert(sizeof(CNA_EffectParameterType) == sizeof(uint32_t) &&
                   CNA_EFFECT_PARAMETER_TYPE_VOID == UINT32_C(0) &&
                   CNA_EFFECT_PARAMETER_TYPE_BOOL == UINT32_C(1) &&
                   CNA_EFFECT_PARAMETER_TYPE_INT32 == UINT32_C(2) &&
                   CNA_EFFECT_PARAMETER_TYPE_SINGLE == UINT32_C(3) &&
                   CNA_EFFECT_PARAMETER_TYPE_STRING == UINT32_C(4) &&
                   CNA_EFFECT_PARAMETER_TYPE_TEXTURE == UINT32_C(5) &&
                   CNA_EFFECT_PARAMETER_TYPE_TEXTURE1D == UINT32_C(6) &&
                   CNA_EFFECT_PARAMETER_TYPE_TEXTURE2D == UINT32_C(7) &&
                   CNA_EFFECT_PARAMETER_TYPE_TEXTURE3D == UINT32_C(8) &&
                   CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE == UINT32_C(9),
               "CNA effect-parameter type identities must remain stable");
_Static_assert(sizeof(CNA_EffectAnnotationHandle) == 8U &&
                   sizeof(CNA_EffectAnnotationCollectionHandle) == 8U,
               "CNA effect-annotation handles must remain stable");
_Static_assert(sizeof(CNA_EffectAnnotationCreateInfo) == 88U &&
                   _Alignof(CNA_EffectAnnotationCreateInfo) == 8U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, name) == 8U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, semantic) == 24U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, row_count) == 40U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, data) == 56U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, cached_string) == 72U,
               "CNA_EffectAnnotationCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_EffectAnnotationInfo) == 24U &&
                   _Alignof(CNA_EffectAnnotationInfo) == 4U,
               "CNA_EffectAnnotationInfo layout must remain stable");
_Static_assert(sizeof(CNA_EffectValueType) == sizeof(uint32_t) &&
                   CNA_EFFECT_VALUE_BOOLEAN == UINT32_C(0) &&
                   CNA_EFFECT_VALUE_INT32 == UINT32_C(1) &&
                   CNA_EFFECT_VALUE_SINGLE == UINT32_C(2) &&
                   CNA_EFFECT_VALUE_MATRIX == UINT32_C(3) &&
                   CNA_EFFECT_VALUE_MATRIX_TRANSPOSE == UINT32_C(4) &&
                   CNA_EFFECT_VALUE_QUATERNION == UINT32_C(5) &&
                   CNA_EFFECT_VALUE_VECTOR2 == UINT32_C(6) &&
                   CNA_EFFECT_VALUE_VECTOR3 == UINT32_C(7) &&
                   CNA_EFFECT_VALUE_VECTOR4 == UINT32_C(8),
               "CNA effect-value identities must remain stable");
_Static_assert(sizeof(CNA_EffectTextureType) == sizeof(uint32_t) &&
                   CNA_EFFECT_TEXTURE_BASE == UINT32_C(0) &&
                   CNA_EFFECT_TEXTURE_2D == UINT32_C(1) &&
                   CNA_EFFECT_TEXTURE_3D == UINT32_C(2) &&
                   CNA_EFFECT_TEXTURE_CUBE == UINT32_C(3),
               "CNA effect-texture identities must remain stable");
_Static_assert(sizeof(CNA_EffectParameterHandle) == 8U &&
                   sizeof(CNA_EffectParameterCollectionHandle) == 8U,
               "CNA effect-parameter handles must remain stable");
_Static_assert(sizeof(CNA_EffectParameterCreateInfo) == 56U &&
                   _Alignof(CNA_EffectParameterCreateInfo) == 8U &&
                   offsetof(CNA_EffectParameterCreateInfo, name) == 8U &&
                   offsetof(CNA_EffectParameterCreateInfo, semantic) == 24U &&
                   offsetof(CNA_EffectParameterCreateInfo, row_count) == 40U &&
                   offsetof(CNA_EffectParameterCreateInfo, parameter_type) == 52U,
               "CNA_EffectParameterCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_EffectParameterInfo) == 24U &&
                   _Alignof(CNA_EffectParameterInfo) == 4U,
               "CNA_EffectParameterInfo layout must remain stable");
_Static_assert(sizeof(CNA_EffectPassHandle) == 8U &&
                   sizeof(CNA_EffectPassCollectionHandle) == 8U &&
                   sizeof(CNA_EffectTechniqueHandle) == 8U &&
                   sizeof(CNA_EffectTechniqueCollectionHandle) == 8U,
               "CNA effect technique/pass handles must remain stable");
_Static_assert(sizeof(CNA_EffectHandle) == 8U,
               "CNA effect handles must remain stable");
_Static_assert(sizeof(CNA_DirectionalLightHandle) == 8U,
               "CNA directional-light handles must remain stable");
_Static_assert(CNA_SKINNED_EFFECT_MAX_BONES == UINT32_C(72),
               "CNA SkinnedEffect maximum bone count must remain stable");
_Static_assert(sizeof(CNA_ColorMatrix4x4) == 64U &&
                   _Alignof(CNA_ColorMatrix4x4) == 4U,
               "CNA_ColorMatrix4x4 layout must remain stable");
_Static_assert(sizeof(CNA_PbrTextureSlot) == sizeof(uint32_t) &&
                   CNA_PBR_TEXTURE_BASE_COLOR == UINT32_C(0) &&
                   CNA_PBR_TEXTURE_NORMAL == UINT32_C(1) &&
                   CNA_PBR_TEXTURE_METALLIC_ROUGHNESS == UINT32_C(2) &&
                   CNA_PBR_TEXTURE_EMISSIVE == UINT32_C(3) &&
                   CNA_PBR_TEXTURE_OCCLUSION == UINT32_C(4),
               "CNA PBR texture-slot identities must remain stable");
_Static_assert(CNA_SKINNED_PBR_EFFECT_MAX_BONES == UINT32_C(72),
               "CNA SkinnedPbrEffect maximum bone count must remain stable");
_Static_assert(sizeof(CNA_ModelBoneHandle) == 8U &&
                   sizeof(CNA_ModelBoneCollectionHandle) == 8U,
               "CNA model-bone handles must remain stable");
_Static_assert(sizeof(CNA_ModelMeshPartHandle) == 8U &&
                   sizeof(CNA_ModelMeshPartCollectionHandle) == 8U &&
                   sizeof(CNA_ModelMeshPartTag) == 8U,
               "CNA model-mesh-part handles and tag must remain stable");
_Static_assert(sizeof(CNA_ModelMeshHandle) == 8U &&
                   sizeof(CNA_ModelMeshCollectionHandle) == 8U &&
                   sizeof(CNA_ModelEffectCollectionHandle) == 8U &&
                   sizeof(CNA_ModelMeshTag) == 8U,
               "CNA model-mesh handles, collections and tag must remain stable");
_Static_assert(sizeof(CNA_ModelHandle) == 8U && sizeof(CNA_ModelTag) == 8U,
               "CNA model handle and tag must remain stable");
_Static_assert(sizeof(CNA_MorphTargetDataEXTHandle) == 8U,
               "CNA morph-target-data handle size changed");
_Static_assert(sizeof(CNA_MorphWeightKeyframeEXTDescriptor) == 56U,
               "CNA morph keyframe descriptor size changed");
_Static_assert(sizeof(CNA_MorphWeightTrackEXTDescriptor) == 24U,
               "CNA morph track descriptor size changed");
_Static_assert(sizeof(CNA_MorphTargetDeltaEXTDescriptor) == 32U,
               "CNA morph delta descriptor size changed");
_Static_assert(sizeof(CNA_MorphTargetDataEXTDescriptor) == 80U,
               "CNA morph data descriptor size changed");
_Static_assert(sizeof(CNA_SkinnedModelEXTHandle) == 8U,
               "CNA skinned-model handle size changed");
_Static_assert(sizeof(CNA_KeyframeEXT) == 48U,
               "CNA skinned keyframe size changed");
_Static_assert(sizeof(CNA_BoneTrackEXTDescriptor) == 24U,
               "CNA skinned bone-track descriptor size changed");
_Static_assert(sizeof(CNA_AnimationClipEXTDescriptor) == 24U,
               "CNA skinned animation-clip descriptor size changed");
_Static_assert(sizeof(CNA_NamedAnimationClipEXTDescriptor) == 40U,
               "CNA named skinned animation-clip descriptor size changed");
_Static_assert(sizeof(CNA_SkinnedModelEXTDescriptor) == 48U,
               "CNA skinned-model descriptor size changed");
_Static_assert(sizeof(CNA_SkinningDataHandle) == 8U &&
                   sizeof(CNA_AnimationPlayerHandle) == 8U,
               "CNA skeletal-animation handle size changed");
_Static_assert(sizeof(CNA_SkinningDataDescriptor) == 64U,
               "CNA SkinningData descriptor size changed");
_Static_assert(sizeof(CNA_RendererInfo) == 32U,
               "CNA_RendererInfo layout must remain stable");
_Static_assert(CNA_GRAPHICS_RENDERER_SDL_RENDERER == UINT32_C(1) &&
                   CNA_GRAPHICS_RENDERER_PORTABLEGL == UINT32_C(46) &&
                   CNA_GRAPHICS_RENDERER_TINYGL == UINT32_C(47) &&
                   CNA_GRAPHICS_RENDERER_IGL == UINT32_C(48) &&
                   CNA_GRAPHICS_RENDERER_PIXIJS == UINT32_C(49) &&
                   CNA_GRAPHICS_RENDERER_NANOVG == UINT32_C(50) &&
                   CNA_GRAPHICS_RENDERER_MAXIMUM == CNA_GRAPHICS_RENDERER_NANOVG,
               "CNA renderer identities must remain stable");
_Static_assert(CNA_GRAPHICS_CAPABILITY_THREE_D == UINT32_C(0) &&
                   CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING == UINT32_C(12) &&
                   CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS == UINT32_C(13) &&
                   CNA_GRAPHICS_CAPABILITY_FLOAT_RENDER_TARGETS == UINT32_C(14) &&
                   CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_RENDER_TARGETS == UINT32_C(15) &&
                   CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_TEXTURE_LINEAR_FILTERING == UINT32_C(16) &&
                   CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS == UINT32_C(17) &&
                   CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW == UINT32_C(18) &&
                   CNA_GRAPHICS_CAPABILITY_MAXIMUM ==
                       CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW &&
                   CNA_GRAPHICS_CAPABILITY_FLAG_INDIRECT_DRAW ==
                       (UINT64_C(1) << CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW),
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
_Static_assert(sizeof(CNA_TextureDataType) == sizeof(uint32_t) &&
                   CNA_TEXTURE_DATA_COLOR == UINT32_C(0) &&
                   CNA_TEXTURE_DATA_USHORT == UINT32_C(17),
               "CNA texture-data identities must remain stable");
_Static_assert(sizeof(CNA_TextureImageFormat) == sizeof(uint32_t) &&
                   CNA_TEXTURE_IMAGE_FORMAT_PNG == UINT32_C(0) &&
                   CNA_TEXTURE_IMAGE_FORMAT_JPEG == UINT32_C(1),
               "CNA texture-image identities must remain stable");
_Static_assert(sizeof(CNA_TextureInfo) == 16U,
               "CNA_TextureInfo layout must remain stable");
_Static_assert(sizeof(CNA_Texture2DTransfer) == 48U &&
                   _Alignof(CNA_Texture2DTransfer) == 8U,
               "CNA_Texture2DTransfer layout must remain stable");
_Static_assert(offsetof(CNA_Texture2DTransfer, level) == 8U &&
                   offsetof(CNA_Texture2DTransfer, has_rectangle) == 12U &&
                   offsetof(CNA_Texture2DTransfer, rectangle) == 16U &&
                   offsetof(CNA_Texture2DTransfer, start_index) == 32U &&
                   offsetof(CNA_Texture2DTransfer, element_count) == 40U,
               "CNA_Texture2DTransfer offsets must remain stable");
_Static_assert(sizeof(CNA_Texture2DDecodeInfo) == 24U,
               "CNA_Texture2DDecodeInfo layout must remain stable");
_Static_assert(sizeof(CNA_Texture2DStorageInfo) == 16U,
               "CNA_Texture2DStorageInfo layout must remain stable");
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
_Static_assert(sizeof(CNA_SpriteScaledCommand) == 72U,
               "CNA_SpriteScaledCommand layout must remain stable");
_Static_assert(offsetof(CNA_SpriteScaledCommand, position) == 16U,
               "CNA_SpriteScaledCommand position must follow the handle");
_Static_assert(offsetof(CNA_SpriteScaledCommand, scale) == 56U,
               "CNA_SpriteScaledCommand scale must follow the origin");
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
_Static_assert(sizeof(CNA_AudioChannels) == sizeof(uint32_t) &&
                   CNA_AUDIO_CHANNELS_MONO == UINT32_C(1) &&
                   CNA_AUDIO_CHANNELS_STEREO == UINT32_C(2),
               "CNA audio channel identities must remain stable");
_Static_assert(sizeof(CNA_SoundState) == sizeof(uint32_t) &&
                   CNA_SOUND_STATE_PLAYING == UINT32_C(0) &&
                   CNA_SOUND_STATE_PAUSED == UINT32_C(1) &&
                   CNA_SOUND_STATE_STOPPED == UINT32_C(2),
               "CNA sound-state identities must remain stable");
_Static_assert(sizeof(CNA_AudioCapabilities) == 16U,
               "CNA_AudioCapabilities layout must remain stable");
_Static_assert(sizeof(CNA_SoundEffectCreateInfo) == 24U,
               "CNA_SoundEffectCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_SoundEffectInstanceInfo) == 32U,
               "CNA_SoundEffectInstanceInfo layout must remain stable");
_Static_assert(sizeof(CNA_Vector3) == 12U,
               "CNA_Vector3 layout must remain stable");
_Static_assert(sizeof(CNA_AudioEmitter) == 60U && offsetof(CNA_AudioEmitter, doppler_scale) == 8U &&
                   offsetof(CNA_AudioEmitter, forward) == 12U &&
                   offsetof(CNA_AudioEmitter, position) == 24U &&
                   offsetof(CNA_AudioEmitter, up) == 36U &&
                   offsetof(CNA_AudioEmitter, velocity) == 48U,
               "CNA_AudioEmitter layout must remain stable");
_Static_assert(sizeof(CNA_AvatarExpression) == 28U &&
                  offsetof(CNA_AvatarExpression, mouth) == 8U,
             "CNA_AvatarExpression layout must remain stable");
_Static_assert(sizeof(CNA_AvatarAppearanceEXT) == 28U &&
                  offsetof(CNA_AvatarAppearanceEXT, skin_color) == 8U,
             "CNA_AvatarAppearanceEXT layout must remain stable");
_Static_assert(sizeof(CNA_AvatarDescriptionInfo) == 32U &&
                  offsetof(CNA_AvatarDescriptionInfo, body_type) == 8U &&
                  offsetof(CNA_AvatarDescriptionInfo, description_byte_count) == 16U &&
                  offsetof(CNA_AvatarDescriptionInfo, is_valid) == 24U,
             "CNA_AvatarDescriptionInfo layout must remain stable");
_Static_assert(sizeof(CNA_AvatarAnimationInfo) == 32U &&
                  offsetof(CNA_AvatarAnimationInfo, bone_transform_count) == 8U &&
                  offsetof(CNA_AvatarAnimationInfo, current_position_ticks) == 16U,
             "CNA_AvatarAnimationInfo layout must remain stable");
_Static_assert(sizeof(CNA_AvatarRendererInfo) == 16U &&
                  offsetof(CNA_AvatarRendererInfo, state) == 8U &&
                  offsetof(CNA_AvatarRendererInfo, is_disposed) == 12U,
             "CNA_AvatarRendererInfo layout must remain stable");
_Static_assert(CNA_AVATAR_RENDERER_BONE_COUNT == INT32_C(71) &&
                  CNA_AVATAR_DESCRIPTION_BYTE_COUNT == UINT64_C(1021),
             "CNA avatar constants must remain stable");
_Static_assert(sizeof(CNA_LeaderboardIdentity) == 76U &&
                  offsetof(CNA_LeaderboardIdentity, game_mode) == 8U &&
                  offsetof(CNA_LeaderboardIdentity, key) == 12U &&
                  CNA_LEADERBOARD_IDENTITY_KEY_CAPACITY == UINT32_C(64),
             "CNA_LeaderboardIdentity layout must remain stable");
_Static_assert(sizeof(CNA_LeaderboardReaderInfo) == 24U &&
                  offsetof(CNA_LeaderboardReaderInfo, page_start) == 8U &&
                  offsetof(CNA_LeaderboardReaderInfo, is_disposed) == 20U,
             "CNA_LeaderboardReaderInfo layout must remain stable");
_Static_assert(sizeof(CNA_LeaderboardEntryInfo) == 24U &&
                  offsetof(CNA_LeaderboardEntryInfo, ranking) == 8U &&
                  offsetof(CNA_LeaderboardEntryInfo, rating) == 16U,
             "CNA_LeaderboardEntryInfo layout must remain stable");
_Static_assert(sizeof(CNA_GameDefaults) == 40U &&
                  offsetof(CNA_GameDefaults, game_difficulty) == 8U &&
                  offsetof(CNA_GameDefaults, has_primary_color) == 20U &&
                  offsetof(CNA_GameDefaults, primary_color) == 32U &&
                  offsetof(CNA_GameDefaults, secondary_color) == 36U,
             "CNA_GameDefaults layout must remain stable");
_Static_assert(sizeof(CNA_PropertyValueKind) == sizeof(uint32_t) &&
                  CNA_PROPERTY_VALUE_KIND_UNKNOWN == UINT32_C(0) &&
                  CNA_PROPERTY_VALUE_KIND_TIME_SPAN == UINT32_C(9) &&
                  CNA_PROPERTY_VALUE_KIND_MAXIMUM == CNA_PROPERTY_VALUE_KIND_TIME_SPAN,
             "CNA property value kinds must remain stable");
_Static_assert(sizeof(CNA_AchievementInfo) == 24U &&
                  offsetof(CNA_AchievementInfo, gamer_score) == 8U &&
                  offsetof(CNA_AchievementInfo, is_earned) == 14U &&
                  offsetof(CNA_AchievementInfo, earned_date_time_ticks) == 16U,
             "CNA_AchievementInfo layout must remain stable");
_Static_assert(sizeof(CNA_GamerPresence) == 16U &&
                  offsetof(CNA_GamerPresence, presence_mode) == 8U &&
                  offsetof(CNA_GamerPresence, presence_value) == 12U,
             "CNA_GamerPresence layout must remain stable");
_Static_assert(sizeof(CNA_GamerPrivileges) == 28U &&
                  offsetof(CNA_GamerPrivileges, allow_communication) == 8U &&
                  offsetof(CNA_GamerPrivileges, allow_online_sessions) == 20U,
             "CNA_GamerPrivileges layout must remain stable");
_Static_assert(sizeof(CNA_GamerProfileInfo) == 32U &&
                  offsetof(CNA_GamerProfileInfo, gamer_score) == 8U &&
                  offsetof(CNA_GamerProfileInfo, is_disposed) == 28U,
             "CNA_GamerProfileInfo layout must remain stable");
_Static_assert(sizeof(CNA_FriendGamerInfo) == 24U &&
                  offsetof(CNA_FriendGamerInfo, friend_request_received_from) == 8U &&
                  offsetof(CNA_FriendGamerInfo, is_playing) == 19U,
             "CNA_FriendGamerInfo layout must remain stable");
_Static_assert(sizeof(CNA_SignedInGamerEventInfo) == 24U &&
                  offsetof(CNA_SignedInGamerEventInfo, gamer) == 16U,
             "CNA_SignedInGamerEventInfo layout must remain stable");
_Static_assert(sizeof(CNA_AvatarBodyType) == sizeof(uint32_t) &&
                  CNA_AVATAR_BODY_TYPE_FEMALE == UINT32_C(0) &&
                  CNA_AVATAR_BODY_TYPE_MALE == UINT32_C(1) &&
                  CNA_AVATAR_BODY_TYPE_MAXIMUM == CNA_AVATAR_BODY_TYPE_MALE,
             "CNA AvatarBodyType identities must remain stable");
_Static_assert(sizeof(CNA_AvatarRendererState) == sizeof(uint32_t) &&
                  CNA_AVATAR_RENDERER_STATE_LOADING == UINT32_C(0) &&
                  CNA_AVATAR_RENDERER_STATE_UNAVAILABLE == UINT32_C(2) &&
                  CNA_AVATAR_RENDERER_STATE_MAXIMUM == CNA_AVATAR_RENDERER_STATE_UNAVAILABLE,
             "CNA AvatarRendererState identities must remain stable");
_Static_assert(sizeof(CNA_AvatarEyebrow) == sizeof(uint32_t) &&
                  CNA_AVATAR_EYEBROW_NEUTRAL == UINT32_C(0) &&
                  CNA_AVATAR_EYEBROW_RAISED == UINT32_C(4) &&
                  CNA_AVATAR_EYEBROW_MAXIMUM == CNA_AVATAR_EYEBROW_RAISED,
             "CNA AvatarEyebrow identities must remain stable");
_Static_assert(sizeof(CNA_AvatarEye) == sizeof(uint32_t) &&
                  CNA_AVATAR_EYE_NEUTRAL == UINT32_C(0) &&
                  CNA_AVATAR_EYE_BLINK == UINT32_C(13) &&
                  CNA_AVATAR_EYE_MAXIMUM == CNA_AVATAR_EYE_BLINK,
             "CNA AvatarEye identities must remain stable");
_Static_assert(sizeof(CNA_AvatarMouth) == sizeof(uint32_t) &&
                  CNA_AVATAR_MOUTH_NEUTRAL == UINT32_C(0) &&
                  CNA_AVATAR_MOUTH_PHONETIC_DTH == UINT32_C(13) &&
                  CNA_AVATAR_MOUTH_MAXIMUM == CNA_AVATAR_MOUTH_PHONETIC_DTH,
             "CNA AvatarMouth identities must remain stable");
_Static_assert(sizeof(CNA_AvatarAnimationPreset) == sizeof(uint32_t) &&
                  CNA_AVATAR_ANIMATION_PRESET_STAND_0 == UINT32_C(0) &&
                  CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN == UINT32_C(30) &&
                  CNA_AVATAR_ANIMATION_PRESET_MAXIMUM == CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN,
             "CNA AvatarAnimationPreset identities must remain stable");
_Static_assert(sizeof(CNA_AvatarBone) == sizeof(uint32_t) &&
                  CNA_AVATAR_BONE_ROOT == UINT32_C(0) &&
                  CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT == UINT32_C(70) &&
                  CNA_AVATAR_BONE_MAXIMUM == CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT,
             "CNA AvatarBone identities must remain stable");
_Static_assert(sizeof(CNA_GamerPresenceMode) == sizeof(uint32_t) &&
                   CNA_GAMER_PRESENCE_MODE_NONE == UINT32_C(0) &&
                   CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE == UINT32_C(59) &&
                   CNA_GAMER_PRESENCE_MODE_MAXIMUM == CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE,
               "CNA GamerPresenceMode identities must remain stable");
_Static_assert(sizeof(CNA_NotificationPosition) == sizeof(uint32_t) &&
                   CNA_NOTIFICATION_POSITION_TOP_LEFT == UINT32_C(0) &&
                   CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT == UINT32_C(8) &&
                   CNA_NOTIFICATION_POSITION_MAXIMUM == CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT,
               "CNA NotificationPosition identities must remain stable");
_Static_assert(sizeof(CNA_GamerZone) == sizeof(uint32_t) &&
                   CNA_GAMER_ZONE_UNKNOWN == UINT32_C(0) &&
                   CNA_GAMER_ZONE_UNDERGROUND == UINT32_C(4) &&
                   CNA_GAMER_ZONE_MAXIMUM == CNA_GAMER_ZONE_UNDERGROUND,
               "CNA GamerZone identities must remain stable");
_Static_assert(sizeof(CNA_LeaderboardKey) == sizeof(uint32_t) &&
                   CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME == UINT32_C(0) &&
                   CNA_LEADERBOARD_KEY_BEST_TIME_RECENT == UINT32_C(3) &&
                   CNA_LEADERBOARD_KEY_MAXIMUM == CNA_LEADERBOARD_KEY_BEST_TIME_RECENT,
               "CNA LeaderboardKey identities must remain stable");
_Static_assert(sizeof(CNA_LeaderboardOutcome) == sizeof(uint32_t) &&
                   CNA_LEADERBOARD_OUTCOME_NONE == UINT32_C(0) &&
                   CNA_LEADERBOARD_OUTCOME_TIE == UINT32_C(3) &&
                   CNA_LEADERBOARD_OUTCOME_MAXIMUM == CNA_LEADERBOARD_OUTCOME_TIE,
               "CNA LeaderboardOutcome identities must remain stable");
_Static_assert(sizeof(CNA_MessageBoxIcon) == sizeof(uint32_t) &&
                   CNA_MESSAGE_BOX_ICON_NONE == UINT32_C(0) &&
                   CNA_MESSAGE_BOX_ICON_ALERT == UINT32_C(3) &&
                   CNA_MESSAGE_BOX_ICON_MAXIMUM == CNA_MESSAGE_BOX_ICON_ALERT,
               "CNA MessageBoxIcon identities must remain stable");
_Static_assert(sizeof(CNA_ControllerSensitivity) == sizeof(uint32_t) &&
                   CNA_CONTROLLER_SENSITIVITY_LOW == UINT32_C(0) &&
                   CNA_CONTROLLER_SENSITIVITY_HIGH == UINT32_C(2) &&
                   CNA_CONTROLLER_SENSITIVITY_MAXIMUM == CNA_CONTROLLER_SENSITIVITY_HIGH,
               "CNA ControllerSensitivity identities must remain stable");
_Static_assert(sizeof(CNA_GameDifficulty) == sizeof(uint32_t) &&
                   CNA_GAME_DIFFICULTY_EASY == UINT32_C(0) &&
                   CNA_GAME_DIFFICULTY_HARD == UINT32_C(2) &&
                   CNA_GAME_DIFFICULTY_MAXIMUM == CNA_GAME_DIFFICULTY_HARD,
               "CNA GameDifficulty identities must remain stable");
_Static_assert(sizeof(CNA_GamerPrivilegeSetting) == sizeof(uint32_t) &&
                   CNA_GAMER_PRIVILEGE_SETTING_BLOCKED == UINT32_C(0) &&
                   CNA_GAMER_PRIVILEGE_SETTING_EVERYONE == UINT32_C(2) &&
                   CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM == CNA_GAMER_PRIVILEGE_SETTING_EVERYONE,
               "CNA GamerPrivilegeSetting identities must remain stable");
_Static_assert(sizeof(CNA_RacingCameraAngle) == sizeof(uint32_t) &&
                   CNA_RACING_CAMERA_ANGLE_BACK == UINT32_C(0) &&
                   CNA_RACING_CAMERA_ANGLE_INSIDE == UINT32_C(2) &&
                   CNA_RACING_CAMERA_ANGLE_MAXIMUM == CNA_RACING_CAMERA_ANGLE_INSIDE,
               "CNA RacingCameraAngle identities must remain stable");
_Static_assert(sizeof(CNA_CueInfo) == 16U && offsetof(CNA_CueInfo, is_created) == 8U &&
                   offsetof(CNA_CueInfo, is_stopping) == 15U &&
                   CNA_AUDIO_ENGINE_CONTENT_VERSION == INT32_C(46),
               "CNA_CueInfo layout and the XACT content version must remain stable");
_Static_assert(sizeof(CNA_AudioListener) == 56U && offsetof(CNA_AudioListener, forward) == 8U &&
                   offsetof(CNA_AudioListener, position) == 20U &&
                   offsetof(CNA_AudioListener, up) == 32U &&
                   offsetof(CNA_AudioListener, velocity) == 44U,
               "CNA_AudioListener layout must remain stable");
_Static_assert(sizeof(CNA_Blend) == sizeof(uint32_t) &&
                   CNA_BLEND_ONE == UINT32_C(0) &&
                   CNA_BLEND_SOURCE_ALPHA_SATURATION == UINT32_C(12),
               "CNA blend identities must remain stable");
_Static_assert(sizeof(CNA_BlendFunction) == sizeof(uint32_t) &&
                   CNA_BLEND_FUNCTION_ADD == UINT32_C(0) &&
                   CNA_BLEND_FUNCTION_MIN == UINT32_C(4),
               "CNA blend-function identities must remain stable");
_Static_assert(sizeof(CNA_ColorWriteChannels) == sizeof(uint32_t) &&
                   CNA_COLOR_WRITE_NONE == UINT32_C(0) &&
                   CNA_COLOR_WRITE_ALL == UINT32_C(15),
               "CNA color-write bits must remain stable");
_Static_assert(sizeof(CNA_CompareFunction) == sizeof(uint32_t) &&
                   CNA_COMPARE_ALWAYS == UINT32_C(0) &&
                   CNA_COMPARE_NOT_EQUAL == UINT32_C(7),
               "CNA comparison identities must remain stable");
_Static_assert(sizeof(CNA_StencilOperation) == sizeof(uint32_t) &&
                   CNA_STENCIL_KEEP == UINT32_C(0) &&
                   CNA_STENCIL_INVERT == UINT32_C(7),
               "CNA stencil-operation identities must remain stable");
_Static_assert(sizeof(CNA_CullMode) == sizeof(uint32_t) &&
                   CNA_CULL_NONE == UINT32_C(0) &&
                   CNA_CULL_COUNTER_CLOCKWISE_FACE == UINT32_C(2),
               "CNA cull-mode identities must remain stable");
_Static_assert(sizeof(CNA_FillMode) == sizeof(uint32_t) &&
                   CNA_FILL_SOLID == UINT32_C(0) && CNA_FILL_WIREFRAME == UINT32_C(1),
               "CNA fill-mode identities must remain stable");
_Static_assert(sizeof(CNA_TextureAddressMode) == sizeof(uint32_t) &&
                   CNA_TEXTURE_ADDRESS_WRAP == UINT32_C(0) &&
                   CNA_TEXTURE_ADDRESS_MIRROR == UINT32_C(2),
               "CNA texture-address identities must remain stable");
_Static_assert(sizeof(CNA_TextureFilter) == sizeof(uint32_t) &&
                   CNA_TEXTURE_FILTER_LINEAR == UINT32_C(0) &&
                   CNA_TEXTURE_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT == UINT32_C(8),
               "CNA texture-filter identities must remain stable");
_Static_assert(sizeof(CNA_BlendState) == 56U,
               "CNA_BlendState layout must remain stable");
_Static_assert(sizeof(CNA_DepthStencilState) == 64U,
               "CNA_DepthStencilState layout must remain stable");
_Static_assert(sizeof(CNA_RasterizerState) == 28U,
               "CNA_RasterizerState layout must remain stable");
_Static_assert(sizeof(CNA_SamplerState) == 40U,
               "CNA_SamplerState layout must remain stable");
_Static_assert(sizeof(CNA_DepthFormat) == sizeof(uint32_t) &&
                   CNA_DEPTH_FORMAT_NONE == UINT32_C(0) &&
                   CNA_DEPTH_FORMAT_DEPTH24_STENCIL8 == UINT32_C(3),
               "CNA depth-format identities must remain stable");
_Static_assert(sizeof(CNA_RenderTargetUsage) == sizeof(uint32_t) &&
                   CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS == UINT32_C(0) &&
                   CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS == UINT32_C(2),
               "CNA render-target usage identities must remain stable");
_Static_assert(sizeof(CNA_CubeMapFace) == sizeof(uint32_t) &&
                   CNA_CUBE_MAP_FACE_POSITIVE_X == UINT32_C(0) &&
                   CNA_CUBE_MAP_FACE_NEGATIVE_Z == UINT32_C(5),
               "CNA cube-face identities must remain stable");
_Static_assert(sizeof(CNA_RenderTarget2DCreateInfo) == 40U,
               "CNA_RenderTarget2DCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_RenderTargetCubeCreateInfo) == 32U,
               "CNA_RenderTargetCubeCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_RenderTargetInfo) == 44U,
               "CNA_RenderTargetInfo layout must remain stable");
_Static_assert(sizeof(CNA_RenderTargetBinding) == 24U,
               "CNA_RenderTargetBinding layout must remain stable");
_Static_assert(sizeof(CNA_Texture3DCreateInfo) == 32U,
               "CNA_Texture3DCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_Texture3DInfo) == 32U,
               "CNA_Texture3DInfo layout must remain stable");
_Static_assert(sizeof(CNA_Texture3DTransfer) == 56U &&
                   offsetof(CNA_Texture3DTransfer, start_index) == 40U &&
                   offsetof(CNA_Texture3DTransfer, element_count) == 48U,
               "CNA_Texture3DTransfer layout must remain stable");
_Static_assert(sizeof(CNA_TextureCubeCreateInfo) == 24U,
               "CNA_TextureCubeCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_TextureCubeInfo) == 24U,
               "CNA_TextureCubeInfo layout must remain stable");
_Static_assert(sizeof(CNA_TextureCubeTransfer) == 56U &&
                   offsetof(CNA_TextureCubeTransfer, rectangle) == 20U &&
                   offsetof(CNA_TextureCubeTransfer, start_index) == 40U,
               "CNA_TextureCubeTransfer layout must remain stable");
_Static_assert(sizeof(CNA_Char16) == sizeof(uint16_t),
               "CNA_Char16 must have a fixed-width representation");
_Static_assert(sizeof(CNA_SpriteFontGlyph) == 56U,
               "CNA_SpriteFontGlyph layout must remain stable");
_Static_assert(sizeof(((CNA_SpriteFontCreateInfo*)0)->glyphs) == sizeof(void*),
               "CNA_SpriteFontCreateInfo must use the platform C pointer representation");
_Static_assert(sizeof(CNA_SpriteFontInfo) == 32U,
               "CNA_SpriteFontInfo layout must remain stable");
_Static_assert(sizeof(CNA_GraphicsProfile) == sizeof(uint32_t) &&
                   CNA_GRAPHICS_PROFILE_REACH == UINT32_C(0) &&
                   CNA_GRAPHICS_PROFILE_HI_DEF == UINT32_C(1),
               "CNA graphics-profile identities must remain stable");
_Static_assert(sizeof(CNA_PresentInterval) == sizeof(uint32_t) &&
                   CNA_PRESENT_INTERVAL_DEFAULT == UINT32_C(0) &&
                   CNA_PRESENT_INTERVAL_IMMEDIATE == UINT32_C(3),
               "CNA presentation-interval identities must remain stable");
_Static_assert(sizeof(CNA_DisplayOrientation) == sizeof(uint32_t) &&
                   CNA_DISPLAY_ORIENTATION_DEFAULT == UINT32_C(0) &&
                   CNA_DISPLAY_ORIENTATION_PORTRAIT == UINT32_C(4),
               "CNA display-orientation bits must remain stable");
_Static_assert(sizeof(CNA_NativeHandleValue) == sizeof(uint64_t),
               "CNA_NativeHandleValue must have a fixed-width representation");
_Static_assert(sizeof(CNA_DisplayMode) == 24U,
               "CNA_DisplayMode layout must remain stable");
_Static_assert(sizeof(CNA_GraphicsAdapterInfo) == 48U,
               "CNA_GraphicsAdapterInfo layout must remain stable");
_Static_assert(sizeof(CNA_GraphicsFormatSelection) == 24U,
               "CNA_GraphicsFormatSelection layout must remain stable");
_Static_assert(sizeof(CNA_PresentationParameters) == 44U,
               "CNA_PresentationParameters layout must remain stable");
_Static_assert(sizeof(CNA_ContainmentType) == sizeof(uint32_t) &&
                   CNA_CONTAINMENT_DISJOINT == UINT32_C(0) &&
                   CNA_CONTAINMENT_CONTAINS == UINT32_C(1) &&
                   CNA_CONTAINMENT_INTERSECTS == UINT32_C(2),
               "CNA containment identities must remain stable");
_Static_assert(sizeof(CNA_PlaneIntersectionType) == sizeof(uint32_t) &&
                   CNA_PLANE_INTERSECTION_FRONT == UINT32_C(0) &&
                   CNA_PLANE_INTERSECTION_BACK == UINT32_C(1) &&
                   CNA_PLANE_INTERSECTION_INTERSECTING == UINT32_C(2),
               "CNA plane-intersection identities must remain stable");
_Static_assert(CNA_CURVE_CONTINUITY_SMOOTH == UINT32_C(0) &&
                   CNA_CURVE_CONTINUITY_STEP == UINT32_C(1) &&
                   CNA_CURVE_LOOP_CONSTANT == UINT32_C(0) &&
                   CNA_CURVE_LOOP_CYCLE == UINT32_C(1) &&
                   CNA_CURVE_LOOP_CYCLE_OFFSET == UINT32_C(2) &&
                   CNA_CURVE_LOOP_OSCILLATE == UINT32_C(3) &&
                   CNA_CURVE_LOOP_LINEAR == UINT32_C(4) &&
                   CNA_CURVE_TANGENT_FLAT == UINT32_C(0) &&
                   CNA_CURVE_TANGENT_LINEAR == UINT32_C(1) &&
                   CNA_CURVE_TANGENT_SMOOTH == UINT32_C(2),
               "CNA curve identities must remain stable");
_Static_assert(sizeof(CNA_PackedVectorFormat) == sizeof(uint32_t) &&
                   CNA_PACKED_VECTOR_FORMAT_ALPHA8 == UINT32_C(0) &&
                   CNA_PACKED_VECTOR_FORMAT_BGR565 == UINT32_C(1) &&
                   CNA_PACKED_VECTOR_FORMAT_BGRA4444 == UINT32_C(2) &&
                   CNA_PACKED_VECTOR_FORMAT_BGRA5551 == UINT32_C(3) &&
                   CNA_PACKED_VECTOR_FORMAT_BYTE4 == UINT32_C(4) &&
                   CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE == UINT32_C(5) &&
                   CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2 == UINT32_C(6) &&
                   CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4 == UINT32_C(7) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2 == UINT32_C(8) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4 == UINT32_C(9) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2 == UINT32_C(10) &&
                   CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4 == UINT32_C(11) &&
                   CNA_PACKED_VECTOR_FORMAT_RG32 == UINT32_C(12) &&
                   CNA_PACKED_VECTOR_FORMAT_RGBA1010102 == UINT32_C(13) &&
                   CNA_PACKED_VECTOR_FORMAT_RGBA64 == UINT32_C(14) &&
                   CNA_PACKED_VECTOR_FORMAT_SHORT2 == UINT32_C(15) &&
                   CNA_PACKED_VECTOR_FORMAT_SHORT4 == UINT32_C(16),
               "CNA packed-vector identities must remain stable");
_Static_assert(sizeof(CNA_Point) == 8U && sizeof(CNA_Vector4) == 16U &&
                   sizeof(CNA_Quaternion) == 16U && _Alignof(CNA_Point) == 4U &&
                   _Alignof(CNA_Vector4) == 4U && _Alignof(CNA_Quaternion) == 4U,
               "CNA foundational value layouts must remain stable");
_Static_assert(offsetof(CNA_Point, x) == 0U && offsetof(CNA_Point, y) == 4U &&
                   offsetof(CNA_Vector4, x) == 0U && offsetof(CNA_Vector4, y) == 4U &&
                   offsetof(CNA_Vector4, z) == 8U && offsetof(CNA_Vector4, w) == 12U &&
                   offsetof(CNA_Quaternion, x) == 0U && offsetof(CNA_Quaternion, y) == 4U &&
                   offsetof(CNA_Quaternion, z) == 8U && offsetof(CNA_Quaternion, w) == 12U,
               "CNA foundational value fields must remain stable");
_Static_assert(sizeof(CNA_Matrix) == 64U && sizeof(CNA_Plane) == 16U &&
                   sizeof(CNA_Ray) == 24U,
               "CNA matrix/intersection value layouts must remain stable");
_Static_assert(offsetof(CNA_Matrix, m11) == 0U && offsetof(CNA_Matrix, m12) == 4U &&
                   offsetof(CNA_Matrix, m13) == 8U && offsetof(CNA_Matrix, m14) == 12U &&
                   offsetof(CNA_Matrix, m21) == 16U && offsetof(CNA_Matrix, m22) == 20U &&
                   offsetof(CNA_Matrix, m23) == 24U && offsetof(CNA_Matrix, m24) == 28U &&
                   offsetof(CNA_Matrix, m31) == 32U && offsetof(CNA_Matrix, m32) == 36U &&
                   offsetof(CNA_Matrix, m33) == 40U && offsetof(CNA_Matrix, m34) == 44U &&
                   offsetof(CNA_Matrix, m41) == 48U && offsetof(CNA_Matrix, m42) == 52U &&
                   offsetof(CNA_Matrix, m43) == 56U && offsetof(CNA_Matrix, m44) == 60U,
               "CNA matrix fields must remain stable");
_Static_assert(offsetof(CNA_Plane, normal) == 0U && offsetof(CNA_Plane, d) == 12U &&
                   offsetof(CNA_Ray, position) == 0U && offsetof(CNA_Ray, direction) == 12U,
               "CNA matrix/intersection value fields must remain stable");
_Static_assert(sizeof(CNA_BoundingBox) == 24U && sizeof(CNA_BoundingSphere) == 16U &&
                   sizeof(CNA_BoundingFrustum) == 64U,
               "CNA bounding-volume layouts must remain stable");
_Static_assert(offsetof(CNA_BoundingBox, min) == 0U &&
                   offsetof(CNA_BoundingBox, max) == 12U &&
                   offsetof(CNA_BoundingSphere, center) == 0U &&
                   offsetof(CNA_BoundingSphere, radius) == 12U &&
                   offsetof(CNA_BoundingFrustum, matrix) == 0U,
               "CNA bounding-volume fields must remain stable");
_Static_assert(sizeof(CNA_CurveKey) == 20U && _Alignof(CNA_CurveKey) == 4U,
               "CNA curve-key layout must remain stable");
_Static_assert(sizeof(CNA_CurveKeyCollectionHandle) == 8U,
               "CNA curve-key collection handle must remain stable");
_Static_assert(sizeof(CNA_CurveHandle) == 8U,
               "CNA curve handle must remain stable");
_Static_assert(offsetof(CNA_CurveKey, position) == 0U &&
                   offsetof(CNA_CurveKey, value) == 4U &&
                   offsetof(CNA_CurveKey, tangent_in) == 8U &&
                   offsetof(CNA_CurveKey, tangent_out) == 12U &&
                   offsetof(CNA_CurveKey, continuity) == 16U,
               "CNA curve-key fields must remain stable");
_Static_assert(sizeof(CNA_PackedAlpha8) == 1U && sizeof(CNA_PackedBgr565) == 2U &&
                   sizeof(CNA_PackedBgra4444) == 2U && sizeof(CNA_PackedBgra5551) == 2U &&
                   sizeof(CNA_PackedByte4) == 4U && sizeof(CNA_PackedHalfSingle) == 2U &&
                   sizeof(CNA_PackedHalfVector2) == 4U && sizeof(CNA_PackedHalfVector4) == 8U &&
                   sizeof(CNA_PackedNormalizedByte2) == 2U &&
                   sizeof(CNA_PackedNormalizedByte4) == 4U &&
                   sizeof(CNA_PackedNormalizedShort2) == 4U &&
                   sizeof(CNA_PackedNormalizedShort4) == 8U &&
                   sizeof(CNA_PackedRg32) == 4U && sizeof(CNA_PackedRgba1010102) == 4U &&
                   sizeof(CNA_PackedRgba64) == 8U && sizeof(CNA_PackedShort2) == 4U &&
                   sizeof(CNA_PackedShort4) == 8U,
               "CNA packed-value layouts must remain stable");
_Static_assert(sizeof(CNA_BufferUsage) == sizeof(uint32_t) &&
                   CNA_BUFFER_USAGE_NONE == UINT32_C(0) &&
                   CNA_BUFFER_USAGE_WRITE_ONLY == UINT32_C(1),
               "CNA buffer-usage identities must remain stable");
_Static_assert(sizeof(CNA_IndexElementSize) == sizeof(uint32_t) &&
                   CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS == UINT32_C(0) &&
                   CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS == UINT32_C(1),
               "CNA index-element identities must remain stable");
_Static_assert(sizeof(CNA_PrimitiveType) == sizeof(uint32_t) &&
                   CNA_PRIMITIVE_TRIANGLE_LIST == UINT32_C(0) &&
                   CNA_PRIMITIVE_TRIANGLE_STRIP == UINT32_C(1) &&
                   CNA_PRIMITIVE_LINE_LIST == UINT32_C(2) &&
                   CNA_PRIMITIVE_LINE_STRIP == UINT32_C(3) &&
                   CNA_PRIMITIVE_POINT_LIST_EXT == UINT32_C(4),
               "CNA primitive identities must remain stable");
_Static_assert(sizeof(CNA_SetDataOptions) == sizeof(uint32_t) &&
                   CNA_SET_DATA_NONE == UINT32_C(0) &&
                   CNA_SET_DATA_DISCARD == UINT32_C(1) &&
                   CNA_SET_DATA_NO_OVERWRITE == UINT32_C(2),
               "CNA SetData identities must remain stable");
_Static_assert(sizeof(CNA_VertexElementFormat) == sizeof(uint32_t) &&
                   CNA_VERTEX_ELEMENT_FORMAT_SINGLE == UINT32_C(0) &&
                   CNA_VERTEX_ELEMENT_FORMAT_VECTOR2 == UINT32_C(1) &&
                   CNA_VERTEX_ELEMENT_FORMAT_VECTOR3 == UINT32_C(2) &&
                   CNA_VERTEX_ELEMENT_FORMAT_VECTOR4 == UINT32_C(3) &&
                   CNA_VERTEX_ELEMENT_FORMAT_COLOR == UINT32_C(4) &&
                   CNA_VERTEX_ELEMENT_FORMAT_BYTE4 == UINT32_C(5) &&
                   CNA_VERTEX_ELEMENT_FORMAT_SHORT2 == UINT32_C(6) &&
                   CNA_VERTEX_ELEMENT_FORMAT_SHORT4 == UINT32_C(7) &&
                   CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT2 == UINT32_C(8) &&
                   CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT4 == UINT32_C(9) &&
                   CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR2 == UINT32_C(10) &&
                   CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4 == UINT32_C(11),
               "CNA vertex-element format identities must remain stable");
_Static_assert(sizeof(CNA_VertexElementUsage) == sizeof(uint32_t) &&
                   CNA_VERTEX_ELEMENT_USAGE_POSITION == UINT32_C(0) &&
                   CNA_VERTEX_ELEMENT_USAGE_COLOR == UINT32_C(1) &&
                   CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE == UINT32_C(2) &&
                   CNA_VERTEX_ELEMENT_USAGE_NORMAL == UINT32_C(3) &&
                   CNA_VERTEX_ELEMENT_USAGE_BINORMAL == UINT32_C(4) &&
                   CNA_VERTEX_ELEMENT_USAGE_TANGENT == UINT32_C(5) &&
                   CNA_VERTEX_ELEMENT_USAGE_BLEND_INDICES == UINT32_C(6) &&
                   CNA_VERTEX_ELEMENT_USAGE_BLEND_WEIGHT == UINT32_C(7) &&
                   CNA_VERTEX_ELEMENT_USAGE_DEPTH == UINT32_C(8) &&
                   CNA_VERTEX_ELEMENT_USAGE_FOG == UINT32_C(9) &&
                   CNA_VERTEX_ELEMENT_USAGE_POINT_SIZE == UINT32_C(10) &&
                   CNA_VERTEX_ELEMENT_USAGE_SAMPLE == UINT32_C(11) &&
                   CNA_VERTEX_ELEMENT_USAGE_TESSELLATE_FACTOR == UINT32_C(12),
               "CNA vertex semantic identities must remain stable");
_Static_assert(sizeof(CNA_VertexElement) == 16U && _Alignof(CNA_VertexElement) == 4U,
               "CNA_VertexElement layout must remain stable");
_Static_assert(offsetof(CNA_VertexElement, offset) == 0U &&
                   offsetof(CNA_VertexElement, format) == 4U &&
                   offsetof(CNA_VertexElement, usage) == 8U &&
                   offsetof(CNA_VertexElement, usage_index) == 12U,
               "CNA_VertexElement fields must remain stable");
_Static_assert(sizeof(CNA_VertexType) == sizeof(uint32_t) &&
                   CNA_VERTEX_TYPE_POSITION_COLOR == UINT32_C(0) &&
                   CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE == UINT32_C(1) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE == UINT32_C(2) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED == UINT32_C(3) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE == UINT32_C(4) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED == UINT32_C(5) &&
                   CNA_VERTEX_TYPE_POSITION_TEXTURE == UINT32_C(6),
               "CNA built-in vertex identities must remain stable");
_Static_assert(sizeof(CNA_VertexPositionColor) == 16U &&
                   sizeof(CNA_VertexPositionColorTexture) == 24U &&
                   sizeof(CNA_VertexPositionNormalTangentTexture) == 48U &&
                   sizeof(CNA_VertexPositionNormalTangentTextureSkinned) == 68U &&
                   sizeof(CNA_VertexPositionNormalTexture) == 32U &&
                   sizeof(CNA_VertexPositionNormalTextureSkinned) == 52U &&
                   sizeof(CNA_VertexPositionTexture) == 20U &&
                   sizeof(CNA_VertexValue) == 68U && _Alignof(CNA_VertexValue) == 4U,
               "CNA built-in vertex layouts must remain stable");
_Static_assert(offsetof(CNA_VertexPositionColor, color) == 12U &&
                   offsetof(CNA_VertexPositionColorTexture, texture_coordinate) == 16U &&
                   offsetof(CNA_VertexPositionNormalTangentTexture, tangent) == 24U &&
                   offsetof(CNA_VertexPositionNormalTangentTexture, texture_coordinate) == 40U &&
                   offsetof(CNA_VertexPositionNormalTangentTextureSkinned, blend_weight) == 48U &&
                   offsetof(CNA_VertexPositionNormalTangentTextureSkinned, blend_indices) == 64U &&
                   offsetof(CNA_VertexPositionNormalTexture, texture_coordinate) == 24U &&
                   offsetof(CNA_VertexPositionNormalTextureSkinned, blend_weight) == 32U &&
                   offsetof(CNA_VertexPositionNormalTextureSkinned, blend_indices) == 48U &&
                   offsetof(CNA_VertexPositionTexture, texture_coordinate) == 12U,
               "CNA built-in vertex fields must remain stable");
_Static_assert(sizeof(CNA_VertexDeclarationHandle) == 8U &&
                   sizeof(CNA_VertexBufferHandle) == 8U &&
                   sizeof(CNA_VertexBufferEventRegistrationHandle) == 8U,
               "CNA vertex-resource handles must remain stable");
_Static_assert(sizeof(CNA_VertexBufferCreateInfo) == 32U &&
                   _Alignof(CNA_VertexBufferCreateInfo) == 8U &&
                   offsetof(CNA_VertexBufferCreateInfo, vertex_declaration) == 8U &&
                   offsetof(CNA_VertexBufferCreateInfo, vertex_count) == 16U &&
                   offsetof(CNA_VertexBufferCreateInfo, dynamic) == 24U,
               "CNA_VertexBufferCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_VertexBufferInfo) == 32U &&
                   _Alignof(CNA_VertexBufferInfo) == 8U &&
                   offsetof(CNA_VertexBufferInfo, vertex_stride) == 20U &&
                   offsetof(CNA_VertexBufferInfo, vertex_element_count) == 24U,
               "CNA_VertexBufferInfo layout must remain stable");
_Static_assert(sizeof(CNA_VertexBufferTransfer) == 32U &&
                   _Alignof(CNA_VertexBufferTransfer) == 8U &&
                   offsetof(CNA_VertexBufferTransfer, start_index) == 16U &&
                   offsetof(CNA_VertexBufferTransfer, element_count) == 24U,
               "CNA_VertexBufferTransfer layout must remain stable");
_Static_assert(sizeof(CNA_IndexBufferHandle) == 8U &&
                   sizeof(CNA_IndexBufferEventRegistrationHandle) == 8U,
               "CNA index-buffer handles must remain stable");
_Static_assert(sizeof(CNA_IndexBufferCreateInfo) == 24U &&
                   _Alignof(CNA_IndexBufferCreateInfo) == 4U &&
                   offsetof(CNA_IndexBufferCreateInfo, index_count) == 8U &&
                   offsetof(CNA_IndexBufferCreateInfo, index_element_size) == 12U &&
                   offsetof(CNA_IndexBufferCreateInfo, dynamic) == 20U,
               "CNA_IndexBufferCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_IndexBufferInfo) == 24U &&
                   _Alignof(CNA_IndexBufferInfo) == 4U &&
                   offsetof(CNA_IndexBufferInfo, index_count) == 8U &&
                   offsetof(CNA_IndexBufferInfo, dynamic) == 20U,
               "CNA_IndexBufferInfo layout must remain stable");
_Static_assert(sizeof(CNA_IndexBufferTransfer) == 32U &&
                   _Alignof(CNA_IndexBufferTransfer) == 8U &&
                   offsetof(CNA_IndexBufferTransfer, start_index) == 16U &&
                   offsetof(CNA_IndexBufferTransfer, element_count) == 24U,
               "CNA_IndexBufferTransfer layout must remain stable");
_Static_assert(sizeof(CNA_VertexBufferBinding) == 16U &&
                   _Alignof(CNA_VertexBufferBinding) == 8U,
               "CNA_VertexBufferBinding layout must remain stable");
_Static_assert(offsetof(CNA_VertexBufferBinding, vertex_buffer) == 0U &&
                   offsetof(CNA_VertexBufferBinding, vertex_offset) == 8U &&
                   offsetof(CNA_VertexBufferBinding, instance_frequency) == 12U,
               "CNA_VertexBufferBinding fields must remain stable");
_Static_assert(sizeof(CNA_Viewport) == 24U && _Alignof(CNA_Viewport) == 4U &&
                   offsetof(CNA_Viewport, x) == 0U &&
                   offsetof(CNA_Viewport, y) == 4U &&
                   offsetof(CNA_Viewport, width) == 8U &&
                   offsetof(CNA_Viewport, height) == 12U &&
                   offsetof(CNA_Viewport, min_depth) == 16U &&
                   offsetof(CNA_Viewport, max_depth) == 20U,
               "CNA_Viewport layout must remain stable");
_Static_assert(sizeof(CNA_ClearOptions) == sizeof(uint32_t) &&
                   CNA_CLEAR_OPTION_TARGET == UINT32_C(1) &&
                   CNA_CLEAR_OPTION_DEPTH_BUFFER == UINT32_C(2) &&
                   CNA_CLEAR_OPTION_STENCIL == UINT32_C(4),
               "CNA clear-option identities must remain stable");
_Static_assert(sizeof(CNA_GraphicsDeviceStatus) == sizeof(uint32_t) &&
                   CNA_GRAPHICS_DEVICE_STATUS_NORMAL == UINT32_C(0) &&
                   CNA_GRAPHICS_DEVICE_STATUS_LOST == UINT32_C(1) &&
                   CNA_GRAPHICS_DEVICE_STATUS_NOT_RESET == UINT32_C(2),
               "CNA graphics-device status identities must remain stable");
_Static_assert(sizeof(CNA_Unsupported3DGraphicsCallBehavior) == sizeof(uint32_t) &&
                   CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW == UINT32_C(0) &&
                   CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB == UINT32_C(1),
               "CNA unsupported-3D-call policy identities must remain stable");
_Static_assert(sizeof(CNA_SpriteEffects) == sizeof(uint32_t) &&
                   (CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY |
                    CNA_SPRITE_EFFECT_FLIP_VERTICALLY) == UINT32_C(3) &&
                   ((CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY |
                     CNA_SPRITE_EFFECT_FLIP_VERTICALLY) &
                    CNA_SPRITE_EFFECT_FLIP_VERTICALLY) == CNA_SPRITE_EFFECT_FLIP_VERTICALLY,
               "CNA sprite-effect bit operations must remain stable");
_Static_assert(sizeof(CNA_GraphicsDeviceEventRegistrationHandle) == 8U &&
                   sizeof(CNA_GraphicsDeviceEvent) == sizeof(uint32_t) &&
                   CNA_GRAPHICS_DEVICE_EVENT_DISPOSING == UINT32_C(0) &&
                   CNA_GRAPHICS_DEVICE_EVENT_DEVICE_LOST == UINT32_C(1) &&
                   CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESET == UINT32_C(2) &&
                   CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESETTING == UINT32_C(3),
               "CNA graphics-device event identities must remain stable");
_Static_assert(sizeof(CNA_ResourceCreatedEventInfo) == 16U &&
                   _Alignof(CNA_ResourceCreatedEventInfo) == 4U &&
                   offsetof(CNA_ResourceCreatedEventInfo, has_resource) == 8U &&
                   offsetof(CNA_ResourceCreatedEventInfo, reserved) == 9U,
               "CNA_ResourceCreatedEventInfo layout must remain stable");
_Static_assert(sizeof(CNA_ResourceDestroyedEventInfo) == 32U &&
                   _Alignof(CNA_ResourceDestroyedEventInfo) == 8U &&
                   offsetof(CNA_ResourceDestroyedEventInfo, has_tag) == 8U &&
                   offsetof(CNA_ResourceDestroyedEventInfo, name) == 16U,
               "CNA_ResourceDestroyedEventInfo layout must remain stable");
_Static_assert(CNA_TEXTURE_COLLECTION_MAX_TEXTURES == UINT32_C(16) &&
                   sizeof(CNA_TextureSlotInfo) == 24U &&
                   _Alignof(CNA_TextureSlotInfo) == 8U &&
                   offsetof(CNA_TextureSlotInfo, bound) == 8U &&
                   offsetof(CNA_TextureSlotInfo, reserved) == 9U &&
                   offsetof(CNA_TextureSlotInfo, texture) == 16U,
               "CNA_TextureSlotInfo layout must remain stable");
_Static_assert(sizeof(CNA_BackBufferReadback) == 48U &&
                   _Alignof(CNA_BackBufferReadback) == 8U &&
                   offsetof(CNA_BackBufferReadback, has_source_rectangle) == 8U &&
                   offsetof(CNA_BackBufferReadback, source_rectangle) == 12U &&
                   offsetof(CNA_BackBufferReadback, start_index) == 32U &&
                   offsetof(CNA_BackBufferReadback, element_count) == 40U,
               "CNA_BackBufferReadback layout must remain stable");
_Static_assert(sizeof(CNA_UserVertexSource) == sizeof(uint32_t) &&
                   CNA_USER_VERTEX_SOURCE_RAW_STREAM == UINT32_C(0) &&
                   CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE == UINT32_C(4),
               "CNA user-vertex-source identities must remain stable");
_Static_assert(sizeof(CNA_UserPrimitives) == 48U &&
                   _Alignof(CNA_UserPrimitives) == 8U &&
                   offsetof(CNA_UserPrimitives, vertex_data) == 16U &&
                   offsetof(CNA_UserPrimitives, vertex_declaration) == 24U &&
                   offsetof(CNA_UserPrimitives, vertex_offset) == 32U,
               "CNA_UserPrimitives layout must remain stable");
_Static_assert(sizeof(CNA_UserIndices) == 24U &&
                   _Alignof(CNA_UserIndices) == 8U &&
                   offsetof(CNA_UserIndices, index_element_size) == 8U &&
                   offsetof(CNA_UserIndices, index_data) == 16U,
               "CNA_UserIndices layout must remain stable");
_Static_assert(sizeof(CNA_OcclusionQueryHandle) == 8U,
               "CNA_OcclusionQueryHandle must remain stable");
_Static_assert(sizeof(CNA_SpriteTextCommand) == 72U &&
                   _Alignof(CNA_SpriteTextCommand) == 8U &&
                   offsetof(CNA_SpriteTextCommand, sprite_font) == 8U &&
                   offsetof(CNA_SpriteTextCommand, text) == 16U &&
                   offsetof(CNA_SpriteTextCommand, position) == 32U,
               "CNA_SpriteTextCommand layout must remain stable");
_Static_assert(sizeof(CNA_SpriteMeshEXT) == 64U &&
                   _Alignof(CNA_SpriteMeshEXT) == 8U &&
                   offsetof(CNA_SpriteMeshEXT, effect) == 8U &&
                   offsetof(CNA_SpriteMeshEXT, positions) == 16U &&
                   offsetof(CNA_SpriteMeshEXT, vertex_count) == 48U,
               "CNA_SpriteMeshEXT layout must remain stable");
_Static_assert(sizeof(CNA_AsciiPostProcessEffectHandle) == 8U &&
                   sizeof(CNA_AsciiQuantizeMode) == sizeof(uint32_t) &&
                   sizeof(CNA_CRTMaskType) == sizeof(uint32_t) &&
                   sizeof(CNA_DitherMode) == sizeof(uint32_t) &&
                   sizeof(CNA_RenderQuality) == sizeof(uint32_t) &&
                   sizeof(CNA_ShadowQuality) == sizeof(uint32_t) &&
                   sizeof(CNA_TonemappingMode) == sizeof(uint32_t) &&
                   sizeof(CNA_DepthEffectMode) == sizeof(uint32_t) &&
                   CNA_CRT_MASK_TYPE_SHADOW_MASK == UINT32_C(2) &&
                   CNA_DEPTH_EFFECT_MODE_PALETTE_16 == UINT32_C(6) &&
                   CNA_SHADOW_QUALITY_ULTRA == UINT32_C(4),
               "CNA graphics-extension identities must remain stable");
_Static_assert(sizeof(CNA_PbrMaterial) == 72U && _Alignof(CNA_PbrMaterial) == 8U &&
                   offsetof(CNA_PbrMaterial, albedo_color) == 40U &&
                   offsetof(CNA_PbrMaterial, emissive_color) == 44U &&
                   offsetof(CNA_PbrMaterial, metallic_factor) == 48U &&
                   offsetof(CNA_PbrMaterial, alpha_blend_enabled) == 68U &&
                   offsetof(CNA_PbrMaterial, reserved) == 69U,
               "CNA_PbrMaterial layout must remain stable");
_Static_assert(sizeof(CNA_PbrMaterialEXT) == 360U && _Alignof(CNA_PbrMaterialEXT) == 8U &&
                   offsetof(CNA_PbrMaterialEXT, albedo_color) == 64U &&
                   offsetof(CNA_PbrMaterialEXT, emissive_factor) == 68U &&
                   offsetof(CNA_PbrMaterialEXT, specular_color_factor) == 80U &&
                   offsetof(CNA_PbrMaterialEXT, metallic_factor) == 92U &&
                   offsetof(CNA_PbrMaterialEXT, alpha_mode) == 120U &&
                   offsetof(CNA_PbrMaterialEXT, double_sided) == 124U &&
                   offsetof(CNA_PbrMaterialEXT, reserved) == 129U &&
                   offsetof(CNA_PbrMaterialEXT, texture_coordinate_sets) == 132U &&
                   offsetof(CNA_PbrMaterialEXT, texture_transforms) == 160U &&
                   CNA_PBR_MATERIAL_EXT_VERSION == UINT32_C(1),
               "CNA_PbrMaterialEXT layout must remain stable");
_Static_assert(sizeof(CNA_RenderPipelineSettings) == 28U &&
                   _Alignof(CNA_RenderPipelineSettings) == 4U &&
                   offsetof(CNA_RenderPipelineSettings, tonemapping_mode) == 12U &&
                   offsetof(CNA_RenderPipelineSettings, render_quality) == 16U &&
                   offsetof(CNA_RenderPipelineSettings, hdr_enabled) == 24U &&
                   offsetof(CNA_RenderPipelineSettings, shadows_enabled) == 27U,
               "CNA_RenderPipelineSettings layout must remain stable");
_Static_assert(sizeof(CNA_StorageDeviceHandle) == 8U &&
                   sizeof(CNA_StorageContainerHandle) == 8U &&
                   sizeof(CNA_StorageStreamHandle) == 8U &&
                   sizeof(CNA_FileMode) == sizeof(uint32_t) &&
                   sizeof(CNA_FileAccess) == sizeof(uint32_t) &&
                   sizeof(CNA_FileShare) == sizeof(uint32_t) &&
                   sizeof(CNA_SeekOrigin) == sizeof(uint32_t) &&
                   CNA_FILE_MODE_CREATE_NEW == UINT32_C(1) &&
                   CNA_FILE_MODE_APPEND == UINT32_C(6) &&
                   CNA_FILE_ACCESS_READ_WRITE == UINT32_C(3) &&
                   CNA_FILE_SHARE_NONE == UINT32_C(0) &&
                   CNA_FILE_SHARE_INHERITABLE == UINT32_C(16) &&
                   CNA_SEEK_ORIGIN_END == UINT32_C(2),
               "CNA storage identities must remain stable");
_Static_assert(sizeof(CNA_ContentManifestEntryInfo) == 32U &&
                   _Alignof(CNA_ContentManifestEntryInfo) == 8U &&
                   offsetof(CNA_ContentManifestEntryInfo, has_xnb) == 8U &&
                   offsetof(CNA_ContentManifestEntryInfo, has_cnj) == 9U &&
                   offsetof(CNA_ContentManifestEntryInfo, reserved) == 10U &&
                   offsetof(CNA_ContentManifestEntryInfo, native_extension_count) == 16U &&
                   offsetof(CNA_ContentManifestEntryInfo, xnb_reader_name_count) == 24U,
               "CNA_ContentManifestEntryInfo layout must remain stable");
_Static_assert(sizeof(CNA_ContentReaderUsageInfo) == 24U &&
                   _Alignof(CNA_ContentReaderUsageInfo) == 8U &&
                   offsetof(CNA_ContentReaderUsageInfo, is_registered) == 8U &&
                   offsetof(CNA_ContentReaderUsageInfo, reserved) == 9U &&
                   offsetof(CNA_ContentReaderUsageInfo, file_count) == 16U,
               "CNA_ContentReaderUsageInfo layout must remain stable");
_Static_assert(sizeof(CNA_ContentReaderHandle) == 8U &&
                   sizeof(CNA_ContentTypeReaderHandle) == 8U &&
                   sizeof(CNA_UnsupportedContentReaderReason) == sizeof(uint32_t) &&
                   CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE ==
                       UINT32_C(0),
               "CNA content reader identities must remain stable");
_Static_assert(sizeof(CNA_ContentReaderCreateInfo) == 48U &&
                   _Alignof(CNA_ContentReaderCreateInfo) == 8U &&
                   offsetof(CNA_ContentReaderCreateInfo, content_manager) == 8U &&
                   offsetof(CNA_ContentReaderCreateInfo, stream) == 16U &&
                   offsetof(CNA_ContentReaderCreateInfo, asset_name) == 24U &&
                   offsetof(CNA_ContentReaderCreateInfo, version) == 40U &&
                   offsetof(CNA_ContentReaderCreateInfo, platform) == 44U &&
                   offsetof(CNA_ContentReaderCreateInfo, reserved) == 45U,
               "CNA_ContentReaderCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_NetworkSessionEndReason) == sizeof(uint32_t) &&
                   sizeof(CNA_NetworkSessionJoinError) == sizeof(uint32_t) &&
                   sizeof(CNA_NetworkSessionState) == sizeof(uint32_t) &&
                   sizeof(CNA_NetworkSessionType) == sizeof(uint32_t) &&
                   sizeof(CNA_SendDataOptions) == sizeof(uint32_t) &&
                   sizeof(CNA_NetworkSessionPropertiesHandle) == 8U &&
                   sizeof(CNA_NetworkSessionPropertyEnumeratorHandle) == 8U &&
                   sizeof(CNA_PacketWriterHandle) == 8U &&
                   sizeof(CNA_PacketReaderHandle) == 8U &&
                   CNA_NETWORK_SESSION_END_REASON_DISCONNECTED == UINT32_C(3) &&
                   CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_FULL == UINT32_C(2) &&
                   CNA_NETWORK_SESSION_STATE_ENDED == UINT32_C(2) &&
                   CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS == UINT32_C(4) &&
                   CNA_SEND_DATA_OPTIONS_CHAT == UINT32_C(4),
               "CNA network identities must remain stable");
_Static_assert(sizeof(CNA_QualityOfService) == 40U && _Alignof(CNA_QualityOfService) == 8U &&
                   offsetof(CNA_QualityOfService, is_available) == 8U &&
                   offsetof(CNA_QualityOfService, reserved) == 9U &&
                   offsetof(CNA_QualityOfService, average_roundtrip_ticks) == 16U &&
                   offsetof(CNA_QualityOfService, minimum_roundtrip_ticks) == 24U &&
                   offsetof(CNA_QualityOfService, bytes_per_second_downstream) == 32U &&
                   offsetof(CNA_QualityOfService, bytes_per_second_upstream) == 36U,
               "CNA_QualityOfService layout must remain stable");
_Static_assert(sizeof(CNA_OptionalInt32) == 8U && _Alignof(CNA_OptionalInt32) == 4U &&
                   offsetof(CNA_OptionalInt32, has_value) == 0U &&
                   offsetof(CNA_OptionalInt32, reserved) == 1U &&
                   offsetof(CNA_OptionalInt32, value) == 4U,
               "CNA_OptionalInt32 layout must remain stable");
_Static_assert(sizeof(CNA_NetworkGamerHandle) == 8U && sizeof(CNA_NetworkMachineHandle) == 8U &&
                   sizeof(CNA_GameEndedEventInfo) == 8U &&
                   sizeof(CNA_GameStartedEventInfo) == 8U &&
                   sizeof(CNA_GamerJoinedEventInfo) == 16U &&
                   _Alignof(CNA_GamerJoinedEventInfo) == 8U &&
                   offsetof(CNA_GamerJoinedEventInfo, gamer) == 8U &&
                   sizeof(CNA_GamerLeftEventInfo) == 16U &&
                   offsetof(CNA_GamerLeftEventInfo, gamer) == 8U &&
                   sizeof(CNA_HostChangedEventInfo) == 24U &&
                   offsetof(CNA_HostChangedEventInfo, old_host) == 8U &&
                   offsetof(CNA_HostChangedEventInfo, new_host) == 16U,
               "CNA network gamer event layouts must remain stable");
_Static_assert(sizeof(CNA_NetworkSessionEndedEventInfo) == 16U &&
                   _Alignof(CNA_NetworkSessionEndedEventInfo) == 4U &&
                   offsetof(CNA_NetworkSessionEndedEventInfo, end_reason) == 8U &&
                   offsetof(CNA_NetworkSessionEndedEventInfo, reserved) == 12U &&
                   sizeof(CNA_WriteLeaderboardsEventInfo) == 24U &&
                   _Alignof(CNA_WriteLeaderboardsEventInfo) == 8U &&
                   offsetof(CNA_WriteLeaderboardsEventInfo, gamer) == 8U &&
                   offsetof(CNA_WriteLeaderboardsEventInfo, is_leaving) == 16U &&
                   offsetof(CNA_WriteLeaderboardsEventInfo, reserved) == 17U,
               "CNA network session event layouts must remain stable");
_Static_assert(sizeof(CNA_AvailableNetworkSessionHandle) == 8U &&
                   sizeof(CNA_AvailableNetworkSessionCollectionHandle) == 8U &&
                   sizeof(CNA_AvailableNetworkSessionCreateInfo) == 72U &&
                   _Alignof(CNA_AvailableNetworkSessionCreateInfo) == 8U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, current_gamer_count) == 8U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, open_private_gamer_slots) ==
                       12U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, open_public_gamer_slots) ==
                       16U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, session_type) == 20U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, host_port) == 24U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, reserved) == 26U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, host_gamertag) == 32U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, host_address) == 48U &&
                   offsetof(CNA_AvailableNetworkSessionCreateInfo, session_properties) == 64U,
               "CNA_AvailableNetworkSessionCreateInfo layout must remain stable");
_Static_assert(sizeof(CNA_NetworkSessionHandle) == 8U &&
                   sizeof(CNA_SignedInGamerHandle) == 8U &&
                   sizeof(CNA_NetworkEventType) == sizeof(uint32_t) &&
                   CNA_NETWORK_SESSION_MAX_SUPPORTED_GAMERS == INT32_C(31) &&
                   CNA_NETWORK_SESSION_MAX_PREVIOUS_GAMERS == INT32_C(100) &&
                   CNA_NETWORK_EVENT_TYPE_PACKET_SEND == UINT32_C(0) &&
                   CNA_NETWORK_EVENT_TYPE_STATE_CHANGE == UINT32_C(4) &&
                   CNA_NETWORK_SESSION_ROSTER_ALL == UINT32_C(0) &&
                   CNA_NETWORK_SESSION_ROSTER_PREVIOUS == UINT32_C(3),
               "CNA network session identities must remain stable");
_Static_assert(sizeof(CNA_NetworkEventInfo) == 56U && _Alignof(CNA_NetworkEventInfo) == 8U &&
                   offsetof(CNA_NetworkEventInfo, type) == 8U &&
                   offsetof(CNA_NetworkEventInfo, reliable) == 12U &&
                   offsetof(CNA_NetworkEventInfo, state) == 16U &&
                   offsetof(CNA_NetworkEventInfo, reason) == 20U &&
                   offsetof(CNA_NetworkEventInfo, gamer) == 24U &&
                   offsetof(CNA_NetworkEventInfo, sender) == 32U &&
                   offsetof(CNA_NetworkEventInfo, packet) == 40U &&
                   offsetof(CNA_NetworkEventInfo, packet_byte_count) == 48U,
               "CNA_NetworkEventInfo layout must remain stable");
_Static_assert(sizeof(CNA_NetworkSessionEventRegistrationHandle) == 8U &&
                   sizeof(CNA_InviteAcceptedEventInfo) == 24U &&
                   _Alignof(CNA_InviteAcceptedEventInfo) == 8U &&
                   offsetof(CNA_InviteAcceptedEventInfo, gamer) == 8U &&
                   offsetof(CNA_InviteAcceptedEventInfo, is_current_session) == 16U &&
                   offsetof(CNA_InviteAcceptedEventInfo, reserved) == 17U,
               "CNA_InviteAcceptedEventInfo layout must remain stable");

_Static_assert(sizeof(CNA_LogLevel) == sizeof(uint32_t) &&
                   sizeof(CNA_LogCategory) == sizeof(uint32_t) &&
                   sizeof(CNA_Platform) == sizeof(uint32_t) &&
                   sizeof(CNA_DesktopOS) == sizeof(uint32_t) &&
                   sizeof(CNA_GraphicsBackendCategory) == sizeof(uint32_t) &&
                   sizeof(CNA_GraphicsBackendMaturity) == sizeof(uint32_t) &&
                   CNA_LOG_LEVEL_FATAL == UINT32_C(0) &&
                   CNA_LOG_LEVEL_TRACE == UINT32_C(5) &&
                   CNA_LOG_LEVEL_EXPERIMENT == UINT32_C(100) &&
                   CNA_LOG_CATEGORY_APPLICATION == UINT32_C(0) &&
                   CNA_LOG_CATEGORY_GPU == UINT32_C(8) &&
                   CNA_PLATFORM_DESKTOP == UINT32_C(0) &&
                   CNA_PLATFORM_WEB == UINT32_C(3) &&
                   CNA_DESKTOP_OS_WINDOWS == UINT32_C(0) &&
                   CNA_DESKTOP_OS_OTHER == UINT32_C(3) &&
                   CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE == UINT32_C(0) &&
                   CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC == UINT32_C(4) &&
                   CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION == UINT32_C(0) &&
                   CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED == UINT32_C(4),
               "CNA core-extension identities must remain stable");

_Static_assert(sizeof(CNA_GamePadType) == sizeof(uint32_t) &&
                   CNA_GAMEPAD_TYPE_UNKNOWN == UINT32_C(0) &&
                   CNA_GAMEPAD_TYPE_GAMEPAD == UINT32_C(1) &&
                   CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD == UINT32_C(9),
               "CNA gamepad-type identities must remain stable");
_Static_assert(sizeof(CNA_GamePadCapabilities) == 48U &&
                   _Alignof(CNA_GamePadCapabilities) == 4U &&
                   offsetof(CNA_GamePadCapabilities, struct_size) == 0U &&
                   offsetof(CNA_GamePadCapabilities, struct_version) == 4U &&
                   offsetof(CNA_GamePadCapabilities, gamepad_type) == 8U &&
                   offsetof(CNA_GamePadCapabilities, is_connected) == 12U &&
                   offsetof(CNA_GamePadCapabilities, has_a_button) == 13U &&
                   offsetof(CNA_GamePadCapabilities, has_voice_support) == 36U &&
                   offsetof(CNA_GamePadCapabilities, has_light_bar_ext) == 37U &&
                   offsetof(CNA_GamePadCapabilities, has_accelerometer_ext) == 46U &&
                   offsetof(CNA_GamePadCapabilities, reserved) == 47U,
               "CNA_GamePadCapabilities layout must remain stable");

/* The canonical thumb-stick and trigger values are exactly the two halves of the analog block a
   gamepad snapshot already carried, so the C API exposes one representation, not two. */
_Static_assert(sizeof(CNA_GamePadThumbSticks) == 16U &&
                   _Alignof(CNA_GamePadThumbSticks) == 4U &&
                   offsetof(CNA_GamePadThumbSticks, left) == 0U &&
                   offsetof(CNA_GamePadThumbSticks, right) == 8U &&
                   sizeof(CNA_GamePadTriggers) == 8U &&
                   _Alignof(CNA_GamePadTriggers) == 4U &&
                   offsetof(CNA_GamePadTriggers, left) == 0U &&
                   offsetof(CNA_GamePadTriggers, right) == 4U,
               "CNA gamepad analog value layouts must remain stable");
_Static_assert(offsetof(CNA_GamePadAnalogState, left_thumb_stick) ==
                       offsetof(CNA_GamePadThumbSticks, left) &&
                   offsetof(CNA_GamePadAnalogState, right_thumb_stick) ==
                       offsetof(CNA_GamePadThumbSticks, right) &&
                   offsetof(CNA_GamePadAnalogState, left_trigger) ==
                       sizeof(CNA_GamePadThumbSticks) + offsetof(CNA_GamePadTriggers, left) &&
                   offsetof(CNA_GamePadAnalogState, right_trigger) ==
                       sizeof(CNA_GamePadThumbSticks) + offsetof(CNA_GamePadTriggers, right) &&
                   sizeof(CNA_GamePadAnalogState) ==
                       sizeof(CNA_GamePadThumbSticks) + sizeof(CNA_GamePadTriggers),
               "CNA_GamePadAnalogState must stay the thumbstick and trigger values back to back");

_Static_assert(sizeof(CNA_GamePadButtonLabel) == sizeof(uint32_t) &&
                   sizeof(CNA_GamePadConnectionState) == sizeof(uint32_t) &&
                   sizeof(CNA_PowerState) == sizeof(uint32_t) &&
                   CNA_GAMEPAD_BUTTON_LABEL_UNKNOWN == UINT32_C(0) &&
                   CNA_GAMEPAD_BUTTON_LABEL_TRIANGLE == UINT32_C(8) &&
                   CNA_GAMEPAD_CONNECTION_STATE_UNKNOWN == UINT32_C(0) &&
                   CNA_GAMEPAD_CONNECTION_STATE_WIRELESS == UINT32_C(2) &&
                   CNA_POWER_STATE_ERROR == UINT32_C(0) &&
                   CNA_POWER_STATE_CHARGED == UINT32_C(5),
               "CNA gamepad device identities must remain stable");
_Static_assert(sizeof(CNA_GamePadTouchpadFinger) == 16U &&
                   _Alignof(CNA_GamePadTouchpadFinger) == 4U &&
                   offsetof(CNA_GamePadTouchpadFinger, is_down) == 0U &&
                   offsetof(CNA_GamePadTouchpadFinger, reserved) == 1U &&
                   offsetof(CNA_GamePadTouchpadFinger, x) == 4U &&
                   offsetof(CNA_GamePadTouchpadFinger, y) == 8U &&
                   offsetof(CNA_GamePadTouchpadFinger, pressure) == 12U,
               "CNA_GamePadTouchpadFinger layout must remain stable");

_Static_assert(sizeof(CNA_KeyState) == sizeof(uint32_t) &&
                   sizeof(CNA_KeyModifiers) == sizeof(uint32_t) &&
                   CNA_KEY_STATE_UP == UINT32_C(0) &&
                   CNA_KEY_STATE_DOWN == UINT32_C(1) &&
                   CNA_KEY_MODIFIER_NONE == UINT32_C(0) &&
                   CNA_KEY_MODIFIER_SHIFT == UINT32_C(1) &&
                   CNA_KEY_MODIFIER_MODE == UINT32_C(0x80) &&
                   CNA_KEY_MODIFIER_ALL == UINT32_C(0xFF),
               "CNA keyboard identities must remain stable");

_Static_assert(sizeof(CNA_MouseCursorHandle) == 8U &&
                   sizeof(CNA_MouseEventRegistrationHandle) == 8U &&
                   sizeof(CNA_MouseCursorStock) == sizeof(uint32_t) &&
                   CNA_MOUSE_CURSOR_STOCK_ARROW == UINT32_C(0) &&
                   CNA_MOUSE_CURSOR_STOCK_WAIT_ARROW == UINT32_C(11),
               "CNA mouse cursor identities must remain stable");

_Static_assert(sizeof(CNA_TextInputRegistrationHandle) == 8U &&
                   sizeof(CNA_TextInputType) == sizeof(uint32_t) &&
                   CNA_TEXT_INPUT_TYPE_TEXT == UINT32_C(0) &&
                   CNA_TEXT_INPUT_TYPE_TEXT_NAME == UINT32_C(1) &&
                   CNA_TEXT_INPUT_TYPE_TEXT_EMAIL == UINT32_C(2) &&
                   CNA_TEXT_INPUT_TYPE_TEXT_USERNAME == UINT32_C(3) &&
                   CNA_TEXT_INPUT_TYPE_TEXT_PASSWORD_HIDDEN == UINT32_C(4) &&
                   CNA_TEXT_INPUT_TYPE_TEXT_PASSWORD_VISIBLE == UINT32_C(5) &&
                   CNA_TEXT_INPUT_TYPE_NUMBER == UINT32_C(6) &&
                   CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_HIDDEN == UINT32_C(7) &&
                   CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_VISIBLE == UINT32_C(8) &&
                   CNA_TEXT_INPUT_TYPE_MAXIMUM == UINT32_C(8),
               "CNA text input identities must remain stable");

_Static_assert(sizeof(CNA_TextEditingEventInfo) == 32U &&
                   _Alignof(CNA_TextEditingEventInfo) == 8U &&
                   offsetof(CNA_TextEditingEventInfo, text) == 8U &&
                   offsetof(CNA_TextEditingEventInfo, start) == 24U &&
                   offsetof(CNA_TextEditingEventInfo, length) == 28U,
               "CNA_TextEditingEventInfo layout must remain stable");

_Static_assert(sizeof(CNA_TextEditingCandidatesEventInfo) == 32U &&
                   _Alignof(CNA_TextEditingCandidatesEventInfo) == 8U &&
                   offsetof(CNA_TextEditingCandidatesEventInfo, candidates) == 8U &&
                   offsetof(CNA_TextEditingCandidatesEventInfo, candidate_count) == 16U &&
                   offsetof(CNA_TextEditingCandidatesEventInfo, selected) == 20U &&
                   offsetof(CNA_TextEditingCandidatesEventInfo, horizontal) == 24U &&
                   offsetof(CNA_TextEditingCandidatesEventInfo, reserved) == 25U,
               "CNA_TextEditingCandidatesEventInfo layout must remain stable");

_Static_assert(sizeof(CNA_GestureType) == sizeof(uint32_t) &&
                   CNA_GESTURE_TYPE_NONE == UINT32_C(0) &&
                   CNA_GESTURE_TYPE_TAP == UINT32_C(1) &&
                   CNA_GESTURE_TYPE_DOUBLE_TAP == UINT32_C(2) &&
                   CNA_GESTURE_TYPE_HOLD == UINT32_C(4) &&
                   CNA_GESTURE_TYPE_HORIZONTAL_DRAG == UINT32_C(8) &&
                   CNA_GESTURE_TYPE_VERTICAL_DRAG == UINT32_C(16) &&
                   CNA_GESTURE_TYPE_FREE_DRAG == UINT32_C(32) &&
                   CNA_GESTURE_TYPE_PINCH == UINT32_C(64) &&
                   CNA_GESTURE_TYPE_FLICK == UINT32_C(128) &&
                   CNA_GESTURE_TYPE_DRAG_COMPLETE == UINT32_C(256) &&
                   CNA_GESTURE_TYPE_PINCH_COMPLETE == UINT32_C(512) &&
                   CNA_GESTURE_TYPE_ALL == UINT32_C(0x000003FF) &&
                   CNA_TOUCH_NO_FINGER == INT32_C(-1),
               "CNA gesture identities must remain stable");

_Static_assert(sizeof(CNA_GestureSample) == 64U &&
                   _Alignof(CNA_GestureSample) == 8U &&
                   offsetof(CNA_GestureSample, gesture_type) == 8U &&
                   offsetof(CNA_GestureSample, finger_id_ext) == 12U &&
                   offsetof(CNA_GestureSample, finger_id2_ext) == 16U &&
                   offsetof(CNA_GestureSample, reserved) == 20U &&
                   offsetof(CNA_GestureSample, timestamp_ticks) == 24U &&
                   offsetof(CNA_GestureSample, position) == 32U &&
                   offsetof(CNA_GestureSample, position2) == 40U &&
                   offsetof(CNA_GestureSample, delta) == 48U &&
                   offsetof(CNA_GestureSample, delta2) == 56U,
               "CNA_GestureSample layout must remain stable");

/* The touch collection surface reuses the existing fixed snapshot rather than adding a second
   spelling of the same numbers, so its capacity is exactly the canonical touch-panel maximum. */
_Static_assert(sizeof(CNA_TouchState) ==
                       16U + (sizeof(CNA_TouchLocation) * CNA_TOUCH_MAX_TOUCHES) &&
                   CNA_TOUCH_MAX_TOUCHES == UINT32_C(8) &&
                   sizeof(CNA_TouchLocation) == 32U,
               "CNA touch collection storage must remain stable");

_Static_assert(sizeof(CNA_HapticDeviceHandle) == 8U &&
                   sizeof(CNA_HapticFeature) == sizeof(uint32_t) &&
                   CNA_HAPTIC_FEATURE_NONE == UINT32_C(0) &&
                   CNA_HAPTIC_FEATURE_CONSTANT == UINT32_C(0x00000001) &&
                   CNA_HAPTIC_FEATURE_LEFT_RIGHT == UINT32_C(0x00000800) &&
                   CNA_HAPTIC_FEATURE_CUSTOM == UINT32_C(0x00008000) &&
                   CNA_HAPTIC_FEATURE_GAIN == UINT32_C(0x00010000) &&
                   CNA_HAPTIC_FEATURE_PAUSE == UINT32_C(0x00080000) &&
                   CNA_HAPTIC_FEATURE_ALL == UINT32_C(0x000F8FFF),
               "CNA haptic feature identities must remain stable");

_Static_assert(sizeof(CNA_HapticEffectType) == sizeof(uint32_t) &&
                   CNA_HAPTIC_EFFECT_TYPE_CONSTANT == UINT32_C(0) &&
                   CNA_HAPTIC_EFFECT_TYPE_LEFT_RIGHT == UINT32_C(11) &&
                   CNA_HAPTIC_EFFECT_TYPE_CUSTOM == UINT32_C(12) &&
                   CNA_HAPTIC_EFFECT_TYPE_MAXIMUM == UINT32_C(12) &&
                   sizeof(CNA_HapticDirectionType) == sizeof(uint32_t) &&
                   CNA_HAPTIC_DIRECTION_TYPE_POLAR == UINT32_C(0) &&
                   CNA_HAPTIC_DIRECTION_TYPE_STEERING_AXIS == UINT32_C(3) &&
                   CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM == UINT32_C(3) &&
                   CNA_HAPTIC_EFFECT_INFINITE_LENGTH == UINT32_C(4294967295),
               "CNA haptic effect identities must remain stable");

_Static_assert(sizeof(CNA_HapticDirection) == 16U &&
                   _Alignof(CNA_HapticDirection) == 4U &&
                   offsetof(CNA_HapticDirection, values) == 4U,
               "CNA_HapticDirection layout must remain stable");

_Static_assert(sizeof(CNA_HapticCapabilities) == 28U &&
                   _Alignof(CNA_HapticCapabilities) == 4U &&
                   offsetof(CNA_HapticCapabilities, features) == 8U &&
                   offsetof(CNA_HapticCapabilities, axis_count) == 12U &&
                   offsetof(CNA_HapticCapabilities, max_effects) == 16U &&
                   offsetof(CNA_HapticCapabilities, max_effects_playing) == 20U &&
                   offsetof(CNA_HapticCapabilities, is_open) == 24U &&
                   offsetof(CNA_HapticCapabilities, rumble_supported) == 25U &&
                   offsetof(CNA_HapticCapabilities, reserved) == 26U,
               "CNA_HapticCapabilities layout must remain stable");

_Static_assert(sizeof(CNA_HapticEffect) == 108U &&
                   _Alignof(CNA_HapticEffect) == 4U &&
                   offsetof(CNA_HapticEffect, type) == 8U &&
                   offsetof(CNA_HapticEffect, direction) == 16U &&
                   offsetof(CNA_HapticEffect, length) == 32U &&
                   offsetof(CNA_HapticEffect, level) == 42U &&
                   offsetof(CNA_HapticEffect, ramp_start) == 52U &&
                   offsetof(CNA_HapticEffect, right_saturation) == 56U &&
                   offsetof(CNA_HapticEffect, center) == 86U &&
                   offsetof(CNA_HapticEffect, large_magnitude) == 92U &&
                   offsetof(CNA_HapticEffect, custom_period) == 96U &&
                   offsetof(CNA_HapticEffect, custom_channels) == 98U &&
                   offsetof(CNA_HapticEffect, fade_level) == 106U,
               "CNA_HapticEffect layout must remain stable");

_Static_assert(sizeof(CNA_JoystickStateHandle) == 8U &&
                   sizeof(CNA_JoystickEventRegistrationHandle) == 8U &&
                   sizeof(CNA_JoystickType) == sizeof(uint32_t) &&
                   CNA_JOYSTICK_TYPE_UNKNOWN == UINT32_C(0) &&
                   CNA_JOYSTICK_TYPE_GAMEPAD == UINT32_C(1) &&
                   CNA_JOYSTICK_TYPE_WHEEL == UINT32_C(2) &&
                   CNA_JOYSTICK_TYPE_ARCADE_STICK == UINT32_C(3) &&
                   CNA_JOYSTICK_TYPE_FLIGHT_STICK == UINT32_C(4) &&
                   CNA_JOYSTICK_TYPE_DANCE_PAD == UINT32_C(5) &&
                   CNA_JOYSTICK_TYPE_GUITAR == UINT32_C(6) &&
                   CNA_JOYSTICK_TYPE_DRUM_KIT == UINT32_C(7) &&
                   CNA_JOYSTICK_TYPE_ARCADE_PAD == UINT32_C(8) &&
                   CNA_JOYSTICK_TYPE_THROTTLE == UINT32_C(9) &&
                   CNA_JOYSTICK_TYPE_MAXIMUM == UINT32_C(9),
               "CNA joystick type identities must remain stable");

/* The canonical hat enumerates its nine reachable combinations as ordinals, so this is an identity
   rather than a bit set: RightUp is 5, not RIGHT | UP. */
_Static_assert(sizeof(CNA_JoystickHatPosition) == sizeof(uint32_t) &&
                   CNA_JOYSTICK_HAT_POSITION_CENTERED == UINT32_C(0) &&
                   CNA_JOYSTICK_HAT_POSITION_UP == UINT32_C(1) &&
                   CNA_JOYSTICK_HAT_POSITION_RIGHT == UINT32_C(2) &&
                   CNA_JOYSTICK_HAT_POSITION_DOWN == UINT32_C(3) &&
                   CNA_JOYSTICK_HAT_POSITION_LEFT == UINT32_C(4) &&
                   CNA_JOYSTICK_HAT_POSITION_RIGHT_UP == UINT32_C(5) &&
                   CNA_JOYSTICK_HAT_POSITION_RIGHT_DOWN == UINT32_C(6) &&
                   CNA_JOYSTICK_HAT_POSITION_LEFT_UP == UINT32_C(7) &&
                   CNA_JOYSTICK_HAT_POSITION_LEFT_DOWN == UINT32_C(8) &&
                   CNA_JOYSTICK_HAT_POSITION_MAXIMUM == UINT32_C(8),
               "CNA joystick hat-position identities must remain stable");

_Static_assert(sizeof(CNA_JoystickInfo) == 16U &&
                   _Alignof(CNA_JoystickInfo) == 4U &&
                   offsetof(CNA_JoystickInfo, id) == 8U &&
                   offsetof(CNA_JoystickInfo, type) == 12U,
               "CNA_JoystickInfo layout must remain stable");

_Static_assert(sizeof(CNA_JoystickCapabilities) == 40U &&
                   _Alignof(CNA_JoystickCapabilities) == 4U &&
                   offsetof(CNA_JoystickCapabilities, axis_count) == 8U &&
                   offsetof(CNA_JoystickCapabilities, button_count) == 12U &&
                   offsetof(CNA_JoystickCapabilities, hat_count) == 16U &&
                   offsetof(CNA_JoystickCapabilities, ball_count) == 20U &&
                   offsetof(CNA_JoystickCapabilities, type) == 24U &&
                   offsetof(CNA_JoystickCapabilities, power_state) == 28U &&
                   offsetof(CNA_JoystickCapabilities, power_percent) == 32U &&
                   offsetof(CNA_JoystickCapabilities, is_connected) == 36U &&
                   offsetof(CNA_JoystickCapabilities, reserved) == 37U,
               "CNA_JoystickCapabilities layout must remain stable");

_Static_assert(sizeof(CNA_InputDeviceEventRegistrationHandle) == 8U &&
                   sizeof(CNA_SensorType) == sizeof(uint32_t) &&
                   CNA_SENSOR_TYPE_UNKNOWN == UINT32_C(0) &&
                   CNA_SENSOR_TYPE_ACCELEROMETER == UINT32_C(1) &&
                   CNA_SENSOR_TYPE_GYROSCOPE == UINT32_C(2) &&
                   CNA_SENSOR_TYPE_ACCELEROMETER_LEFT == UINT32_C(3) &&
                   CNA_SENSOR_TYPE_GYROSCOPE_LEFT == UINT32_C(4) &&
                   CNA_SENSOR_TYPE_ACCELEROMETER_RIGHT == UINT32_C(5) &&
                   CNA_SENSOR_TYPE_GYROSCOPE_RIGHT == UINT32_C(6) &&
                   CNA_SENSOR_TYPE_MAXIMUM == UINT32_C(6),
               "CNA sensor kind identities must remain stable");

_Static_assert(sizeof(CNA_SensorInfo) == 16U &&
                   _Alignof(CNA_SensorInfo) == 4U &&
                   offsetof(CNA_SensorInfo, id) == 8U &&
                   offsetof(CNA_SensorInfo, type) == 12U,
               "CNA_SensorInfo layout must remain stable");

/* The device identifier is 64-bit where the sensor and joystick ones are 32-bit, because a touch
   device identifier is natively 64-bit; that widens the alignment of this value to 8. */
_Static_assert(sizeof(CNA_InputDeviceInfo) == 16U &&
                   _Alignof(CNA_InputDeviceInfo) == 8U &&
                   offsetof(CNA_InputDeviceInfo, id) == 8U &&
                   sizeof(((CNA_InputDeviceInfo*)0)->id) == 8U,
               "CNA_InputDeviceInfo layout must remain stable");

/* The two media source identities are 0 and 4: the canonical gap is reproduced rather than
   renumbered into a dense range, so there is deliberately no MAXIMUM to compare against. */
_Static_assert(sizeof(CNA_MediaState) == sizeof(uint32_t) &&
                   CNA_MEDIA_STATE_STOPPED == UINT32_C(0) &&
                   CNA_MEDIA_STATE_PLAYING == UINT32_C(1) &&
                   CNA_MEDIA_STATE_PAUSED == UINT32_C(2) &&
                   CNA_MEDIA_STATE_MAXIMUM == UINT32_C(2) &&
                   sizeof(CNA_MediaSourceType) == sizeof(uint32_t) &&
                   CNA_MEDIA_SOURCE_TYPE_LOCAL_DEVICE == UINT32_C(0) &&
                   CNA_MEDIA_SOURCE_TYPE_WINDOWS_MEDIA_CONNECT == UINT32_C(4) &&
                   sizeof(CNA_VideoSoundtrackType) == sizeof(uint32_t) &&
                   CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC == UINT32_C(0) &&
                   CNA_VIDEO_SOUNDTRACK_TYPE_DIALOG == UINT32_C(1) &&
                   CNA_VIDEO_SOUNDTRACK_TYPE_MUSIC_AND_DIALOG == UINT32_C(2) &&
                   CNA_VIDEO_SOUNDTRACK_TYPE_MAXIMUM == UINT32_C(2),
               "CNA media identities must remain stable");

_Static_assert(CNA_VISUALIZATION_DATA_SIZE == UINT32_C(256) &&
                   sizeof(CNA_VisualizationData) == 2056U &&
                   _Alignof(CNA_VisualizationData) == 4U &&
                   offsetof(CNA_VisualizationData, frequencies) == 8U &&
                   offsetof(CNA_VisualizationData, samples) == 1032U,
               "CNA_VisualizationData layout must remain stable");

_Static_assert(sizeof(CNA_SensorState) == sizeof(uint32_t) &&
                   CNA_SENSOR_STATE_NOT_SUPPORTED == UINT32_C(0) &&
                   CNA_SENSOR_STATE_READY == UINT32_C(1) &&
                   CNA_SENSOR_STATE_INITIALIZING == UINT32_C(2) &&
                   CNA_SENSOR_STATE_NO_DATA == UINT32_C(3) &&
                   CNA_SENSOR_STATE_NO_PERMISSIONS == UINT32_C(4) &&
                   CNA_SENSOR_STATE_DISABLED == UINT32_C(5) &&
                   CNA_SENSOR_STATE_MAXIMUM == UINT32_C(5),
               "CNA sensor state identities must remain stable");

/* Both members are 100-nanosecond ticks; the local time counts from 0001-01-01, not the Unix
   epoch, because that is the canonical runtime type's own base. */
_Static_assert(sizeof(CNA_DateTimeOffset) == 16U &&
                   _Alignof(CNA_DateTimeOffset) == 8U &&
                   offsetof(CNA_DateTimeOffset, offset_ticks) == 8U,
               "CNA_DateTimeOffset layout must remain stable");

_Static_assert(sizeof(CNA_AccelerometerReading) == 40U &&
                   _Alignof(CNA_AccelerometerReading) == 8U &&
                   offsetof(CNA_AccelerometerReading, timestamp) == 8U &&
                   offsetof(CNA_AccelerometerReading, acceleration) == 24U &&
                   sizeof(CNA_GyroscopeReading) == 40U &&
                   offsetof(CNA_GyroscopeReading, rotation_rate) == 24U,
               "CNA accelerometer and gyroscope reading layouts must remain stable");

_Static_assert(sizeof(CNA_AttitudeReading) == 120U &&
                   _Alignof(CNA_AttitudeReading) == 8U &&
                   offsetof(CNA_AttitudeReading, timestamp) == 8U &&
                   offsetof(CNA_AttitudeReading, pitch) == 24U &&
                   offsetof(CNA_AttitudeReading, quaternion) == 36U &&
                   offsetof(CNA_AttitudeReading, rotation_matrix) == 52U,
               "CNA_AttitudeReading layout must remain stable");

_Static_assert(sizeof(CNA_CompassReading) == 64U &&
                   offsetof(CNA_CompassReading, heading_accuracy) == 24U &&
                   offsetof(CNA_CompassReading, magnetic_heading) == 32U &&
                   offsetof(CNA_CompassReading, true_heading) == 40U &&
                   offsetof(CNA_CompassReading, magnetometer_reading) == 48U,
               "CNA_CompassReading layout must remain stable");

_Static_assert(sizeof(CNA_MotionReading) == 184U &&
                   offsetof(CNA_MotionReading, attitude) == 24U &&
                   offsetof(CNA_MotionReading, device_acceleration) == 144U &&
                   offsetof(CNA_MotionReading, device_rotation_rate) == 156U &&
                   offsetof(CNA_MotionReading, gravity) == 168U,
               "CNA_MotionReading layout must remain stable");

_Static_assert(sizeof(CNA_PowerState) == sizeof(uint32_t) &&
                   CNA_POWER_STATE_ERROR == UINT32_C(0) &&
                   CNA_POWER_STATE_UNKNOWN == UINT32_C(1) &&
                   CNA_POWER_STATE_ON_BATTERY == UINT32_C(2) &&
                   CNA_POWER_STATE_NO_BATTERY == UINT32_C(3) &&
                   CNA_POWER_STATE_CHARGING == UINT32_C(4) &&
                   CNA_POWER_STATE_CHARGED == UINT32_C(5) &&
                   CNA_POWER_STATE_MAXIMUM == UINT32_C(5),
               "CNA host power identities must remain stable");

_Static_assert(sizeof(CNA_MessageBoxType) == sizeof(uint32_t) &&
                   CNA_MESSAGE_BOX_TYPE_ERROR == UINT32_C(0) &&
                   CNA_MESSAGE_BOX_TYPE_WARNING == UINT32_C(1) &&
                   CNA_MESSAGE_BOX_TYPE_INFORMATION == UINT32_C(2) &&
                   CNA_MESSAGE_BOX_TYPE_MAXIMUM == UINT32_C(2),
               "CNA message box severity identities must remain stable");

_Static_assert(sizeof(CNA_VibrationTestLog) == 48U &&
                   _Alignof(CNA_VibrationTestLog) == 8U &&
                   offsetof(CNA_VibrationTestLog, start_calls) == 8U &&
                   offsetof(CNA_VibrationTestLog, last_duration_ticks) == 24U &&
                   offsetof(CNA_VibrationTestLog, last_intensity) == 32U,
               "CNA_VibrationTestLog layout must remain stable");

_Static_assert(sizeof(CNA_MessageBoxTestLog) == 24U &&
                   _Alignof(CNA_MessageBoxTestLog) == 4U &&
                   offsetof(CNA_MessageBoxTestLog, last_type) == 16U &&
                   offsetof(CNA_MessageBoxTestLog, last_button_count) == 20U,
               "CNA_MessageBoxTestLog layout must remain stable");

_Static_assert(sizeof(CNA_MicrophoneState) == sizeof(uint32_t) &&
                   CNA_MICROPHONE_STATE_STARTED == UINT32_C(0) &&
                   CNA_MICROPHONE_STATE_STOPPED == UINT32_C(1) &&
                   CNA_MICROPHONE_STATE_MAXIMUM == UINT32_C(1),
               "CNA microphone state identities must remain stable");

_Static_assert(sizeof(CNA_AudioStopOptions) == sizeof(uint32_t) &&
                   CNA_AUDIO_STOP_OPTIONS_AS_AUTHORED == UINT32_C(0) &&
                   CNA_AUDIO_STOP_OPTIONS_IMMEDIATE == UINT32_C(1) &&
                   CNA_AUDIO_STOP_OPTIONS_MAXIMUM == UINT32_C(1),
               "CNA audio stop-option identities must remain stable");

_Static_assert(sizeof(CNA_PresentationMode) == sizeof(uint32_t) &&
                   CNA_PRESENTATION_MODE_LETTERBOX == UINT32_C(0) &&
                   CNA_PRESENTATION_MODE_OVERSCAN == UINT32_C(1) &&
                   CNA_PRESENTATION_MODE_STRETCH == UINT32_C(2) &&
                   CNA_PRESENTATION_MODE_NATIVE_BACK_BUFFER == UINT32_C(3) &&
                   CNA_PRESENTATION_MODE_FIXED_HEIGHT_DYNAMIC_WIDTH == UINT32_C(4) &&
                   CNA_PRESENTATION_MODE_MAXIMUM == UINT32_C(4) &&
                   sizeof(CNA_GraphicsDeviceManagerEvent) == sizeof(uint32_t) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DISPOSED == UINT32_C(0) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_CREATED == UINT32_C(1) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_DISPOSING == UINT32_C(2) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESET == UINT32_C(3) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESETTING == UINT32_C(4) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM == UINT32_C(4) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_DEFAULT_BACK_BUFFER_WIDTH == INT32_C(800) &&
                   CNA_GRAPHICS_DEVICE_MANAGER_DEFAULT_BACK_BUFFER_HEIGHT == INT32_C(480),
               "CNA graphics device manager identities must remain stable");

/* The configuration carries the presentation parameters the graphics slice already froze, so its
   own layout is pinned relative to them rather than by a number that would move with theirs. */
_Static_assert(offsetof(CNA_GraphicsDeviceInformation, adapter_index) == 8U &&
                   offsetof(CNA_GraphicsDeviceInformation, graphics_profile) == 12U &&
                   offsetof(CNA_GraphicsDeviceInformation, presentation_parameters) == 16U &&
                   sizeof(CNA_GraphicsDeviceInformation) ==
                       16U + sizeof(CNA_PresentationParameters),
               "CNA_GraphicsDeviceInformation layout must remain stable");

_Static_assert(sizeof(CNA_GameWindowEvent) == sizeof(uint32_t) &&
                   CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED == UINT32_C(0) &&
                   CNA_GAME_WINDOW_EVENT_ORIENTATION_CHANGED == UINT32_C(1) &&
                   CNA_GAME_WINDOW_EVENT_SCREEN_DEVICE_NAME_CHANGED == UINT32_C(2) &&
                   CNA_GAME_WINDOW_EVENT_MAXIMUM == UINT32_C(2),
               "CNA game window event identities must remain stable");

_Static_assert(sizeof(CNA_GameEvent) == sizeof(uint32_t) &&
                   CNA_GAME_EVENT_ACTIVATED == UINT32_C(0) &&
                   CNA_GAME_EVENT_DEACTIVATED == UINT32_C(1) &&
                   CNA_GAME_EVENT_DISPOSED == UINT32_C(2) &&
                   CNA_GAME_EVENT_EXITING == UINT32_C(3) &&
                   CNA_GAME_EVENT_MAXIMUM == UINT32_C(3),
               "CNA game event identities must remain stable");

_Static_assert(sizeof(CNA_GameFrameHooks) == 56U &&
                   _Alignof(CNA_GameFrameHooks) == 8U &&
                   offsetof(CNA_GameFrameHooks, initialize) == 8U &&
                   offsetof(CNA_GameFrameHooks, begin_run) == 16U &&
                   offsetof(CNA_GameFrameHooks, end_run) == 24U &&
                   offsetof(CNA_GameFrameHooks, begin_draw) == 32U &&
                   offsetof(CNA_GameFrameHooks, end_draw) == 40U &&
                   offsetof(CNA_GameFrameHooks, context) == 48U,
               "CNA_GameFrameHooks layout must remain stable");

_Static_assert(sizeof(CNA_GameComponentEvent) == sizeof(uint32_t) &&
                   CNA_GAME_COMPONENT_EVENT_ENABLED_CHANGED == UINT32_C(0) &&
                   CNA_GAME_COMPONENT_EVENT_UPDATE_ORDER_CHANGED == UINT32_C(1) &&
                   CNA_GAME_COMPONENT_EVENT_DRAW_ORDER_CHANGED == UINT32_C(2) &&
                   CNA_GAME_COMPONENT_EVENT_VISIBLE_CHANGED == UINT32_C(3) &&
                   CNA_GAME_COMPONENT_EVENT_DISPOSED == UINT32_C(4) &&
                   CNA_GAME_COMPONENT_EVENT_MAXIMUM == UINT32_C(4) &&
                   sizeof(CNA_GameServiceType) == sizeof(uint32_t) &&
                   CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER == UINT32_C(0) &&
                   CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE == UINT32_C(1) &&
                   CNA_GAME_SERVICE_TYPE_MAXIMUM == UINT32_C(1),
               "CNA game component and service identities must remain stable");

_Static_assert(sizeof(CNA_GameComponentCallbacks) == 64U &&
                   _Alignof(CNA_GameComponentCallbacks) == 8U &&
                   offsetof(CNA_GameComponentCallbacks, initialize) == 8U &&
                   offsetof(CNA_GameComponentCallbacks, update) == 16U &&
                   offsetof(CNA_GameComponentCallbacks, draw) == 24U &&
                   offsetof(CNA_GameComponentCallbacks, load_content) == 32U &&
                   offsetof(CNA_GameComponentCallbacks, unload_content) == 40U &&
                   offsetof(CNA_GameComponentCallbacks, dispose) == 48U &&
                   offsetof(CNA_GameComponentCallbacks, context) == 56U,
               "CNA_GameComponentCallbacks layout must remain stable");

_Static_assert(sizeof(CNA_CameraState) == sizeof(uint32_t) &&
                   CNA_CAMERA_STATE_NOT_SUPPORTED == UINT32_C(0) &&
                   CNA_CAMERA_STATE_CLOSED == UINT32_C(1) &&
                   CNA_CAMERA_STATE_OPENING == UINT32_C(2) &&
                   CNA_CAMERA_STATE_DENIED == UINT32_C(3) &&
                   CNA_CAMERA_STATE_READY == UINT32_C(4) &&
                   CNA_CAMERA_STATE_LOST == UINT32_C(5) &&
                   CNA_CAMERA_STATE_MAXIMUM == UINT32_C(5) &&
                   sizeof(CNA_CameraPosition) == sizeof(uint32_t) &&
                   CNA_CAMERA_POSITION_UNKNOWN == UINT32_C(0) &&
                   CNA_CAMERA_POSITION_FRONT_FACING == UINT32_C(1) &&
                   CNA_CAMERA_POSITION_BACK_FACING == UINT32_C(2) &&
                   CNA_CAMERA_POSITION_MAXIMUM == UINT32_C(2),
               "CNA camera identities must remain stable");

_Static_assert(sizeof(CNA_CameraDeviceInfo) == 12U &&
                   _Alignof(CNA_CameraDeviceInfo) == 4U &&
                   offsetof(CNA_CameraDeviceInfo, position) == 8U,
               "CNA_CameraDeviceInfo layout must remain stable");

_Static_assert(sizeof(CNA_FileDialogFilter) == 40U &&
                   _Alignof(CNA_FileDialogFilter) == 8U &&
                   offsetof(CNA_FileDialogFilter, name) == 8U &&
                   offsetof(CNA_FileDialogFilter, pattern) == 24U,
               "CNA_FileDialogFilter layout must remain stable");

/* The legacy reading event carries the same acceleration as three doubles rather than a vector,
   which is why it is a value of its own and not a CNA_AccelerometerReading. */
_Static_assert(sizeof(CNA_AccelerometerReadingEventInfo) == 48U &&
                   _Alignof(CNA_AccelerometerReadingEventInfo) == 8U &&
                   offsetof(CNA_AccelerometerReadingEventInfo, timestamp) == 8U &&
                   offsetof(CNA_AccelerometerReadingEventInfo, x) == 24U &&
                   offsetof(CNA_AccelerometerReadingEventInfo, y) == 32U &&
                   offsetof(CNA_AccelerometerReadingEventInfo, z) == 40U,
               "CNA_AccelerometerReadingEventInfo layout must remain stable");

/* CBIND-106: the `.cnb` container. Both identities are typedefs of uint32_t rather than structures
   -- the canonical CnbChunkId holds exactly one uint32_t with defaulted equality -- so what has to
   stay frozen about them is the width and the wire values, not a layout. */
_Static_assert(sizeof(CNA_CnbChunkId) == sizeof(uint32_t),
               "CNA_CnbChunkId must have a fixed-width representation");
_Static_assert(sizeof(CNA_CnbCompression) == sizeof(uint32_t) &&
                   CNA_CNB_COMPRESSION_NONE == UINT32_C(0) &&
                   CNA_CNB_COMPRESSION_LZ4 == UINT32_C(1) &&
                   CNA_CNB_COMPRESSION_ZSTD == UINT32_C(2) &&
                   CNA_CNB_COMPRESSION_DEFLATE == UINT32_C(3) &&
                   CNA_CNB_COMPRESSION_MAXIMUM == UINT32_C(3),
               "CNB codec identifiers are wire format and must remain stable");

/* Byte offsets and sizes a .cnb file is literally made of. A changed value here does not break an
   ABI so much as stop this build reading files every other build writes. */
_Static_assert(CNA_CNB_FORMAT_MAGIC_SIZE == UINT32_C(4) &&
                   CNA_CNB_FORMAT_HEADER_SIZE == UINT32_C(64) &&
                   CNA_CNB_FORMAT_TOC_ENTRY_SIZE == UINT32_C(48) &&
                   CNA_CNB_FORMAT_HEADER_CHECKSUM_COVERAGE == UINT32_C(44) &&
                   CNA_CNB_FORMAT_HEADER_CHECKSUM_OFFSET == UINT32_C(44) &&
                   CNA_CNB_FORMAT_HEADER_RESERVED_SIZE == UINT32_C(16) &&
                   CNA_CNB_FORMAT_CONTAINER_MAJOR == UINT32_C(1) &&
                   CNA_CNB_FORMAT_CONTAINER_MINOR == UINT32_C(0) &&
                   CNA_CNB_FORMAT_DEFAULT_TOC_OFFSET == UINT64_C(64),
               "CNB container constants are wire format and must remain stable");
_Static_assert(CNA_CNB_CONTAINER_CHUNK_METADATA == UINT32_C(0x54454D43) &&
                   CNA_CNB_CONTAINER_CHUNK_EXTERNAL_REFERENCES == UINT32_C(0x46455258) &&
                   CNA_CNB_CHUNK_FLAG_NONE == UINT32_C(0) &&
                   CNA_CNB_CHUNK_FLAG_MANDATORY == UINT32_C(1) &&
                   CNA_CNB_CHUNK_FLAG_ALL == UINT32_C(1),
               "CNB container chunk identifiers and flags must remain stable");
_Static_assert(CNA_CNB_ASSET_TYPE_INVALID == UINT32_C(0) &&
                   CNA_CNB_ASSET_TYPE_TEXTURE2D == UINT32_C(1) &&
                   CNA_CNB_ASSET_TYPE_TEXTURE3D == UINT32_C(2) &&
                   CNA_CNB_ASSET_TYPE_TEXTURE_CUBE == UINT32_C(3) &&
                   CNA_CNB_ASSET_TYPE_SPRITE_FONT == UINT32_C(4) &&
                   CNA_CNB_ASSET_TYPE_MODEL == UINT32_C(5) &&
                   CNA_CNB_ASSET_TYPE_ANIMATION_CLIP == UINT32_C(6) &&
                   CNA_CNB_ASSET_TYPE_CURVE == UINT32_C(7) &&
                   CNA_CNB_ASSET_TYPE_SOUND_EFFECT == UINT32_C(8) &&
                   CNA_CNB_ASSET_TYPE_SONG == UINT32_C(9) &&
                   CNA_CNB_ASSET_TYPE_VIDEO == UINT32_C(10) &&
                   CNA_CNB_ASSET_TYPE_EFFECT == UINT32_C(11) &&
                   CNA_CNB_ASSET_TYPE_RESERVED_RANGE_FIRST == UINT32_C(0x40000000) &&
                   CNA_CNB_ASSET_TYPE_CUSTOM_RANGE_FIRST == UINT32_C(0x80000000),
               "CNB asset type identifiers are frozen once CNB v1 ships");
_Static_assert(CNA_CNB_CRC32C_SEED == UINT32_C(0),
               "The CRC-32C seed is the checksum of an empty range");

/* The one versioned value structure in the family. Its 64-bit members are placed ahead of its
   32-bit ones so the layout carries no padding on any supported target. */
_Static_assert(sizeof(CNA_CnbReadLimits) == 48U &&
                   _Alignof(CNA_CnbReadLimits) == 8U &&
                   offsetof(CNA_CnbReadLimits, struct_version) == 4U &&
                   offsetof(CNA_CnbReadLimits, max_file_size) == 8U &&
                   offsetof(CNA_CnbReadLimits, max_chunk_size) == 16U &&
                   offsetof(CNA_CnbReadLimits, max_total_uncompressed_size) == 24U &&
                   offsetof(CNA_CnbReadLimits, max_chunk_count) == 32U &&
                   offsetof(CNA_CnbReadLimits, max_string_bytes) == 36U &&
                   offsetof(CNA_CnbReadLimits, max_array_element_count) == 40U &&
                   offsetof(CNA_CnbReadLimits, max_chunk_alignment) == 44U,
               "CNA_CnbReadLimits layout must remain stable");
_Static_assert(CNA_CNB_READ_LIMITS_STRUCT_VERSION == UINT32_C(1),
               "CNA_CnbReadLimits is at structure version 1");

/* CBIND-107: the document's three published values. Each is versioned and each places its 64-bit
   members ahead of its 32-bit ones, so the layouts carry no padding on any supported target. */
_Static_assert(sizeof(CNA_CnbChunkEntry) == 56U &&
                   _Alignof(CNA_CnbChunkEntry) == 8U &&
                   offsetof(CNA_CnbChunkEntry, struct_version) == 4U &&
                   offsetof(CNA_CnbChunkEntry, offset) == 8U &&
                   offsetof(CNA_CnbChunkEntry, stored_size) == 16U &&
                   offsetof(CNA_CnbChunkEntry, uncompressed_size) == 24U &&
                   offsetof(CNA_CnbChunkEntry, type) == 32U &&
                   offsetof(CNA_CnbChunkEntry, flags) == 36U &&
                   offsetof(CNA_CnbChunkEntry, checksum) == 40U &&
                   offsetof(CNA_CnbChunkEntry, compression) == 44U &&
                   offsetof(CNA_CnbChunkEntry, alignment) == 48U &&
                   offsetof(CNA_CnbChunkEntry, reserved) == 52U,
               "CNA_CnbChunkEntry layout must remain stable");
_Static_assert(sizeof(CNA_CnbExternalReference) == 16U &&
                   _Alignof(CNA_CnbExternalReference) == 4U &&
                   offsetof(CNA_CnbExternalReference, flags) == 8U &&
                   offsetof(CNA_CnbExternalReference, expected_asset_type_id) == 12U,
               "CNA_CnbExternalReference layout must remain stable");
_Static_assert(sizeof(CNA_CnbMetadata) == 16U &&
                   _Alignof(CNA_CnbMetadata) == 4U &&
                   offsetof(CNA_CnbMetadata, present) == 8U &&
                   offsetof(CNA_CnbMetadata, reserved) == 9U &&
                   offsetof(CNA_CnbMetadata, flags) == 12U,
               "CNA_CnbMetadata layout must remain stable");
_Static_assert(CNA_CNB_CHUNK_ENTRY_STRUCT_VERSION == UINT32_C(1) &&
                   CNA_CNB_EXTERNAL_REFERENCE_STRUCT_VERSION == UINT32_C(1) &&
                   CNA_CNB_METADATA_STRUCT_VERSION == UINT32_C(1),
               "The CNB document's published structures are all at version 1");

/* CBIND-108: the texture format identifiers are wire format -- a `.cnb` written by any build must
   mean the same thing to every other -- so the whole numbering is frozen here rather than only its
   ends. The three chunk identifiers are frozen with it. */
_Static_assert(sizeof(CNA_CnbTextureFormat) == sizeof(uint32_t) &&
                   CNA_CNB_TEXTURE_FORMAT_UNKNOWN == UINT32_C(0) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA8 == UINT32_C(1) &&
                   CNA_CNB_TEXTURE_FORMAT_BGRA8 == UINT32_C(2) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA8_SRGB == UINT32_C(3) &&
                   CNA_CNB_TEXTURE_FORMAT_BGR565 == UINT32_C(4) &&
                   CNA_CNB_TEXTURE_FORMAT_BGRA5551 == UINT32_C(5) &&
                   CNA_CNB_TEXTURE_FORMAT_BGRA4444 == UINT32_C(6) &&
                   CNA_CNB_TEXTURE_FORMAT_ALPHA8 == UINT32_C(7) &&
                   CNA_CNB_TEXTURE_FORMAT_R8 == UINT32_C(8) &&
                   CNA_CNB_TEXTURE_FORMAT_R16 == UINT32_C(9) &&
                   CNA_CNB_TEXTURE_FORMAT_RG16 == UINT32_C(10) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA16 == UINT32_C(11) &&
                   CNA_CNB_TEXTURE_FORMAT_RG8_SNORM == UINT32_C(12) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA8_SNORM == UINT32_C(13) &&
                   CNA_CNB_TEXTURE_FORMAT_RGB10_A2 == UINT32_C(14) &&
                   CNA_CNB_TEXTURE_FORMAT_R32_FLOAT == UINT32_C(15) &&
                   CNA_CNB_TEXTURE_FORMAT_RG32_FLOAT == UINT32_C(16) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA32_FLOAT == UINT32_C(17) &&
                   CNA_CNB_TEXTURE_FORMAT_R16_FLOAT == UINT32_C(18) &&
                   CNA_CNB_TEXTURE_FORMAT_RG16_FLOAT == UINT32_C(19) &&
                   CNA_CNB_TEXTURE_FORMAT_RGBA16_FLOAT == UINT32_C(20) &&
                   CNA_CNB_TEXTURE_FORMAT_HDR_BLENDABLE == UINT32_C(21) &&
                   CNA_CNB_TEXTURE_FORMAT_BC1 == UINT32_C(22) &&
                   CNA_CNB_TEXTURE_FORMAT_BC2 == UINT32_C(23) &&
                   CNA_CNB_TEXTURE_FORMAT_BC3 == UINT32_C(24) &&
                   CNA_CNB_TEXTURE_FORMAT_BC3_SRGB == UINT32_C(25) &&
                   CNA_CNB_TEXTURE_FORMAT_BC7 == UINT32_C(26) &&
                   CNA_CNB_TEXTURE_FORMAT_BC7_SRGB == UINT32_C(27) &&
                   CNA_CNB_TEXTURE_FORMAT_MAXIMUM == UINT32_C(27),
               "CNB texture format identifiers are wire format and must remain stable");
_Static_assert(CNA_CNB_TEXTURE_CHUNK_HEADER == UINT32_C(0x48584554) &&
                   CNA_CNB_TEXTURE_CHUNK_REPRESENTATIONS == UINT32_C(0x52584554) &&
                   CNA_CNB_TEXTURE_CHUNK_PAYLOAD == UINT32_C(0x44584554) &&
                   CNA_CNB_TEXTURE_SCHEMA_VERSION == UINT32_C(1) &&
                   CNA_CNB_TEXTURE_HEADER_STRIDE == UINT32_C(24) &&
                   CNA_CNB_TEXTURE_REPRESENTATION_STRIDE == UINT32_C(24) &&
                   CNA_CNB_TEXTURE_CUBE_FACE_COUNT == UINT32_C(6) &&
                   CNA_CNB_MAX_TEXTURE_MIP_LEVELS == UINT32_C(16) &&
                   CNA_CNB_MAX_TEXTURE_REPRESENTATIONS == UINT32_C(8),
               "CNB texture schema constants are wire format and must remain stable");
_Static_assert(sizeof(CNA_CnbTextureInfo) == 32U &&
                   _Alignof(CNA_CnbTextureInfo) == 4U &&
                   offsetof(CNA_CnbTextureInfo, width) == 8U &&
                   offsetof(CNA_CnbTextureInfo, height) == 12U &&
                   offsetof(CNA_CnbTextureInfo, depth) == 16U &&
                   offsetof(CNA_CnbTextureInfo, face_count) == 20U &&
                   offsetof(CNA_CnbTextureInfo, mip_count) == 24U &&
                   offsetof(CNA_CnbTextureInfo, representation_count) == 28U &&
                   CNA_CNB_TEXTURE_INFO_STRUCT_VERSION == UINT32_C(1),
               "CNA_CnbTextureInfo layout must remain stable");
