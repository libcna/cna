// SPDX-License-Identifier: MS-PL

#include <CNA/C/abi.h>
#include <CNA/C/core.h>
#include <CNA/C/graphics.h>
#include <CNA/C/graphics_resource.h>
#include <CNA/C/cna.h>

#include <cstddef>

static_assert(CNA_ABI_VERSION == CNA_ABI_VERSION_ENCODE(0, 1, 0));
static_assert(sizeof(CNA_Result) == sizeof(uint32_t));
static_assert(sizeof(CNA_Handle) == sizeof(uint64_t));
static_assert(sizeof(CNA_GraphicsResourceTag) == sizeof(uint64_t));
static_assert(sizeof(CNA_GraphicsResourceEventRegistrationHandle) == sizeof(uint64_t));
static_assert(sizeof(CNA_ErrorCategory) == sizeof(uint32_t));
static_assert(sizeof(CNA_GameTime) == 24U);
static_assert(sizeof(CNA_Color) == 4U);
static_assert(CNA_COLOR_TRANSPARENT.a == UINT8_C(0));
static_assert(CNA_COLOR_CORNFLOWER_BLUE.r == UINT8_C(100));
static_assert(CNA_COLOR_YELLOW_GREEN.b == UINT8_C(50));
static_assert(sizeof(CNA_GraphicsCapability) == sizeof(uint32_t));
static_assert(sizeof(CNA_GraphicsRendererType) == sizeof(uint32_t));
static_assert(sizeof(CNA_GraphicsCapabilityFlags) == sizeof(uint64_t));
static_assert(sizeof(CNA_EffectParameterClass) == sizeof(uint32_t));
static_assert(CNA_EFFECT_PARAMETER_CLASS_SCALAR == UINT32_C(0));
static_assert(CNA_EFFECT_PARAMETER_CLASS_VECTOR == UINT32_C(1));
static_assert(CNA_EFFECT_PARAMETER_CLASS_MATRIX == UINT32_C(2));
static_assert(CNA_EFFECT_PARAMETER_CLASS_OBJECT == UINT32_C(3));
static_assert(CNA_EFFECT_PARAMETER_CLASS_STRUCT == UINT32_C(4));
static_assert(sizeof(CNA_EffectParameterType) == sizeof(uint32_t));
static_assert(CNA_EFFECT_PARAMETER_TYPE_VOID == UINT32_C(0));
static_assert(CNA_EFFECT_PARAMETER_TYPE_BOOL == UINT32_C(1));
static_assert(CNA_EFFECT_PARAMETER_TYPE_INT32 == UINT32_C(2));
static_assert(CNA_EFFECT_PARAMETER_TYPE_SINGLE == UINT32_C(3));
static_assert(CNA_EFFECT_PARAMETER_TYPE_STRING == UINT32_C(4));
static_assert(CNA_EFFECT_PARAMETER_TYPE_TEXTURE == UINT32_C(5));
static_assert(CNA_EFFECT_PARAMETER_TYPE_TEXTURE1D == UINT32_C(6));
static_assert(CNA_EFFECT_PARAMETER_TYPE_TEXTURE2D == UINT32_C(7));
static_assert(CNA_EFFECT_PARAMETER_TYPE_TEXTURE3D == UINT32_C(8));
static_assert(CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE == UINT32_C(9));
static_assert(sizeof(CNA_EffectAnnotationHandle) == 8U);
static_assert(sizeof(CNA_EffectAnnotationCollectionHandle) == 8U);
static_assert(sizeof(CNA_EffectAnnotationCreateInfo) == 88U);
static_assert(alignof(CNA_EffectAnnotationCreateInfo) == 8U);
static_assert(offsetof(CNA_EffectAnnotationCreateInfo, name) == 8U);
static_assert(offsetof(CNA_EffectAnnotationCreateInfo, semantic) == 24U);
static_assert(offsetof(CNA_EffectAnnotationCreateInfo, row_count) == 40U);
static_assert(offsetof(CNA_EffectAnnotationCreateInfo, data) == 56U);
static_assert(offsetof(CNA_EffectAnnotationCreateInfo, cached_string) == 72U);
static_assert(sizeof(CNA_EffectAnnotationInfo) == 24U);
static_assert(alignof(CNA_EffectAnnotationInfo) == 4U);
static_assert(sizeof(CNA_EffectValueType) == sizeof(uint32_t));
static_assert(CNA_EFFECT_VALUE_BOOLEAN == UINT32_C(0));
static_assert(CNA_EFFECT_VALUE_INT32 == UINT32_C(1));
static_assert(CNA_EFFECT_VALUE_SINGLE == UINT32_C(2));
static_assert(CNA_EFFECT_VALUE_MATRIX == UINT32_C(3));
static_assert(CNA_EFFECT_VALUE_MATRIX_TRANSPOSE == UINT32_C(4));
static_assert(CNA_EFFECT_VALUE_QUATERNION == UINT32_C(5));
static_assert(CNA_EFFECT_VALUE_VECTOR2 == UINT32_C(6));
static_assert(CNA_EFFECT_VALUE_VECTOR3 == UINT32_C(7));
static_assert(CNA_EFFECT_VALUE_VECTOR4 == UINT32_C(8));
static_assert(sizeof(CNA_EffectTextureType) == sizeof(uint32_t));
static_assert(CNA_EFFECT_TEXTURE_BASE == UINT32_C(0));
static_assert(CNA_EFFECT_TEXTURE_2D == UINT32_C(1));
static_assert(CNA_EFFECT_TEXTURE_3D == UINT32_C(2));
static_assert(CNA_EFFECT_TEXTURE_CUBE == UINT32_C(3));
static_assert(sizeof(CNA_EffectParameterHandle) == 8U);
static_assert(sizeof(CNA_EffectParameterCollectionHandle) == 8U);
static_assert(sizeof(CNA_EffectParameterCreateInfo) == 56U);
static_assert(alignof(CNA_EffectParameterCreateInfo) == 8U);
static_assert(offsetof(CNA_EffectParameterCreateInfo, name) == 8U);
static_assert(offsetof(CNA_EffectParameterCreateInfo, semantic) == 24U);
static_assert(offsetof(CNA_EffectParameterCreateInfo, row_count) == 40U);
static_assert(offsetof(CNA_EffectParameterCreateInfo, parameter_type) == 52U);
static_assert(sizeof(CNA_EffectParameterInfo) == 24U);
static_assert(alignof(CNA_EffectParameterInfo) == 4U);
static_assert(sizeof(CNA_EffectPassHandle) == 8U);
static_assert(sizeof(CNA_EffectPassCollectionHandle) == 8U);
static_assert(sizeof(CNA_EffectTechniqueHandle) == 8U);
static_assert(sizeof(CNA_EffectTechniqueCollectionHandle) == 8U);
static_assert(sizeof(CNA_EffectHandle) == 8U);
static_assert(sizeof(CNA_DirectionalLightHandle) == 8U);
static_assert(CNA_SKINNED_EFFECT_MAX_BONES == UINT32_C(72));
static_assert(sizeof(CNA_ColorMatrix4x4) == 64U);
static_assert(alignof(CNA_ColorMatrix4x4) == 4U);
static_assert(sizeof(CNA_PbrTextureSlot) == sizeof(uint32_t));
static_assert(CNA_PBR_TEXTURE_BASE_COLOR == UINT32_C(0));
static_assert(CNA_PBR_TEXTURE_NORMAL == UINT32_C(1));
static_assert(CNA_PBR_TEXTURE_METALLIC_ROUGHNESS == UINT32_C(2));
static_assert(CNA_PBR_TEXTURE_EMISSIVE == UINT32_C(3));
static_assert(CNA_PBR_TEXTURE_OCCLUSION == UINT32_C(4));
static_assert(CNA_SKINNED_PBR_EFFECT_MAX_BONES == UINT32_C(72));
static_assert(sizeof(CNA_ModelBoneHandle) == 8U);
static_assert(sizeof(CNA_ModelBoneCollectionHandle) == 8U);
static_assert(sizeof(CNA_ModelMeshPartHandle) == 8U);
static_assert(sizeof(CNA_ModelMeshPartCollectionHandle) == 8U);
static_assert(sizeof(CNA_ModelMeshPartTag) == 8U);
static_assert(sizeof(CNA_ModelMeshHandle) == 8U);
static_assert(sizeof(CNA_ModelMeshCollectionHandle) == 8U);
static_assert(sizeof(CNA_ModelEffectCollectionHandle) == 8U);
static_assert(sizeof(CNA_ModelMeshTag) == 8U);
static_assert(sizeof(CNA_ModelHandle) == 8U);
static_assert(sizeof(CNA_ModelTag) == 8U);
static_assert(sizeof(CNA_MorphTargetDataEXTHandle) == 8U);
static_assert(sizeof(CNA_MorphWeightKeyframeEXTDescriptor) == 56U);
static_assert(sizeof(CNA_MorphWeightTrackEXTDescriptor) == 24U);
static_assert(sizeof(CNA_MorphTargetDeltaEXTDescriptor) == 32U);
static_assert(sizeof(CNA_MorphTargetDataEXTDescriptor) == 80U);
static_assert(sizeof(CNA_SkinnedModelEXTHandle) == 8U);
static_assert(sizeof(CNA_KeyframeEXT) == 48U);
static_assert(sizeof(CNA_BoneTrackEXTDescriptor) == 24U);
static_assert(sizeof(CNA_AnimationClipEXTDescriptor) == 24U);
static_assert(sizeof(CNA_NamedAnimationClipEXTDescriptor) == 40U);
static_assert(sizeof(CNA_SkinnedModelEXTDescriptor) == 48U);
static_assert(sizeof(CNA_SkinningDataHandle) == 8U);
static_assert(sizeof(CNA_AnimationPlayerHandle) == 8U);
static_assert(sizeof(CNA_SkinningDataDescriptor) == 64U);
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
static_assert(sizeof(CNA_TextureDataType) == sizeof(uint32_t));
static_assert(CNA_TEXTURE_DATA_COLOR == UINT32_C(0));
static_assert(CNA_TEXTURE_DATA_USHORT == UINT32_C(17));
static_assert(sizeof(CNA_TextureImageFormat) == sizeof(uint32_t));
static_assert(CNA_TEXTURE_IMAGE_FORMAT_PNG == UINT32_C(0));
static_assert(CNA_TEXTURE_IMAGE_FORMAT_JPEG == UINT32_C(1));
static_assert(sizeof(CNA_TextureInfo) == 16U);
static_assert(sizeof(CNA_Texture2DTransfer) == 48U);
static_assert(alignof(CNA_Texture2DTransfer) == 8U);
static_assert(offsetof(CNA_Texture2DTransfer, level) == 8U);
static_assert(offsetof(CNA_Texture2DTransfer, has_rectangle) == 12U);
static_assert(offsetof(CNA_Texture2DTransfer, rectangle) == 16U);
static_assert(offsetof(CNA_Texture2DTransfer, start_index) == 32U);
static_assert(offsetof(CNA_Texture2DTransfer, element_count) == 40U);
static_assert(sizeof(CNA_Texture2DDecodeInfo) == 24U);
static_assert(sizeof(CNA_Texture2DStorageInfo) == 16U);
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
static_assert(sizeof(CNA_SpriteScaledCommand) == 72U,
              "CNA_SpriteScaledCommand layout must remain stable");
