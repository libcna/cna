// SPDX-License-Identifier: MS-PL

#include <CNA/C/abi.h>
#include <CNA/C/core.h>
#include <CNA/C/graphics.h>
#include <CNA/C/graphics_resource.h>
#include <CNA/C/cna.h>

#include <stddef.h>

_Static_assert(CNA_ABI_VERSION == CNA_ABI_VERSION_ENCODE(0, 1, 0),
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