static_assert(offsetof(CNA_SpriteScaledCommand, scale) == 56U,
              "CNA_SpriteScaledCommand scale must follow the origin");
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
static_assert(sizeof(CNA_AudioCapabilities) == 16U);
static_assert(sizeof(CNA_SoundEffectCreateInfo) == 24U);
static_assert(sizeof(CNA_SoundEffectInstanceInfo) == 32U);
static_assert(sizeof(CNA_Vector3) == 12U);
static_assert(sizeof(CNA_AudioEmitter) == 60U && offsetof(CNA_AudioEmitter, doppler_scale) == 8U &&
                  offsetof(CNA_AudioEmitter, forward) == 12U &&
                  offsetof(CNA_AudioEmitter, position) == 24U &&
                  offsetof(CNA_AudioEmitter, up) == 36U &&
                  offsetof(CNA_AudioEmitter, velocity) == 48U,
              "CNA_AudioEmitter layout must remain stable");
static_assert(sizeof(CNA_AvatarExpression) == 28U &&
                 offsetof(CNA_AvatarExpression, mouth) == 8U,
            "CNA_AvatarExpression layout must remain stable");
static_assert(sizeof(CNA_AvatarAppearanceEXT) == 28U &&
                 offsetof(CNA_AvatarAppearanceEXT, skin_color) == 8U,
            "CNA_AvatarAppearanceEXT layout must remain stable");
static_assert(sizeof(CNA_AvatarDescriptionInfo) == 32U &&
                 offsetof(CNA_AvatarDescriptionInfo, body_type) == 8U &&
                 offsetof(CNA_AvatarDescriptionInfo, description_byte_count) == 16U &&
                 offsetof(CNA_AvatarDescriptionInfo, is_valid) == 24U,
            "CNA_AvatarDescriptionInfo layout must remain stable");
static_assert(sizeof(CNA_AvatarAnimationInfo) == 32U &&
                 offsetof(CNA_AvatarAnimationInfo, bone_transform_count) == 8U &&
                 offsetof(CNA_AvatarAnimationInfo, current_position_ticks) == 16U,
            "CNA_AvatarAnimationInfo layout must remain stable");
static_assert(sizeof(CNA_AvatarRendererInfo) == 16U &&
                 offsetof(CNA_AvatarRendererInfo, state) == 8U &&
                 offsetof(CNA_AvatarRendererInfo, is_disposed) == 12U,
            "CNA_AvatarRendererInfo layout must remain stable");
static_assert(CNA_AVATAR_RENDERER_BONE_COUNT == INT32_C(71) &&
                 CNA_AVATAR_DESCRIPTION_BYTE_COUNT == UINT64_C(1021),
            "CNA avatar constants must remain stable");
static_assert(sizeof(CNA_LeaderboardIdentity) == 76U &&
                 offsetof(CNA_LeaderboardIdentity, game_mode) == 8U &&
                 offsetof(CNA_LeaderboardIdentity, key) == 12U &&
                 CNA_LEADERBOARD_IDENTITY_KEY_CAPACITY == UINT32_C(64),
            "CNA_LeaderboardIdentity layout must remain stable");
static_assert(sizeof(CNA_LeaderboardReaderInfo) == 24U &&
                 offsetof(CNA_LeaderboardReaderInfo, page_start) == 8U &&
                 offsetof(CNA_LeaderboardReaderInfo, is_disposed) == 20U,
            "CNA_LeaderboardReaderInfo layout must remain stable");
static_assert(sizeof(CNA_LeaderboardEntryInfo) == 24U &&
                 offsetof(CNA_LeaderboardEntryInfo, ranking) == 8U &&
                 offsetof(CNA_LeaderboardEntryInfo, rating) == 16U,
            "CNA_LeaderboardEntryInfo layout must remain stable");
static_assert(sizeof(CNA_GameDefaults) == 40U &&
                 offsetof(CNA_GameDefaults, game_difficulty) == 8U &&
                 offsetof(CNA_GameDefaults, has_primary_color) == 20U &&
                 offsetof(CNA_GameDefaults, primary_color) == 32U &&
                 offsetof(CNA_GameDefaults, secondary_color) == 36U,
            "CNA_GameDefaults layout must remain stable");
static_assert(sizeof(CNA_PropertyValueKind) == sizeof(uint32_t) &&
                 CNA_PROPERTY_VALUE_KIND_UNKNOWN == UINT32_C(0) &&
                 CNA_PROPERTY_VALUE_KIND_TIME_SPAN == UINT32_C(9) &&
                 CNA_PROPERTY_VALUE_KIND_MAXIMUM == CNA_PROPERTY_VALUE_KIND_TIME_SPAN,
            "CNA property value kinds must remain stable");
static_assert(sizeof(CNA_AchievementInfo) == 24U &&
                 offsetof(CNA_AchievementInfo, gamer_score) == 8U &&
                 offsetof(CNA_AchievementInfo, is_earned) == 14U &&
                 offsetof(CNA_AchievementInfo, earned_date_time_ticks) == 16U,
            "CNA_AchievementInfo layout must remain stable");
static_assert(sizeof(CNA_GamerPresence) == 16U &&
                 offsetof(CNA_GamerPresence, presence_mode) == 8U &&
                 offsetof(CNA_GamerPresence, presence_value) == 12U,
            "CNA_GamerPresence layout must remain stable");
static_assert(sizeof(CNA_GamerPrivileges) == 28U &&
                 offsetof(CNA_GamerPrivileges, allow_communication) == 8U &&
                 offsetof(CNA_GamerPrivileges, allow_online_sessions) == 20U,
            "CNA_GamerPrivileges layout must remain stable");
static_assert(sizeof(CNA_GamerProfileInfo) == 32U &&
                 offsetof(CNA_GamerProfileInfo, gamer_score) == 8U &&
                 offsetof(CNA_GamerProfileInfo, is_disposed) == 28U,
            "CNA_GamerProfileInfo layout must remain stable");
static_assert(sizeof(CNA_FriendGamerInfo) == 24U &&
                 offsetof(CNA_FriendGamerInfo, friend_request_received_from) == 8U &&
                 offsetof(CNA_FriendGamerInfo, is_playing) == 19U,
            "CNA_FriendGamerInfo layout must remain stable");
static_assert(sizeof(CNA_SignedInGamerEventInfo) == 24U &&
                 offsetof(CNA_SignedInGamerEventInfo, gamer) == 16U,
            "CNA_SignedInGamerEventInfo layout must remain stable");
static_assert(sizeof(CNA_AvatarBodyType) == sizeof(uint32_t) &&
                 CNA_AVATAR_BODY_TYPE_FEMALE == UINT32_C(0) &&
                 CNA_AVATAR_BODY_TYPE_MALE == UINT32_C(1) &&
                 CNA_AVATAR_BODY_TYPE_MAXIMUM == CNA_AVATAR_BODY_TYPE_MALE,
            "CNA AvatarBodyType identities must remain stable");
static_assert(sizeof(CNA_AvatarRendererState) == sizeof(uint32_t) &&
                 CNA_AVATAR_RENDERER_STATE_LOADING == UINT32_C(0) &&
                 CNA_AVATAR_RENDERER_STATE_UNAVAILABLE == UINT32_C(2) &&
                 CNA_AVATAR_RENDERER_STATE_MAXIMUM == CNA_AVATAR_RENDERER_STATE_UNAVAILABLE,
            "CNA AvatarRendererState identities must remain stable");
static_assert(sizeof(CNA_AvatarEyebrow) == sizeof(uint32_t) &&
                 CNA_AVATAR_EYEBROW_NEUTRAL == UINT32_C(0) &&
                 CNA_AVATAR_EYEBROW_RAISED == UINT32_C(4) &&
                 CNA_AVATAR_EYEBROW_MAXIMUM == CNA_AVATAR_EYEBROW_RAISED,
            "CNA AvatarEyebrow identities must remain stable");
static_assert(sizeof(CNA_AvatarEye) == sizeof(uint32_t) &&
                 CNA_AVATAR_EYE_NEUTRAL == UINT32_C(0) &&
                 CNA_AVATAR_EYE_BLINK == UINT32_C(13) &&
                 CNA_AVATAR_EYE_MAXIMUM == CNA_AVATAR_EYE_BLINK,
            "CNA AvatarEye identities must remain stable");
static_assert(sizeof(CNA_AvatarMouth) == sizeof(uint32_t) &&
                 CNA_AVATAR_MOUTH_NEUTRAL == UINT32_C(0) &&
                 CNA_AVATAR_MOUTH_PHONETIC_DTH == UINT32_C(13) &&
                 CNA_AVATAR_MOUTH_MAXIMUM == CNA_AVATAR_MOUTH_PHONETIC_DTH,
            "CNA AvatarMouth identities must remain stable");
static_assert(sizeof(CNA_AvatarAnimationPreset) == sizeof(uint32_t) &&
                 CNA_AVATAR_ANIMATION_PRESET_STAND_0 == UINT32_C(0) &&
                 CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN == UINT32_C(30) &&
                 CNA_AVATAR_ANIMATION_PRESET_MAXIMUM == CNA_AVATAR_ANIMATION_PRESET_MALE_YAWN,
            "CNA AvatarAnimationPreset identities must remain stable");
static_assert(sizeof(CNA_AvatarBone) == sizeof(uint32_t) &&
                 CNA_AVATAR_BONE_ROOT == UINT32_C(0) &&
                 CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT == UINT32_C(70) &&
                 CNA_AVATAR_BONE_MAXIMUM == CNA_AVATAR_BONE_FINGER_THUMB_3_RIGHT,
            "CNA AvatarBone identities must remain stable");
static_assert(sizeof(CNA_GamerPresenceMode) == sizeof(uint32_t) &&
                  CNA_GAMER_PRESENCE_MODE_NONE == UINT32_C(0) &&
                 CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE == UINT32_C(59) &&
                 CNA_GAMER_PRESENCE_MODE_MAXIMUM == CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE,
              "CNA GamerPresenceMode identities must remain stable");
static_assert(sizeof(CNA_NotificationPosition) == sizeof(uint32_t) &&
                  CNA_NOTIFICATION_POSITION_TOP_LEFT == UINT32_C(0) &&
                 CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT == UINT32_C(8) &&
                 CNA_NOTIFICATION_POSITION_MAXIMUM == CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT,
              "CNA NotificationPosition identities must remain stable");
static_assert(sizeof(CNA_GamerZone) == sizeof(uint32_t) &&
                  CNA_GAMER_ZONE_UNKNOWN == UINT32_C(0) &&
                 CNA_GAMER_ZONE_UNDERGROUND == UINT32_C(4) &&
                 CNA_GAMER_ZONE_MAXIMUM == CNA_GAMER_ZONE_UNDERGROUND,
              "CNA GamerZone identities must remain stable");
static_assert(sizeof(CNA_LeaderboardKey) == sizeof(uint32_t) &&
                  CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME == UINT32_C(0) &&
                 CNA_LEADERBOARD_KEY_BEST_TIME_RECENT == UINT32_C(3) &&
                 CNA_LEADERBOARD_KEY_MAXIMUM == CNA_LEADERBOARD_KEY_BEST_TIME_RECENT,
              "CNA LeaderboardKey identities must remain stable");
static_assert(sizeof(CNA_LeaderboardOutcome) == sizeof(uint32_t) &&
                  CNA_LEADERBOARD_OUTCOME_NONE == UINT32_C(0) &&
                 CNA_LEADERBOARD_OUTCOME_TIE == UINT32_C(3) &&
                 CNA_LEADERBOARD_OUTCOME_MAXIMUM == CNA_LEADERBOARD_OUTCOME_TIE,
              "CNA LeaderboardOutcome identities must remain stable");
static_assert(sizeof(CNA_MessageBoxIcon) == sizeof(uint32_t) &&
                  CNA_MESSAGE_BOX_ICON_NONE == UINT32_C(0) &&
                 CNA_MESSAGE_BOX_ICON_ALERT == UINT32_C(3) &&
                 CNA_MESSAGE_BOX_ICON_MAXIMUM == CNA_MESSAGE_BOX_ICON_ALERT,
              "CNA MessageBoxIcon identities must remain stable");
static_assert(sizeof(CNA_ControllerSensitivity) == sizeof(uint32_t) &&
                  CNA_CONTROLLER_SENSITIVITY_LOW == UINT32_C(0) &&
                 CNA_CONTROLLER_SENSITIVITY_HIGH == UINT32_C(2) &&
                 CNA_CONTROLLER_SENSITIVITY_MAXIMUM == CNA_CONTROLLER_SENSITIVITY_HIGH,
              "CNA ControllerSensitivity identities must remain stable");
static_assert(sizeof(CNA_GameDifficulty) == sizeof(uint32_t) &&
                  CNA_GAME_DIFFICULTY_EASY == UINT32_C(0) &&
                 CNA_GAME_DIFFICULTY_HARD == UINT32_C(2) &&
                 CNA_GAME_DIFFICULTY_MAXIMUM == CNA_GAME_DIFFICULTY_HARD,
              "CNA GameDifficulty identities must remain stable");
static_assert(sizeof(CNA_GamerPrivilegeSetting) == sizeof(uint32_t) &&
                  CNA_GAMER_PRIVILEGE_SETTING_BLOCKED == UINT32_C(0) &&
                 CNA_GAMER_PRIVILEGE_SETTING_EVERYONE == UINT32_C(2) &&
                 CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM == CNA_GAMER_PRIVILEGE_SETTING_EVERYONE,
              "CNA GamerPrivilegeSetting identities must remain stable");
static_assert(sizeof(CNA_RacingCameraAngle) == sizeof(uint32_t) &&
                  CNA_RACING_CAMERA_ANGLE_BACK == UINT32_C(0) &&
                 CNA_RACING_CAMERA_ANGLE_INSIDE == UINT32_C(2) &&
                 CNA_RACING_CAMERA_ANGLE_MAXIMUM == CNA_RACING_CAMERA_ANGLE_INSIDE,
              "CNA RacingCameraAngle identities must remain stable");
static_assert(sizeof(CNA_CueInfo) == 16U && offsetof(CNA_CueInfo, is_created) == 8U &&
                  offsetof(CNA_CueInfo, is_stopping) == 15U &&
                  CNA_AUDIO_ENGINE_CONTENT_VERSION == INT32_C(46),
              "CNA_CueInfo layout and the XACT content version must remain stable");
static_assert(sizeof(CNA_AudioListener) == 56U && offsetof(CNA_AudioListener, forward) == 8U &&
                  offsetof(CNA_AudioListener, position) == 20U &&
                  offsetof(CNA_AudioListener, up) == 32U &&
                  offsetof(CNA_AudioListener, velocity) == 44U,
              "CNA_AudioListener layout must remain stable");
static_assert(sizeof(CNA_Blend) == sizeof(uint32_t));
static_assert(CNA_BLEND_ONE == UINT32_C(0));
static_assert(CNA_BLEND_SOURCE_ALPHA_SATURATION == UINT32_C(12));
static_assert(sizeof(CNA_BlendFunction) == sizeof(uint32_t));
static_assert(CNA_BLEND_FUNCTION_ADD == UINT32_C(0));
static_assert(CNA_BLEND_FUNCTION_MIN == UINT32_C(4));
static_assert(sizeof(CNA_ColorWriteChannels) == sizeof(uint32_t));
static_assert(CNA_COLOR_WRITE_NONE == UINT32_C(0));
static_assert(CNA_COLOR_WRITE_ALL == UINT32_C(15));
static_assert(sizeof(CNA_CompareFunction) == sizeof(uint32_t));
static_assert(CNA_COMPARE_ALWAYS == UINT32_C(0));
static_assert(CNA_COMPARE_NOT_EQUAL == UINT32_C(7));
static_assert(sizeof(CNA_StencilOperation) == sizeof(uint32_t));
static_assert(CNA_STENCIL_KEEP == UINT32_C(0));
static_assert(CNA_STENCIL_INVERT == UINT32_C(7));
static_assert(sizeof(CNA_CullMode) == sizeof(uint32_t));
static_assert(CNA_CULL_NONE == UINT32_C(0));
static_assert(CNA_CULL_COUNTER_CLOCKWISE_FACE == UINT32_C(2));
static_assert(sizeof(CNA_FillMode) == sizeof(uint32_t));
static_assert(CNA_FILL_SOLID == UINT32_C(0));
static_assert(CNA_FILL_WIREFRAME == UINT32_C(1));
static_assert(sizeof(CNA_TextureAddressMode) == sizeof(uint32_t));
static_assert(CNA_TEXTURE_ADDRESS_WRAP == UINT32_C(0));
static_assert(CNA_TEXTURE_ADDRESS_MIRROR == UINT32_C(2));
static_assert(sizeof(CNA_TextureFilter) == sizeof(uint32_t));
static_assert(CNA_TEXTURE_FILTER_LINEAR == UINT32_C(0));
static_assert(CNA_TEXTURE_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT == UINT32_C(8));
static_assert(sizeof(CNA_BlendState) == 56U);
static_assert(sizeof(CNA_DepthStencilState) == 64U);
static_assert(sizeof(CNA_RasterizerState) == 28U);
static_assert(sizeof(CNA_SamplerState) == 40U);
static_assert(sizeof(CNA_DepthFormat) == sizeof(uint32_t));
static_assert(CNA_DEPTH_FORMAT_NONE == UINT32_C(0));
static_assert(CNA_DEPTH_FORMAT_DEPTH24_STENCIL8 == UINT32_C(3));
static_assert(sizeof(CNA_RenderTargetUsage) == sizeof(uint32_t));
static_assert(CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS == UINT32_C(0));
static_assert(CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS == UINT32_C(2));
static_assert(sizeof(CNA_CubeMapFace) == sizeof(uint32_t));
static_assert(CNA_CUBE_MAP_FACE_POSITIVE_X == UINT32_C(0));
static_assert(CNA_CUBE_MAP_FACE_NEGATIVE_Z == UINT32_C(5));
static_assert(sizeof(CNA_RenderTarget2DCreateInfo) == 40U);
static_assert(sizeof(CNA_RenderTargetCubeCreateInfo) == 32U);
static_assert(sizeof(CNA_RenderTargetInfo) == 44U);
static_assert(sizeof(CNA_RenderTargetBinding) == 24U);
static_assert(sizeof(CNA_Texture3DCreateInfo) == 32U);
static_assert(sizeof(CNA_Texture3DInfo) == 32U);
static_assert(sizeof(CNA_Texture3DTransfer) == 56U);
static_assert(offsetof(CNA_Texture3DTransfer, start_index) == 40U);
static_assert(offsetof(CNA_Texture3DTransfer, element_count) == 48U);
static_assert(sizeof(CNA_TextureCubeCreateInfo) == 24U);
static_assert(sizeof(CNA_TextureCubeInfo) == 24U);
static_assert(sizeof(CNA_TextureCubeTransfer) == 56U);
static_assert(offsetof(CNA_TextureCubeTransfer, rectangle) == 20U);
static_assert(offsetof(CNA_TextureCubeTransfer, start_index) == 40U);
static_assert(sizeof(CNA_Char16) == sizeof(uint16_t));
static_assert(sizeof(CNA_SpriteFontGlyph) == 56U);
static_assert(sizeof(((CNA_SpriteFontCreateInfo*)nullptr)->glyphs) == sizeof(void*));
static_assert(sizeof(CNA_SpriteFontInfo) == 32U);
static_assert(sizeof(CNA_GraphicsProfile) == sizeof(uint32_t));
static_assert(CNA_GRAPHICS_PROFILE_REACH == UINT32_C(0));
static_assert(CNA_GRAPHICS_PROFILE_HI_DEF == UINT32_C(1));
static_assert(sizeof(CNA_PresentInterval) == sizeof(uint32_t));
static_assert(CNA_PRESENT_INTERVAL_DEFAULT == UINT32_C(0));
static_assert(CNA_PRESENT_INTERVAL_IMMEDIATE == UINT32_C(3));
static_assert(sizeof(CNA_DisplayOrientation) == sizeof(uint32_t));
static_assert(CNA_DISPLAY_ORIENTATION_DEFAULT == UINT32_C(0));
static_assert(CNA_DISPLAY_ORIENTATION_PORTRAIT == UINT32_C(4));
static_assert(sizeof(CNA_NativeHandleValue) == sizeof(uint64_t));
static_assert(sizeof(CNA_DisplayMode) == 24U);
static_assert(sizeof(CNA_GraphicsAdapterInfo) == 48U);
static_assert(sizeof(CNA_GraphicsFormatSelection) == 24U);
static_assert(sizeof(CNA_PresentationParameters) == 44U);
static_assert(sizeof(CNA_ContainmentType) == sizeof(uint32_t));
static_assert(CNA_CONTAINMENT_DISJOINT == UINT32_C(0));
static_assert(CNA_CONTAINMENT_CONTAINS == UINT32_C(1));
static_assert(CNA_CONTAINMENT_INTERSECTS == UINT32_C(2));
static_assert(sizeof(CNA_PlaneIntersectionType) == sizeof(uint32_t));
static_assert(CNA_PLANE_INTERSECTION_FRONT == UINT32_C(0));
static_assert(CNA_PLANE_INTERSECTION_BACK == UINT32_C(1));
static_assert(CNA_PLANE_INTERSECTION_INTERSECTING == UINT32_C(2));
static_assert(CNA_CURVE_CONTINUITY_SMOOTH == UINT32_C(0));
static_assert(CNA_CURVE_CONTINUITY_STEP == UINT32_C(1));
static_assert(CNA_CURVE_LOOP_CONSTANT == UINT32_C(0));
static_assert(CNA_CURVE_LOOP_CYCLE == UINT32_C(1));
static_assert(CNA_CURVE_LOOP_CYCLE_OFFSET == UINT32_C(2));
static_assert(CNA_CURVE_LOOP_OSCILLATE == UINT32_C(3));
static_assert(CNA_CURVE_LOOP_LINEAR == UINT32_C(4));
static_assert(CNA_CURVE_TANGENT_FLAT == UINT32_C(0));
static_assert(CNA_CURVE_TANGENT_LINEAR == UINT32_C(1));
static_assert(CNA_CURVE_TANGENT_SMOOTH == UINT32_C(2));
static_assert(sizeof(CNA_PackedVectorFormat) == sizeof(uint32_t));
static_assert(CNA_PACKED_VECTOR_FORMAT_ALPHA8 == UINT32_C(0));
static_assert(CNA_PACKED_VECTOR_FORMAT_BGR565 == UINT32_C(1));
static_assert(CNA_PACKED_VECTOR_FORMAT_BGRA4444 == UINT32_C(2));
static_assert(CNA_PACKED_VECTOR_FORMAT_BGRA5551 == UINT32_C(3));
static_assert(CNA_PACKED_VECTOR_FORMAT_BYTE4 == UINT32_C(4));
static_assert(CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE == UINT32_C(5));
static_assert(CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2 == UINT32_C(6));
static_assert(CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4 == UINT32_C(7));
static_assert(CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2 == UINT32_C(8));
static_assert(CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4 == UINT32_C(9));
static_assert(CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2 == UINT32_C(10));
static_assert(CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4 == UINT32_C(11));
static_assert(CNA_PACKED_VECTOR_FORMAT_RG32 == UINT32_C(12));
static_assert(CNA_PACKED_VECTOR_FORMAT_RGBA1010102 == UINT32_C(13));
static_assert(CNA_PACKED_VECTOR_FORMAT_RGBA64 == UINT32_C(14));
static_assert(CNA_PACKED_VECTOR_FORMAT_SHORT2 == UINT32_C(15));
static_assert(CNA_PACKED_VECTOR_FORMAT_SHORT4 == UINT32_C(16));
static_assert(sizeof(CNA_Point) == 8U);
static_assert(sizeof(CNA_Vector4) == 16U);
static_assert(sizeof(CNA_Quaternion) == 16U);
static_assert(alignof(CNA_Point) == 4U);
static_assert(alignof(CNA_Vector4) == 4U);
static_assert(alignof(CNA_Quaternion) == 4U);
static_assert(offsetof(CNA_Point, x) == 0U);
static_assert(offsetof(CNA_Point, y) == 4U);
static_assert(offsetof(CNA_Vector4, x) == 0U);
static_assert(offsetof(CNA_Vector4, y) == 4U);
static_assert(offsetof(CNA_Vector4, z) == 8U);
static_assert(offsetof(CNA_Vector4, w) == 12U);
static_assert(offsetof(CNA_Quaternion, x) == 0U);
static_assert(offsetof(CNA_Quaternion, y) == 4U);
static_assert(offsetof(CNA_Quaternion, z) == 8U);
static_assert(offsetof(CNA_Quaternion, w) == 12U);
static_assert(sizeof(CNA_Matrix) == 64U);
static_assert(sizeof(CNA_Plane) == 16U);
static_assert(sizeof(CNA_Ray) == 24U);
static_assert(offsetof(CNA_Matrix, m11) == 0U);
static_assert(offsetof(CNA_Matrix, m12) == 4U);
static_assert(offsetof(CNA_Matrix, m13) == 8U);
static_assert(offsetof(CNA_Matrix, m14) == 12U);
static_assert(offsetof(CNA_Matrix, m21) == 16U);
static_assert(offsetof(CNA_Matrix, m22) == 20U);
static_assert(offsetof(CNA_Matrix, m23) == 24U);
static_assert(offsetof(CNA_Matrix, m24) == 28U);
static_assert(offsetof(CNA_Matrix, m31) == 32U);
static_assert(offsetof(CNA_Matrix, m32) == 36U);
static_assert(offsetof(CNA_Matrix, m33) == 40U);
static_assert(offsetof(CNA_Matrix, m34) == 44U);
static_assert(offsetof(CNA_Matrix, m41) == 48U);
static_assert(offsetof(CNA_Matrix, m42) == 52U);
static_assert(offsetof(CNA_Matrix, m43) == 56U);
static_assert(offsetof(CNA_Matrix, m44) == 60U);
static_assert(offsetof(CNA_Plane, normal) == 0U);
static_assert(offsetof(CNA_Plane, d) == 12U);
static_assert(offsetof(CNA_Ray, position) == 0U);
static_assert(offsetof(CNA_Ray, direction) == 12U);
static_assert(sizeof(CNA_BoundingBox) == 24U);
static_assert(sizeof(CNA_BoundingSphere) == 16U);
static_assert(sizeof(CNA_BoundingFrustum) == 64U);
static_assert(offsetof(CNA_BoundingBox, min) == 0U);
static_assert(offsetof(CNA_BoundingBox, max) == 12U);
static_assert(offsetof(CNA_BoundingSphere, center) == 0U);
static_assert(offsetof(CNA_BoundingSphere, radius) == 12U);
static_assert(offsetof(CNA_BoundingFrustum, matrix) == 0U);
static_assert(sizeof(CNA_CurveKey) == 20U);
static_assert(sizeof(CNA_CurveKeyCollectionHandle) == 8U);
static_assert(sizeof(CNA_CurveHandle) == 8U);
static_assert(alignof(CNA_CurveKey) == 4U);
static_assert(offsetof(CNA_CurveKey, position) == 0U);
static_assert(offsetof(CNA_CurveKey, value) == 4U);
static_assert(offsetof(CNA_CurveKey, tangent_in) == 8U);
static_assert(offsetof(CNA_CurveKey, tangent_out) == 12U);
static_assert(offsetof(CNA_CurveKey, continuity) == 16U);
static_assert(sizeof(CNA_PackedAlpha8) == 1U);
static_assert(sizeof(CNA_PackedBgr565) == 2U);
static_assert(sizeof(CNA_PackedBgra4444) == 2U);
static_assert(sizeof(CNA_PackedBgra5551) == 2U);
static_assert(sizeof(CNA_PackedByte4) == 4U);
static_assert(sizeof(CNA_PackedHalfSingle) == 2U);
static_assert(sizeof(CNA_PackedHalfVector2) == 4U);
static_assert(sizeof(CNA_PackedHalfVector4) == 8U);
static_assert(sizeof(CNA_PackedNormalizedByte2) == 2U);
static_assert(sizeof(CNA_PackedNormalizedByte4) == 4U);
static_assert(sizeof(CNA_PackedNormalizedShort2) == 4U);
static_assert(sizeof(CNA_PackedNormalizedShort4) == 8U);
static_assert(sizeof(CNA_PackedRg32) == 4U);
static_assert(sizeof(CNA_PackedRgba1010102) == 4U);
static_assert(sizeof(CNA_PackedRgba64) == 8U);
static_assert(sizeof(CNA_PackedShort2) == 4U);
static_assert(sizeof(CNA_PackedShort4) == 8U);
static_assert(sizeof(CNA_BufferUsage) == sizeof(uint32_t));
static_assert(CNA_BUFFER_USAGE_NONE == UINT32_C(0));
static_assert(CNA_BUFFER_USAGE_WRITE_ONLY == UINT32_C(1));
static_assert(sizeof(CNA_IndexElementSize) == sizeof(uint32_t));
static_assert(CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS == UINT32_C(0));
static_assert(CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS == UINT32_C(1));
static_assert(sizeof(CNA_PrimitiveType) == sizeof(uint32_t));
static_assert(CNA_PRIMITIVE_TRIANGLE_LIST == UINT32_C(0));
static_assert(CNA_PRIMITIVE_TRIANGLE_STRIP == UINT32_C(1));
static_assert(CNA_PRIMITIVE_LINE_LIST == UINT32_C(2));
static_assert(CNA_PRIMITIVE_LINE_STRIP == UINT32_C(3));
static_assert(CNA_PRIMITIVE_POINT_LIST_EXT == UINT32_C(4));
static_assert(sizeof(CNA_SetDataOptions) == sizeof(uint32_t));
static_assert(CNA_SET_DATA_NONE == UINT32_C(0));
static_assert(CNA_SET_DATA_DISCARD == UINT32_C(1));
static_assert(CNA_SET_DATA_NO_OVERWRITE == UINT32_C(2));
static_assert(sizeof(CNA_VertexElementFormat) == sizeof(uint32_t));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_SINGLE == UINT32_C(0));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_VECTOR2 == UINT32_C(1));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_VECTOR3 == UINT32_C(2));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_VECTOR4 == UINT32_C(3));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_COLOR == UINT32_C(4));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_BYTE4 == UINT32_C(5));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_SHORT2 == UINT32_C(6));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_SHORT4 == UINT32_C(7));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT2 == UINT32_C(8));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT4 == UINT32_C(9));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR2 == UINT32_C(10));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4 == UINT32_C(11));
static_assert(sizeof(CNA_VertexElementUsage) == sizeof(uint32_t));
static_assert(CNA_VERTEX_ELEMENT_USAGE_POSITION == UINT32_C(0));
static_assert(CNA_VERTEX_ELEMENT_USAGE_COLOR == UINT32_C(1));
static_assert(CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE == UINT32_C(2));
static_assert(CNA_VERTEX_ELEMENT_USAGE_NORMAL == UINT32_C(3));
static_assert(CNA_VERTEX_ELEMENT_USAGE_BINORMAL == UINT32_C(4));
static_assert(CNA_VERTEX_ELEMENT_USAGE_TANGENT == UINT32_C(5));
static_assert(CNA_VERTEX_ELEMENT_USAGE_BLEND_INDICES == UINT32_C(6));
static_assert(CNA_VERTEX_ELEMENT_USAGE_BLEND_WEIGHT == UINT32_C(7));
static_assert(CNA_VERTEX_ELEMENT_USAGE_DEPTH == UINT32_C(8));
static_assert(CNA_VERTEX_ELEMENT_USAGE_FOG == UINT32_C(9));
static_assert(CNA_VERTEX_ELEMENT_USAGE_POINT_SIZE == UINT32_C(10));
static_assert(CNA_VERTEX_ELEMENT_USAGE_SAMPLE == UINT32_C(11));
static_assert(CNA_VERTEX_ELEMENT_USAGE_TESSELLATE_FACTOR == UINT32_C(12));
static_assert(sizeof(CNA_VertexElement) == 16U);
static_assert(alignof(CNA_VertexElement) == 4U);
static_assert(offsetof(CNA_VertexElement, offset) == 0U);
static_assert(offsetof(CNA_VertexElement, format) == 4U);
static_assert(offsetof(CNA_VertexElement, usage) == 8U);
static_assert(offsetof(CNA_VertexElement, usage_index) == 12U);
static_assert(sizeof(CNA_VertexType) == sizeof(uint32_t));
static_assert(CNA_VERTEX_TYPE_POSITION_COLOR == UINT32_C(0));
static_assert(CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE == UINT32_C(1));
static_assert(CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE == UINT32_C(2));
static_assert(CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED == UINT32_C(3));
static_assert(CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE == UINT32_C(4));
static_assert(CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED == UINT32_C(5));
static_assert(CNA_VERTEX_TYPE_POSITION_TEXTURE == UINT32_C(6));
static_assert(sizeof(CNA_VertexPositionColor) == 16U);
static_assert(sizeof(CNA_VertexPositionColorTexture) == 24U);
static_assert(sizeof(CNA_VertexPositionNormalTangentTexture) == 48U);
static_assert(sizeof(CNA_VertexPositionNormalTangentTextureSkinned) == 68U);
static_assert(sizeof(CNA_VertexPositionNormalTexture) == 32U);
static_assert(sizeof(CNA_VertexPositionNormalTextureSkinned) == 52U);
static_assert(sizeof(CNA_VertexPositionTexture) == 20U);
static_assert(sizeof(CNA_VertexValue) == 68U);
static_assert(alignof(CNA_VertexValue) == 4U);
static_assert(offsetof(CNA_VertexPositionColor, color) == 12U);
static_assert(offsetof(CNA_VertexPositionColorTexture, texture_coordinate) == 16U);
static_assert(offsetof(CNA_VertexPositionNormalTangentTexture, tangent) == 24U);
static_assert(offsetof(CNA_VertexPositionNormalTangentTexture, texture_coordinate) == 40U);
static_assert(offsetof(CNA_VertexPositionNormalTangentTextureSkinned, blend_weight) == 48U);
static_assert(offsetof(CNA_VertexPositionNormalTangentTextureSkinned, blend_indices) == 64U);
static_assert(offsetof(CNA_VertexPositionNormalTexture, texture_coordinate) == 24U);
static_assert(offsetof(CNA_VertexPositionNormalTextureSkinned, blend_weight) == 32U);
static_assert(offsetof(CNA_VertexPositionNormalTextureSkinned, blend_indices) == 48U);
static_assert(offsetof(CNA_VertexPositionTexture, texture_coordinate) == 12U);
static_assert(sizeof(CNA_VertexDeclarationHandle) == 8U);
static_assert(sizeof(CNA_VertexBufferHandle) == 8U);
static_assert(sizeof(CNA_VertexBufferEventRegistrationHandle) == 8U);
static_assert(sizeof(CNA_VertexBufferCreateInfo) == 32U);
static_assert(alignof(CNA_VertexBufferCreateInfo) == 8U);
static_assert(offsetof(CNA_VertexBufferCreateInfo, vertex_declaration) == 8U);
static_assert(offsetof(CNA_VertexBufferCreateInfo, vertex_count) == 16U);
static_assert(offsetof(CNA_VertexBufferCreateInfo, dynamic) == 24U);
static_assert(sizeof(CNA_VertexBufferInfo) == 32U);
static_assert(alignof(CNA_VertexBufferInfo) == 8U);
static_assert(offsetof(CNA_VertexBufferInfo, vertex_stride) == 20U);
static_assert(offsetof(CNA_VertexBufferInfo, vertex_element_count) == 24U);
static_assert(sizeof(CNA_VertexBufferTransfer) == 32U);
static_assert(alignof(CNA_VertexBufferTransfer) == 8U);
static_assert(offsetof(CNA_VertexBufferTransfer, start_index) == 16U);
static_assert(offsetof(CNA_VertexBufferTransfer, element_count) == 24U);
static_assert(sizeof(CNA_IndexBufferHandle) == 8U);
static_assert(sizeof(CNA_IndexBufferEventRegistrationHandle) == 8U);
static_assert(sizeof(CNA_IndexBufferCreateInfo) == 24U);
static_assert(alignof(CNA_IndexBufferCreateInfo) == 4U);
static_assert(offsetof(CNA_IndexBufferCreateInfo, index_count) == 8U);
static_assert(offsetof(CNA_IndexBufferCreateInfo, index_element_size) == 12U);
static_assert(offsetof(CNA_IndexBufferCreateInfo, dynamic) == 20U);
static_assert(sizeof(CNA_IndexBufferInfo) == 24U);
static_assert(alignof(CNA_IndexBufferInfo) == 4U);
static_assert(offsetof(CNA_IndexBufferInfo, index_count) == 8U);
static_assert(offsetof(CNA_IndexBufferInfo, dynamic) == 20U);
static_assert(sizeof(CNA_IndexBufferTransfer) == 32U);
static_assert(alignof(CNA_IndexBufferTransfer) == 8U);
static_assert(offsetof(CNA_IndexBufferTransfer, start_index) == 16U);
static_assert(offsetof(CNA_IndexBufferTransfer, element_count) == 24U);
static_assert(sizeof(CNA_VertexBufferBinding) == 16U);
static_assert(alignof(CNA_VertexBufferBinding) == 8U);
static_assert(offsetof(CNA_VertexBufferBinding, vertex_buffer) == 0U);
static_assert(offsetof(CNA_VertexBufferBinding, vertex_offset) == 8U);
static_assert(offsetof(CNA_VertexBufferBinding, instance_frequency) == 12U);
static_assert(CNA_MATH_E == 2.71828175F);
static_assert(CNA_MATH_LOG10_E == 0.4342945F);
static_assert(CNA_MATH_LOG2_E == 1.442695F);
static_assert(CNA_MATH_PI == 3.14159274F);
static_assert(CNA_MATH_PI_OVER_2 == 1.57079637F);
static_assert(CNA_MATH_PI_OVER_4 == 0.7853982F);
static_assert(CNA_MATH_TWO_PI == 6.28318548F);
static_assert(CNA_MATH_MACHINE_EPSILON_FLOAT == 5.96046448E-8F);
static_assert(sizeof(CNA_Viewport) == 24U);
static_assert(alignof(CNA_Viewport) == 4U);
static_assert(offsetof(CNA_Viewport, x) == 0U);
static_assert(offsetof(CNA_Viewport, y) == 4U);
static_assert(offsetof(CNA_Viewport, width) == 8U);
static_assert(offsetof(CNA_Viewport, height) == 12U);
static_assert(offsetof(CNA_Viewport, min_depth) == 16U);
static_assert(offsetof(CNA_Viewport, max_depth) == 20U);
static_assert(sizeof(CNA_ClearOptions) == sizeof(uint32_t));
static_assert(CNA_CLEAR_OPTION_TARGET == UINT32_C(1));
static_assert(CNA_CLEAR_OPTION_DEPTH_BUFFER == UINT32_C(2));
static_assert(CNA_CLEAR_OPTION_STENCIL == UINT32_C(4));
static_assert(sizeof(CNA_GraphicsDeviceStatus) == sizeof(uint32_t));
static_assert(CNA_GRAPHICS_DEVICE_STATUS_NORMAL == UINT32_C(0));
static_assert(CNA_GRAPHICS_DEVICE_STATUS_LOST == UINT32_C(1));
static_assert(CNA_GRAPHICS_DEVICE_STATUS_NOT_RESET == UINT32_C(2));
static_assert(sizeof(CNA_Unsupported3DGraphicsCallBehavior) == sizeof(uint32_t));
static_assert(CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW == UINT32_C(0));
static_assert(CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB == UINT32_C(1));
static_assert(sizeof(CNA_SpriteEffects) == sizeof(uint32_t));
static_assert((CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY) == UINT32_C(3));
static_assert(((CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY) &
               CNA_SPRITE_EFFECT_FLIP_VERTICALLY) == CNA_SPRITE_EFFECT_FLIP_VERTICALLY);
static_assert(sizeof(CNA_GraphicsDeviceEventRegistrationHandle) == 8U);
static_assert(sizeof(CNA_GraphicsDeviceEvent) == sizeof(uint32_t));
static_assert(CNA_GRAPHICS_DEVICE_EVENT_DISPOSING == UINT32_C(0));
static_assert(CNA_GRAPHICS_DEVICE_EVENT_DEVICE_LOST == UINT32_C(1));
static_assert(CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESET == UINT32_C(2));
static_assert(CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESETTING == UINT32_C(3));
static_assert(sizeof(CNA_ResourceCreatedEventInfo) == 16U);
static_assert(alignof(CNA_ResourceCreatedEventInfo) == 4U);
static_assert(offsetof(CNA_ResourceCreatedEventInfo, has_resource) == 8U);
static_assert(offsetof(CNA_ResourceCreatedEventInfo, reserved) == 9U);
static_assert(sizeof(CNA_ResourceDestroyedEventInfo) == 32U);
static_assert(alignof(CNA_ResourceDestroyedEventInfo) == 8U);
static_assert(offsetof(CNA_ResourceDestroyedEventInfo, has_tag) == 8U);
static_assert(offsetof(CNA_ResourceDestroyedEventInfo, name) == 16U);
static_assert(CNA_TEXTURE_COLLECTION_MAX_TEXTURES == UINT32_C(16));
static_assert(sizeof(CNA_TextureSlotInfo) == 24U);
static_assert(alignof(CNA_TextureSlotInfo) == 8U);
static_assert(offsetof(CNA_TextureSlotInfo, bound) == 8U);
static_assert(offsetof(CNA_TextureSlotInfo, reserved) == 9U);
static_assert(offsetof(CNA_TextureSlotInfo, texture) == 16U);
static_assert(sizeof(CNA_BackBufferReadback) == 48U);
static_assert(alignof(CNA_BackBufferReadback) == 8U);
static_assert(offsetof(CNA_BackBufferReadback, has_source_rectangle) == 8U);
static_assert(offsetof(CNA_BackBufferReadback, source_rectangle) == 12U);
static_assert(offsetof(CNA_BackBufferReadback, start_index) == 32U);
static_assert(offsetof(CNA_BackBufferReadback, element_count) == 40U);
static_assert(sizeof(CNA_UserVertexSource) == sizeof(uint32_t));
static_assert(CNA_USER_VERTEX_SOURCE_RAW_STREAM == UINT32_C(0));
static_assert(CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE == UINT32_C(4));
static_assert(sizeof(CNA_UserPrimitives) == 48U);
static_assert(alignof(CNA_UserPrimitives) == 8U);
static_assert(offsetof(CNA_UserPrimitives, vertex_data) == 16U);
static_assert(offsetof(CNA_UserPrimitives, vertex_declaration) == 24U);
static_assert(offsetof(CNA_UserPrimitives, vertex_offset) == 32U);
static_assert(sizeof(CNA_UserIndices) == 24U);
static_assert(alignof(CNA_UserIndices) == 8U);
static_assert(offsetof(CNA_UserIndices, index_element_size) == 8U);
static_assert(offsetof(CNA_UserIndices, index_data) == 16U);
static_assert(sizeof(CNA_OcclusionQueryHandle) == 8U);
static_assert(sizeof(CNA_SpriteTextCommand) == 72U);
static_assert(alignof(CNA_SpriteTextCommand) == 8U);
static_assert(offsetof(CNA_SpriteTextCommand, sprite_font) == 8U);
static_assert(offsetof(CNA_SpriteTextCommand, text) == 16U);
static_assert(offsetof(CNA_SpriteTextCommand, position) == 32U);
static_assert(sizeof(CNA_SpriteMeshEXT) == 64U);
static_assert(alignof(CNA_SpriteMeshEXT) == 8U);
static_assert(offsetof(CNA_SpriteMeshEXT, effect) == 8U);
static_assert(offsetof(CNA_SpriteMeshEXT, positions) == 16U);
static_assert(offsetof(CNA_SpriteMeshEXT, vertex_count) == 48U);
static_assert(sizeof(CNA_AsciiPostProcessEffectHandle) == 8U);
static_assert(sizeof(CNA_AsciiQuantizeMode) == sizeof(uint32_t));
static_assert(sizeof(CNA_CRTMaskType) == sizeof(uint32_t));
static_assert(sizeof(CNA_DitherMode) == sizeof(uint32_t));
static_assert(sizeof(CNA_RenderQuality) == sizeof(uint32_t));
static_assert(sizeof(CNA_ShadowQuality) == sizeof(uint32_t));
static_assert(sizeof(CNA_TonemappingMode) == sizeof(uint32_t));
static_assert(sizeof(CNA_DepthEffectMode) == sizeof(uint32_t));
static_assert(CNA_CRT_MASK_TYPE_SHADOW_MASK == UINT32_C(2));
static_assert(CNA_DEPTH_EFFECT_MODE_PALETTE_16 == UINT32_C(6));
static_assert(CNA_SHADOW_QUALITY_ULTRA == UINT32_C(4));
static_assert(sizeof(CNA_PbrMaterial) == 72U);
static_assert(alignof(CNA_PbrMaterial) == 8U);
static_assert(offsetof(CNA_PbrMaterial, albedo_color) == 40U);
static_assert(offsetof(CNA_PbrMaterial, emissive_color) == 44U);
static_assert(offsetof(CNA_PbrMaterial, metallic_factor) == 48U);
static_assert(offsetof(CNA_PbrMaterial, alpha_blend_enabled) == 68U);
static_assert(offsetof(CNA_PbrMaterial, reserved) == 69U);
static_assert(sizeof(CNA_RenderPipelineSettings) == 28U);
static_assert(alignof(CNA_RenderPipelineSettings) == 4U);
static_assert(offsetof(CNA_RenderPipelineSettings, tonemapping_mode) == 12U);
static_assert(offsetof(CNA_RenderPipelineSettings, render_quality) == 16U);
static_assert(offsetof(CNA_RenderPipelineSettings, hdr_enabled) == 24U);
static_assert(offsetof(CNA_RenderPipelineSettings, shadows_enabled) == 27U);
static_assert(sizeof(CNA_StorageDeviceHandle) == 8U);
static_assert(sizeof(CNA_StorageContainerHandle) == 8U);
static_assert(sizeof(CNA_StorageStreamHandle) == 8U);
static_assert(sizeof(CNA_FileMode) == sizeof(uint32_t));
static_assert(sizeof(CNA_FileAccess) == sizeof(uint32_t));
static_assert(sizeof(CNA_FileShare) == sizeof(uint32_t));
static_assert(sizeof(CNA_SeekOrigin) == sizeof(uint32_t));
static_assert(CNA_FILE_MODE_CREATE_NEW == UINT32_C(1));
static_assert(CNA_FILE_MODE_APPEND == UINT32_C(6));
static_assert(CNA_FILE_ACCESS_READ_WRITE == UINT32_C(3));
static_assert(CNA_FILE_SHARE_NONE == UINT32_C(0));
static_assert(CNA_FILE_SHARE_INHERITABLE == UINT32_C(16));
static_assert(CNA_SEEK_ORIGIN_END == UINT32_C(2));
static_assert(sizeof(CNA_ContentManifestEntryInfo) == 32U);
static_assert(alignof(CNA_ContentManifestEntryInfo) == 8U);
static_assert(offsetof(CNA_ContentManifestEntryInfo, has_xnb) == 8U);
static_assert(offsetof(CNA_ContentManifestEntryInfo, has_cnj) == 9U);
static_assert(offsetof(CNA_ContentManifestEntryInfo, reserved) == 10U);
static_assert(offsetof(CNA_ContentManifestEntryInfo, native_extension_count) == 16U);
static_assert(offsetof(CNA_ContentManifestEntryInfo, xnb_reader_name_count) == 24U);
static_assert(sizeof(CNA_ContentReaderUsageInfo) == 24U);
static_assert(alignof(CNA_ContentReaderUsageInfo) == 8U);
static_assert(offsetof(CNA_ContentReaderUsageInfo, is_registered) == 8U);
static_assert(offsetof(CNA_ContentReaderUsageInfo, reserved) == 9U);
static_assert(offsetof(CNA_ContentReaderUsageInfo, file_count) == 16U);
static_assert(sizeof(CNA_ContentReaderHandle) == 8U);
static_assert(sizeof(CNA_ContentTypeReaderHandle) == 8U);
static_assert(sizeof(CNA_UnsupportedContentReaderReason) == sizeof(uint32_t));
static_assert(CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE ==
              UINT32_C(0));
static_assert(sizeof(CNA_ContentReaderCreateInfo) == 48U);
static_assert(alignof(CNA_ContentReaderCreateInfo) == 8U);
static_assert(offsetof(CNA_ContentReaderCreateInfo, content_manager) == 8U);
static_assert(offsetof(CNA_ContentReaderCreateInfo, stream) == 16U);
static_assert(offsetof(CNA_ContentReaderCreateInfo, asset_name) == 24U);
static_assert(offsetof(CNA_ContentReaderCreateInfo, version) == 40U);
static_assert(offsetof(CNA_ContentReaderCreateInfo, platform) == 44U);
static_assert(offsetof(CNA_ContentReaderCreateInfo, reserved) == 45U);
static_assert(sizeof(CNA_NetworkSessionEndReason) == sizeof(uint32_t));
static_assert(sizeof(CNA_NetworkSessionJoinError) == sizeof(uint32_t));
static_assert(sizeof(CNA_NetworkSessionState) == sizeof(uint32_t));
static_assert(sizeof(CNA_NetworkSessionType) == sizeof(uint32_t));
static_assert(sizeof(CNA_SendDataOptions) == sizeof(uint32_t));
static_assert(sizeof(CNA_NetworkSessionPropertiesHandle) == 8U);
static_assert(sizeof(CNA_NetworkSessionPropertyEnumeratorHandle) == 8U);
static_assert(sizeof(CNA_PacketWriterHandle) == 8U);
static_assert(sizeof(CNA_PacketReaderHandle) == 8U);
static_assert(CNA_NETWORK_SESSION_END_REASON_DISCONNECTED == UINT32_C(3));
static_assert(CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_FULL == UINT32_C(2));
static_assert(CNA_NETWORK_SESSION_STATE_ENDED == UINT32_C(2));
static_assert(CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS == UINT32_C(4));
static_assert(CNA_SEND_DATA_OPTIONS_CHAT == UINT32_C(4));
static_assert(sizeof(CNA_QualityOfService) == 40U);
static_assert(alignof(CNA_QualityOfService) == 8U);
static_assert(offsetof(CNA_QualityOfService, is_available) == 8U);
static_assert(offsetof(CNA_QualityOfService, reserved) == 9U);
static_assert(offsetof(CNA_QualityOfService, average_roundtrip_ticks) == 16U);
static_assert(offsetof(CNA_QualityOfService, minimum_roundtrip_ticks) == 24U);
static_assert(offsetof(CNA_QualityOfService, bytes_per_second_downstream) == 32U);
static_assert(offsetof(CNA_QualityOfService, bytes_per_second_upstream) == 36U);
static_assert(sizeof(CNA_OptionalInt32) == 8U);
static_assert(alignof(CNA_OptionalInt32) == 4U);
static_assert(offsetof(CNA_OptionalInt32, has_value) == 0U);
static_assert(offsetof(CNA_OptionalInt32, reserved) == 1U);
static_assert(offsetof(CNA_OptionalInt32, value) == 4U);
static_assert(sizeof(CNA_NetworkGamerHandle) == 8U);
static_assert(sizeof(CNA_NetworkMachineHandle) == 8U);
static_assert(sizeof(CNA_GameEndedEventInfo) == 8U);
static_assert(sizeof(CNA_GameStartedEventInfo) == 8U);
static_assert(sizeof(CNA_GamerJoinedEventInfo) == 16U);
static_assert(alignof(CNA_GamerJoinedEventInfo) == 8U);
static_assert(offsetof(CNA_GamerJoinedEventInfo, gamer) == 8U);
static_assert(sizeof(CNA_GamerLeftEventInfo) == 16U);
static_assert(offsetof(CNA_GamerLeftEventInfo, gamer) == 8U);
static_assert(sizeof(CNA_HostChangedEventInfo) == 24U);
static_assert(offsetof(CNA_HostChangedEventInfo, old_host) == 8U);
static_assert(offsetof(CNA_HostChangedEventInfo, new_host) == 16U);
static_assert(sizeof(CNA_NetworkSessionEndedEventInfo) == 16U);
static_assert(alignof(CNA_NetworkSessionEndedEventInfo) == 4U);
static_assert(offsetof(CNA_NetworkSessionEndedEventInfo, end_reason) == 8U);
static_assert(offsetof(CNA_NetworkSessionEndedEventInfo, reserved) == 12U);
static_assert(sizeof(CNA_WriteLeaderboardsEventInfo) == 24U);
static_assert(alignof(CNA_WriteLeaderboardsEventInfo) == 8U);
static_assert(offsetof(CNA_WriteLeaderboardsEventInfo, gamer) == 8U);
static_assert(offsetof(CNA_WriteLeaderboardsEventInfo, is_leaving) == 16U);
static_assert(offsetof(CNA_WriteLeaderboardsEventInfo, reserved) == 17U);
static_assert(sizeof(CNA_AvailableNetworkSessionHandle) == 8U);
static_assert(sizeof(CNA_AvailableNetworkSessionCollectionHandle) == 8U);
static_assert(sizeof(CNA_AvailableNetworkSessionCreateInfo) == 72U);
static_assert(alignof(CNA_AvailableNetworkSessionCreateInfo) == 8U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, current_gamer_count) == 8U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, open_private_gamer_slots) == 12U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, open_public_gamer_slots) == 16U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, session_type) == 20U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, host_port) == 24U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, reserved) == 26U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, host_gamertag) == 32U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, host_address) == 48U);
static_assert(offsetof(CNA_AvailableNetworkSessionCreateInfo, session_properties) == 64U);
static_assert(sizeof(CNA_NetworkSessionHandle) == 8U);
static_assert(sizeof(CNA_SignedInGamerHandle) == 8U);
static_assert(sizeof(CNA_NetworkEventType) == sizeof(uint32_t));
static_assert(CNA_NETWORK_SESSION_MAX_SUPPORTED_GAMERS == INT32_C(31));
static_assert(CNA_NETWORK_SESSION_MAX_PREVIOUS_GAMERS == INT32_C(100));
static_assert(CNA_NETWORK_EVENT_TYPE_PACKET_SEND == UINT32_C(0));
static_assert(CNA_NETWORK_EVENT_TYPE_STATE_CHANGE == UINT32_C(4));
static_assert(CNA_NETWORK_SESSION_ROSTER_ALL == UINT32_C(0));
static_assert(CNA_NETWORK_SESSION_ROSTER_PREVIOUS == UINT32_C(3));
static_assert(sizeof(CNA_NetworkEventInfo) == 56U);
static_assert(alignof(CNA_NetworkEventInfo) == 8U);
static_assert(offsetof(CNA_NetworkEventInfo, type) == 8U);
static_assert(offsetof(CNA_NetworkEventInfo, reliable) == 12U);
static_assert(offsetof(CNA_NetworkEventInfo, state) == 16U);
static_assert(offsetof(CNA_NetworkEventInfo, reason) == 20U);
static_assert(offsetof(CNA_NetworkEventInfo, gamer) == 24U);
static_assert(offsetof(CNA_NetworkEventInfo, sender) == 32U);
static_assert(offsetof(CNA_NetworkEventInfo, packet) == 40U);
static_assert(offsetof(CNA_NetworkEventInfo, packet_byte_count) == 48U);
static_assert(sizeof(CNA_NetworkSessionEventRegistrationHandle) == 8U);
static_assert(sizeof(CNA_InviteAcceptedEventInfo) == 24U);
static_assert(alignof(CNA_InviteAcceptedEventInfo) == 8U);
static_assert(offsetof(CNA_InviteAcceptedEventInfo, gamer) == 8U);
static_assert(offsetof(CNA_InviteAcceptedEventInfo, is_current_session) == 16U);
static_assert(offsetof(CNA_InviteAcceptedEventInfo, reserved) == 17U);

static_assert(sizeof(CNA_LogLevel) == sizeof(uint32_t));
static_assert(sizeof(CNA_LogCategory) == sizeof(uint32_t));
static_assert(sizeof(CNA_Platform) == sizeof(uint32_t));
static_assert(sizeof(CNA_DesktopOS) == sizeof(uint32_t));
static_assert(sizeof(CNA_GraphicsBackendCategory) == sizeof(uint32_t));
static_assert(sizeof(CNA_GraphicsBackendMaturity) == sizeof(uint32_t));
static_assert(CNA_LOG_LEVEL_EXPERIMENT == UINT32_C(100));
static_assert(CNA_LOG_CATEGORY_GPU == UINT32_C(8));
static_assert(CNA_PLATFORM_WEB == UINT32_C(3));
static_assert(CNA_DESKTOP_OS_OTHER == UINT32_C(3));
static_assert(CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC == UINT32_C(4));
static_assert(CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED == UINT32_C(4));

static_assert(sizeof(CNA_GamePadType) == sizeof(uint32_t));
static_assert(CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD == UINT32_C(9));
static_assert(sizeof(CNA_GamePadCapabilities) == 48U);
static_assert(alignof(CNA_GamePadCapabilities) == 4U);
static_assert(offsetof(CNA_GamePadCapabilities, gamepad_type) == 8U);
static_assert(offsetof(CNA_GamePadCapabilities, is_connected) == 12U);
static_assert(offsetof(CNA_GamePadCapabilities, has_accelerometer_ext) == 46U);
static_assert(offsetof(CNA_GamePadCapabilities, reserved) == 47U);

static_assert(sizeof(CNA_GamePadThumbSticks) == 16U);
static_assert(sizeof(CNA_GamePadTriggers) == 8U);
static_assert(offsetof(CNA_GamePadThumbSticks, right) == 8U);
static_assert(offsetof(CNA_GamePadTriggers, right) == 4U);
static_assert(sizeof(CNA_GamePadAnalogState) ==
              sizeof(CNA_GamePadThumbSticks) + sizeof(CNA_GamePadTriggers));

static_assert(sizeof(CNA_GamePadButtonLabel) == sizeof(uint32_t));
static_assert(sizeof(CNA_GamePadConnectionState) == sizeof(uint32_t));
static_assert(sizeof(CNA_PowerState) == sizeof(uint32_t));
static_assert(CNA_GAMEPAD_BUTTON_LABEL_TRIANGLE == UINT32_C(8));
static_assert(CNA_GAMEPAD_CONNECTION_STATE_WIRELESS == UINT32_C(2));
static_assert(CNA_POWER_STATE_CHARGED == UINT32_C(5));
static_assert(sizeof(CNA_GamePadTouchpadFinger) == 16U);
static_assert(offsetof(CNA_GamePadTouchpadFinger, pressure) == 12U);

static_assert(sizeof(CNA_KeyState) == sizeof(uint32_t));
static_assert(sizeof(CNA_KeyModifiers) == sizeof(uint32_t));
static_assert(CNA_KEY_STATE_DOWN == UINT32_C(1));
static_assert(CNA_KEY_MODIFIER_MODE == UINT32_C(0x80));
static_assert(CNA_KEY_MODIFIER_ALL == UINT32_C(0xFF));

static_assert(sizeof(CNA_MouseCursorHandle) == 8U);
static_assert(sizeof(CNA_MouseEventRegistrationHandle) == 8U);
static_assert(CNA_MOUSE_CURSOR_STOCK_WAIT_ARROW == UINT32_C(11));

static_assert(sizeof(CNA_TextInputRegistrationHandle) == 8U);
static_assert(sizeof(CNA_TextInputType) == sizeof(uint32_t));
static_assert(CNA_TEXT_INPUT_TYPE_TEXT == UINT32_C(0));
static_assert(CNA_TEXT_INPUT_TYPE_NUMBER == UINT32_C(6));
static_assert(CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_VISIBLE == UINT32_C(8));
static_assert(CNA_TEXT_INPUT_TYPE_MAXIMUM == UINT32_C(8));
static_assert(sizeof(CNA_TextEditingEventInfo) == 32U);
static_assert(offsetof(CNA_TextEditingEventInfo, text) == 8U);
static_assert(offsetof(CNA_TextEditingEventInfo, length) == 28U);
static_assert(sizeof(CNA_TextEditingCandidatesEventInfo) == 32U);
static_assert(offsetof(CNA_TextEditingCandidatesEventInfo, candidates) == 8U);
static_assert(offsetof(CNA_TextEditingCandidatesEventInfo, horizontal) == 24U);

static_assert(sizeof(CNA_GestureType) == sizeof(uint32_t));
static_assert(CNA_GESTURE_TYPE_NONE == UINT32_C(0));
static_assert(CNA_GESTURE_TYPE_PINCH_COMPLETE == UINT32_C(512));
static_assert(CNA_GESTURE_TYPE_ALL == UINT32_C(0x000003FF));
static_assert(CNA_TOUCH_NO_FINGER == INT32_C(-1));
static_assert(sizeof(CNA_GestureSample) == 64U);
static_assert(alignof(CNA_GestureSample) == 8U);
static_assert(offsetof(CNA_GestureSample, timestamp_ticks) == 24U);
static_assert(offsetof(CNA_GestureSample, delta2) == 56U);
static_assert(sizeof(CNA_TouchState) == 16U + (sizeof(CNA_TouchLocation) * CNA_TOUCH_MAX_TOUCHES));

static_assert(sizeof(CNA_HapticDeviceHandle) == 8U);
static_assert(sizeof(CNA_HapticFeature) == sizeof(uint32_t));
static_assert(CNA_HAPTIC_FEATURE_ALL == UINT32_C(0x000F8FFF));
static_assert(CNA_HAPTIC_EFFECT_TYPE_MAXIMUM == UINT32_C(12));
static_assert(CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM == UINT32_C(3));
static_assert(CNA_HAPTIC_EFFECT_INFINITE_LENGTH == UINT32_C(4294967295));
static_assert(sizeof(CNA_HapticDirection) == 16U);
static_assert(sizeof(CNA_HapticCapabilities) == 28U);
static_assert(offsetof(CNA_HapticCapabilities, max_effects) == 16U);
static_assert(sizeof(CNA_HapticEffect) == 108U);
static_assert(alignof(CNA_HapticEffect) == 4U);
static_assert(offsetof(CNA_HapticEffect, direction) == 16U);
static_assert(offsetof(CNA_HapticEffect, fade_level) == 106U);

static_assert(sizeof(CNA_JoystickStateHandle) == 8U);
static_assert(sizeof(CNA_JoystickEventRegistrationHandle) == 8U);
static_assert(sizeof(CNA_JoystickType) == sizeof(uint32_t));
static_assert(CNA_JOYSTICK_TYPE_MAXIMUM == UINT32_C(9));
static_assert(sizeof(CNA_JoystickHatPosition) == sizeof(uint32_t));
static_assert(CNA_JOYSTICK_HAT_POSITION_RIGHT_UP == UINT32_C(5));
static_assert(CNA_JOYSTICK_HAT_POSITION_MAXIMUM == UINT32_C(8));
static_assert(sizeof(CNA_JoystickInfo) == 16U);
static_assert(offsetof(CNA_JoystickInfo, type) == 12U);
static_assert(sizeof(CNA_JoystickCapabilities) == 40U);
static_assert(alignof(CNA_JoystickCapabilities) == 4U);
static_assert(offsetof(CNA_JoystickCapabilities, power_percent) == 32U);
static_assert(offsetof(CNA_JoystickCapabilities, reserved) == 37U);

static_assert(sizeof(CNA_InputDeviceEventRegistrationHandle) == 8U);
static_assert(sizeof(CNA_SensorType) == sizeof(uint32_t));
static_assert(CNA_SENSOR_TYPE_GYROSCOPE == UINT32_C(2));
static_assert(CNA_SENSOR_TYPE_MAXIMUM == UINT32_C(6));
static_assert(sizeof(CNA_SensorInfo) == 16U);
static_assert(alignof(CNA_SensorInfo) == 4U);
static_assert(offsetof(CNA_SensorInfo, type) == 12U);
static_assert(sizeof(CNA_InputDeviceInfo) == 16U);
static_assert(alignof(CNA_InputDeviceInfo) == 8U);
static_assert(offsetof(CNA_InputDeviceInfo, id) == 8U);

static_assert(sizeof(CNA_MediaState) == sizeof(uint32_t));
static_assert(CNA_MEDIA_STATE_MAXIMUM == UINT32_C(2));
static_assert(CNA_MEDIA_SOURCE_TYPE_WINDOWS_MEDIA_CONNECT == UINT32_C(4));
static_assert(CNA_VIDEO_SOUNDTRACK_TYPE_MAXIMUM == UINT32_C(2));
static_assert(CNA_VISUALIZATION_DATA_SIZE == UINT32_C(256));
static_assert(sizeof(CNA_VisualizationData) == 2056U);
static_assert(offsetof(CNA_VisualizationData, samples) == 1032U);

static_assert(sizeof(CNA_SensorState) == sizeof(uint32_t));
static_assert(CNA_SENSOR_STATE_MAXIMUM == UINT32_C(5));
static_assert(sizeof(CNA_DateTimeOffset) == 16U);
static_assert(alignof(CNA_DateTimeOffset) == 8U);
static_assert(sizeof(CNA_AccelerometerReading) == 40U);
static_assert(sizeof(CNA_GyroscopeReading) == 40U);
static_assert(sizeof(CNA_AttitudeReading) == 120U);
static_assert(offsetof(CNA_AttitudeReading, rotation_matrix) == 52U);
static_assert(sizeof(CNA_CompassReading) == 64U);
static_assert(sizeof(CNA_MotionReading) == 184U);
static_assert(offsetof(CNA_MotionReading, gravity) == 168U);
static_assert(sizeof(CNA_PowerState) == sizeof(uint32_t));
static_assert(CNA_POWER_STATE_MAXIMUM == UINT32_C(5));
static_assert(CNA_MESSAGE_BOX_TYPE_MAXIMUM == UINT32_C(2));
static_assert(sizeof(CNA_VibrationTestLog) == 48U);
static_assert(alignof(CNA_VibrationTestLog) == 8U);
static_assert(sizeof(CNA_MessageBoxTestLog) == 24U);
static_assert(CNA_MICROPHONE_STATE_MAXIMUM == UINT32_C(1));
static_assert(CNA_AUDIO_STOP_OPTIONS_MAXIMUM == UINT32_C(1));
static_assert(CNA_PRESENTATION_MODE_MAXIMUM == UINT32_C(4));
static_assert(CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM == UINT32_C(4));
static_assert(offsetof(CNA_GraphicsDeviceInformation, presentation_parameters) == 16U);
static_assert(CNA_GAME_WINDOW_EVENT_MAXIMUM == UINT32_C(2));
static_assert(CNA_GAME_EVENT_MAXIMUM == UINT32_C(3));
static_assert(sizeof(CNA_GameFrameHooks) == 56U);
static_assert(offsetof(CNA_GameFrameHooks, context) == 48U);
static_assert(CNA_GAME_COMPONENT_EVENT_MAXIMUM == UINT32_C(4));
static_assert(CNA_GAME_SERVICE_TYPE_MAXIMUM == UINT32_C(1));
static_assert(sizeof(CNA_GameComponentCallbacks) == 64U);
static_assert(offsetof(CNA_GameComponentCallbacks, context) == 56U);
static_assert(CNA_CAMERA_STATE_MAXIMUM == UINT32_C(5));
static_assert(CNA_CAMERA_POSITION_MAXIMUM == UINT32_C(2));
static_assert(sizeof(CNA_CameraDeviceInfo) == 12U);
static_assert(offsetof(CNA_CameraDeviceInfo, position) == 8U);
static_assert(sizeof(CNA_FileDialogFilter) == 40U);
static_assert(offsetof(CNA_FileDialogFilter, pattern) == 24U);
static_assert(sizeof(CNA_AccelerometerReadingEventInfo) == 48U);
static_assert(alignof(CNA_AccelerometerReadingEventInfo) == 8U);
static_assert(offsetof(CNA_AccelerometerReadingEventInfo, x) == 24U);
