# Skia migration matrix for the EasyGL graphics suite

This is the SKIA-2 inventory. It covers every CTest registered by `EasyGLTests.cmake`, its two
manual comparison executables, every checked-in EasyGL golden PNG, and every XNA-oracle scene.
Classification is based on the fixture's most demanding required path: a mixed test with a required
3D leg is `3d`, even if it also contains SpriteBatch controls.

The four categories are deliberately exclusive:

- `2d-direct`: Skia canvas/image/target/presentation primitives can express the contract;
- `2d-emulation`: the contract needs bounded runtime-effect, replay, or CPU emulation;
- `3d`: the fixture requires vertex/index, cube/volume sampling, depth/stencil, stock-3D, or model paths;
- `device-dependent`: the result depends on a probed surface/driver/display capability.

Run `python3 scripts/validate_skia_test_matrix.py`. It compares this file to live CMake
registrations and both asset directories, and rejects every unclassified or stale entry.

## 2D-direct registrations

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `ctest:EasyGL_PixelTestGame_Smoke` | `ctest` | `2d-direct` | Reuse the backend-neutral pixel fixture; SKIA-7. |
| `ctest:EasyGL_GoldenImage_Smoke` | `ctest` | `2d-direct` | Reuse golden comparison on raster output; SKIA-7. |
| `ctest:EasyGL_SpriteBatch_Rotation_Golden` | `ctest` | `2d-direct` | Existing Skia rotation/origin pixel coverage; SKIA-34. |
| `ctest:EasyGL_TextureFilter_Linear_Golden` | `ctest` | `2d-direct` | Existing point/linear Skia sampling coverage; SKIA-43. |
| `ctest:EasyGL_BlendState_Additive_Golden` | `ctest` | `2d-direct` | Existing Skia Additive route; SKIA-50. |
| `ctest:EasyGL_RenderTarget2D_Readback` | `ctest` | `2d-direct` | Existing target readback path; SKIA-61–62. |
| `ctest:EasyGL_RenderTarget2D_Golden` | `ctest` | `2d-direct` | Shared zero-tolerance target golden already runs on Skia; SKIA-75. |
| `ctest:EasyGL_RenderTarget2D_MipComplete` | `ctest` | `2d-direct` | Direct 2D feature with explicit raster mip refusal; SKIA-70. |
| `ctest:EasyGL_RenderTarget_ViewportScissorReset` | `ctest` | `2d-direct` | Existing target-local viewport/scissor coverage; SKIA-41–42. |
| `ctest:EasyGL_SpriteFont_Properties` | `ctest` | `2d-direct` | Common atlas metrics; Skia glyph fixtures cover rendering; SKIA-38. |
| `ctest:EasyGL_SpriteEffects_Flip` | `ctest` | `2d-direct` | Existing flip coverage; SKIA-34. |
| `ctest:EasyGL_SpriteBatch_RotationAroundOrigin` | `ctest` | `2d-direct` | Existing rotation/origin coverage; SKIA-34. |
| `ctest:EasyGL_SpriteBatch_ScaleOverloads` | `ctest` | `2d-direct` | Existing overload and scale coverage; SKIA-32, SKIA-39. |
| `ctest:EasyGL_SpriteBatch_RenderTargetSize` | `ctest` | `2d-direct` | Active raster target dimensions are direct; SKIA-61. |
| `ctest:EasyGL_SpriteBatch_SourceRectangleCropping` | `ctest` | `2d-direct` | Existing source-rectangle edge coverage; SKIA-32, SKIA-37. |
| `ctest:EasyGL_SpriteBatch_LayerDepthOrder` | `ctest` | `2d-direct` | Existing sort/depth ordering coverage; SKIA-36. |
| `ctest:EasyGL_TransformMatrix_Translation` | `ctest` | `2d-direct` | Existing affine transform coverage; SKIA-35. |
| `ctest:EasyGL_Texture2D_PartialRect_RoundTrip` | `ctest` | `2d-direct` | Existing exact transfer-range coverage; SKIA-24. |
| `ctest:EasyGL_Texture2D_Mip_RoundTrip` | `ctest` | `2d-direct` | Direct 2D feature with explicit raster mip refusal; SKIA-27. |
| `ctest:EasyGL_SurfaceFormat_Throws` | `ctest` | `2d-direct` | Existing raster format-rejection matrix; SKIA-25. |
| `ctest:EasyGL_RenderTargetUsage` | `ctest` | `2d-direct` | Preserve/discard is directly testable; SKIA-64. |
| `ctest:EasyGL_RT_Roundtrip` | `ctest` | `2d-direct` | Existing bind/read/sample round trip; SKIA-61–63. |
| `ctest:EasyGL_DeviceValidation` | `ctest` | `2d-direct` | Reuse common argument validation before native work. |
| `ctest:EasyGL_ClearOverloads` | `ctest` | `2d-direct` | Color routes direct; absent attachments use bounded policy; SKIA-13, SKIA-67. |
| `ctest:EasyGL_ViewportState` | `ctest` | `2d-direct` | Existing Skia viewport state coverage; SKIA-42. |
| `ctest:EasyGL_Scissor` | `ctest` | `2d-direct` | Existing Skia clip coverage; SKIA-41. |
| `ctest:EasyGL_Viewport_Subregion` | `ctest` | `2d-direct` | Direct active-canvas viewport placement; SKIA-42. |
| `ctest:EasyGL_DisposedResource` | `ctest` | `2d-direct` | Shared disposed-resource guards already run on Skia; SKIA-29. |
| `ctest:EasyGL_DoubleDispose` | `ctest` | `2d-direct` | Shared idempotent disposal already runs on Skia; SKIA-29. |
| `ctest:EasyGL_BoundResourceDispose` | `ctest` | `2d-direct` | Checked target/batch lifetime route; SKIA-69. |
| `ctest:EasyGL_MoveSemantics` | `ctest` | `2d-direct` | Shared wrapper ownership contract; SKIA-29. |
| `ctest:EasyGL_ResourceEvents` | `ctest` | `2d-direct` | Common resource events apply to Skia wrappers. |
| `ctest:EasyGL_DeviceDisposeOrder` | `ctest` | `2d-direct` | Existing backend-before-resource coverage; SKIA-12, SKIA-18. |
| `ctest:EasyGL_ResourceLeak` | `ctest` | `2d-direct` | Raster resources are measurable under ASan/LSan; SKIA-29, SKIA-74. |
| `ctest:EasyGL_ViewportResetAfterResize` | `ctest` | `2d-direct` | Existing resize/viewport tests; SKIA-8, SKIA-13. |
| `ctest:EasyGL_NpotTexture` | `ctest` | `2d-direct` | Shared NPOT fixture already runs on Skia; SKIA-26. |
| `ctest:EasyGL_TextureAddressMode` | `ctest` | `2d-direct` | Existing Clamp/Wrap axis coverage; SKIA-44–46. |
| `ctest:EasyGL_TextureAddressMode_Mirror` | `ctest` | `2d-direct` | Existing Mirror axis coverage; SKIA-46. |
| `ctest:EasyGL_SpriteFont_SingleGlyph` | `ctest` | `2d-direct` | Shared fixture already runs on Skia; SKIA-38. |
| `ctest:EasyGL_SpriteFont_MultiGlyphSpacing` | `ctest` | `2d-direct` | Shared fixture already runs on Skia; SKIA-38. |
| `ctest:EasyGL_SpriteFont_Newline` | `ctest` | `2d-direct` | Shared fixture already runs on Skia; SKIA-38. |
| `ctest:EasyGL_SpriteFont_DefaultChar` | `ctest` | `2d-direct` | Shared fixture already runs on Skia; SKIA-38. |
| `ctest:EasyGL_SpriteFont_EffectsFlip` | `ctest` | `2d-direct` | Shared flip fixture already runs on Skia; SKIA-38. |
| `ctest:EasyGL_SpriteFont_EffectsRotationScale` | `ctest` | `2d-direct` | Direct atlas sprite transform; SKIA-38. |
| `ctest:EasyGL_TextureFilter_PointVsLinear` | `ctest` | `2d-direct` | Existing exact point/linear coverage; SKIA-43. |
| `ctest:EasyGL_BlendState_Opaque` | `ctest` | `2d-direct` | Shared preset fixture already runs on Skia; SKIA-47–50. |
| `ctest:EasyGL_BlendState_AlphaBlend` | `ctest` | `2d-direct` | Shared preset fixture already runs on Skia; SKIA-47–50. |
| `ctest:EasyGL_BlendState_NonPremultiplied` | `ctest` | `2d-direct` | Shared preset fixture already runs on Skia; SKIA-47–50. |
| `ctest:EasyGL_BlendState_Additive` | `ctest` | `2d-direct` | Shared preset fixture already runs on Skia; SKIA-47–50. |
| `ctest:EasyGL_RenderTarget2D_Properties` | `ctest` | `2d-direct` | Level-zero color target properties are direct; SKIA-61. |
| `ctest:EasyGL_Demo2D_SmokeTest` | `ctest` | `2d-direct` | `Skia_Demo2D_Smoke` already runs the same demo. |
| `ctest:EasyGL_Sample_LayeredBlend` | `ctest` | `2d-direct` | Standard blend/layer path is direct; SKIA-36, SKIA-50. |
| `ctest:EasyGL_SpriteBatch_BlendStateLeak` | `ctest` | `2d-direct` | Existing Skia state-transition coverage; SKIA-59. |
| `ctest:EasyGL_SpriteBatch_CustomViewport` | `ctest` | `2d-direct` | Direct canvas viewport path; SKIA-42. |
| `ctest:EasyGL_SpriteBatch_ViewportSwitch` | `ctest` | `2d-direct` | Direct target-local viewport reset; SKIA-42, SKIA-61. |
| `ctest:EasyGL_InvalidMipLevel` | `ctest` | `2d-direct` | Explicit invalid/raster-mip refusal; SKIA-27, SKIA-70. |
| `ctest:EasyGL_BackbufferReadbackDimension` | `ctest` | `2d-direct` | Direct normalized RGBA8 readback; SKIA-7, SKIA-62. |
| `ctest:EasyGL_BackbufferFirstRead` | `ctest` | `2d-direct` | Deterministic initialized raster backbuffer; SKIA-13. |
| `ctest:EasyGL_BackbufferReject` | `ctest` | `2d-direct` | Existing bounds/unchanged-destination contract; SKIA-21, SKIA-62. |
| `ctest:EasyGL_Texture2D_GetDataContract` | `ctest` | `2d-direct` | Shared fixture already runs on Skia; SKIA-23. |
| `ctest:EasyGL_Texture2D_GetDataTransferRange` | `ctest` | `2d-direct` | Shared fixture already runs on Skia; SKIA-24. |
| `ctest:EasyGL_RenderTarget_PassBoundary` | `ctest` | `2d-direct` | Bind/unbind is immediate but preserves the same public boundary; SKIA-61. |

## 2D-emulation registrations

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `ctest:EasyGL_ShaderEffect_GLSL` | `ctest` | `2d-emulation` | Needs a deliberately mapped SkSL subset; SKIA-89–92. |
| `ctest:EasyGL_ShaderEffect_SpriteBatch_Uniform` | `ctest` | `2d-emulation` | SpriteBatch custom-effect subset; SKIA-93–94. |
| `ctest:EasyGL_Bloom_Extract` | `ctest` | `2d-emulation` | Candidate SkSL/CPU image filter pass; SKIA-89–94. |
| `ctest:EasyGL_Bloom_GaussianBlur` | `ctest` | `2d-emulation` | Candidate Skia image filter or bounded convolution. |
| `ctest:EasyGL_Bloom_Combine` | `ctest` | `2d-emulation` | Candidate SkSL/CPU compositing pass. |
| `ctest:EasyGL_Bloom_Pipeline` | `ctest` | `2d-emulation` | Requires deterministic multi-pass target replay. |
| `ctest:EasyGL_Clouds_Shader` | `ctest` | `2d-emulation` | Fixture draws through SpriteBatch; map only a proven SkSL subset. |
| `ctest:EasyGL_Distort_Shader` | `ctest` | `2d-emulation` | SpriteBatch runtime-effect candidate; SKIA-93–94. |
| `ctest:EasyGL_Distorters_DisplacementMapped_Shader` | `ctest` | `2d-emulation` | Multi-sampler runtime-effect candidate. |
| `ctest:EasyGL_Distorters_HeatHaze_Shader` | `ctest` | `2d-emulation` | Runtime-effect/CPU warp investigation. |
| `ctest:EasyGL_Distorters_PullIn_Shader` | `ctest` | `2d-emulation` | Runtime-effect/CPU warp investigation. |
| `ctest:EasyGL_DistortBlur_Shader` | `ctest` | `2d-emulation` | Multi-pass runtime-effect/image-filter investigation. |
| `ctest:EasyGL_PostprocessEffect_Shader` | `ctest` | `2d-emulation` | SpriteBatch postprocess subset; SKIA-93–94. |
| `ctest:EasyGL_EffectClone` | `ctest` | `2d-emulation` | Common clone semantics need a Skia effect representation. |
| `ctest:EasyGL_EffectCurrentTechnique` | `ctest` | `2d-emulation` | Common technique semantics need a bounded Skia mapping. |
| `ctest:EasyGL_ColorWriteChannels` | `ctest` | `2d-emulation` | Runtime blender emulation already proven; SKIA-56–57. |
| `ctest:EasyGL_BlendState_SeparateFunctions` | `ctest` | `2d-emulation` | Bounded runtime blender route; SKIA-53–55. |
| `ctest:EasyGL_BlendState_SeparateFactors` | `ctest` | `2d-emulation` | Bounded runtime blender route; SKIA-53–55. |
| `ctest:EasyGL_BlendState_BlendFactor` | `ctest` | `2d-emulation` | Needs a proven source-alpha/constant-factor mapping; SKIA-53–55. |
| `ctest:EasyGL_TextureCube_ContentLoad` | `ctest` | `2d-emulation` | Bounded CPU cube storage; shared DDS fixture passes as `Skia_TextureCube_ContentLoad`; SKIA-80–84. |
| `ctest:EasyGL_TextureCube_Faces_RoundTrip` | `ctest` | `2d-emulation` | Six-face CPU storage fixture passes on Skia; SKIA-80–84. |
| `ctest:EasyGL_TextureCube_Mip_RoundTrip` | `ctest` | `2d-emulation` | Bounded cube mip storage fixture passes on Skia; SKIA-80–84. |
| `ctest:EasyGL_TextureCube_PartialRect_RoundTrip` | `ctest` | `2d-emulation` | Exact CPU face-region transfer fixture passes on Skia; SKIA-80–84. |
| `ctest:EasyGL_Texture3D_Slices_RoundTrip` | `ctest` | `2d-emulation` | Bounded CPU volume slice fixture passes on Skia; SKIA-82–84. |
| `ctest:EasyGL_Texture3D_Mip_RoundTrip` | `ctest` | `2d-emulation` | Bounded volume mip fixture passes on Skia; SKIA-82–84. |
| `ctest:EasyGL_Texture3D_PartialBox_RoundTrip` | `ctest` | `2d-emulation` | Exact CPU sub-volume transfer fixture passes on Skia; SKIA-82–84. |
| `ctest:EasyGL_Texture3D_PartialBox_Readback` | `ctest` | `2d-emulation` | Exact front-to-back CPU box readback fixture passes on Skia; SKIA-82–84. |
| `ctest:EasyGL_RenderTargetCube_PluralBinding` | `ctest` | `2d-emulation` | Shared six-face singular/plural binding and explicit MRT-refusal fixture passes through CPU raster readback; SKIA-85–86. |
| `ctest:EasyGL_RenderTargetCube_Properties` | `ctest` | `2d-emulation` | Shared public property fixture passes with real mips and truthful zero-sample clamping; SKIA-85–86. |
| `ctest:EasyGL_RenderTargetCube_GetDataContract` | `ctest` | `2d-emulation` | Shared asymmetric rendered/uploaded face, mip, depth-interaction, lifetime, and readback contract passes; SKIA-85–86. |
| `ctest:EasyGL_RenderTargetCube_Usage` | `ctest` | `2d-emulation` | Shared Preserve/Discard and switching contract passes on six raster surfaces; SKIA-85–86. |

## 3D registrations

Every row in this section requires at least one vertex/index, stock-3D, model, cube/volume,
depth/stencil, or MRT leg. The raster backend must keep it outside its 2D support claim; any future
emulator is tracked by SKIA-80–105.

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `ctest:EasyGL_House3D_SmokeTest` | `ctest` | `3d` | No raster 3D pipeline; SKIA-95–103. |
| `ctest:EasyGL_TexturedQuad_Readback` | `ctest` | `3d` | Uses vertex/index textured geometry. |
| `ctest:EasyGL_BasicEffect_Golden` | `ctest` | `3d` | Stock BasicEffect/geometry path. |
| `ctest:EasyGL_DepthStencilState_WriteEnable_Golden` | `ctest` | `3d` | Requires real depth storage. |
| `ctest:EasyGL_RasterizerState_CullMode_Golden` | `ctest` | `3d` | Requires triangle winding/culling. |
| `ctest:EasyGL_AlphaTestEffect_Golden` | `ctest` | `3d` | Stock AlphaTestEffect geometry path. |
| `ctest:EasyGL_DualTextureEffect_Golden` | `ctest` | `3d` | Stock dual-texture geometry path. |
| `ctest:EasyGL_EnvironmentMapEffect_Golden` | `ctest` | `3d` | Requires normals and cube sampling. |
| `ctest:EasyGL_SkinnedEffect_Golden` | `ctest` | `3d` | Requires skinned vertex pipeline. |
| `ctest:EasyGL_SkinnedEffect_VertexColor` | `ctest` | `3d` | Requires skinned vertex pipeline. |
| `ctest:EasyGL_PbrEffect_Golden` | `ctest` | `3d` | Requires PBR vertex/fragment pipeline. |
| `ctest:EasyGL_SkinnedPbrEffect_Golden` | `ctest` | `3d` | Requires skinned PBR pipeline. |
| `ctest:EasyGL_RenderTarget2D_DepthBuffer` | `ctest` | `3d` | Requires target depth attachment. |
| `ctest:EasyGL_BasicEffect_Properties` | `ctest` | `3d` | Stock-3D effect family inventory. |
| `ctest:EasyGL_AlphaTestEffect_Properties` | `ctest` | `3d` | Stock-3D effect family inventory. |
| `ctest:EasyGL_SkinnedEffect_Properties` | `ctest` | `3d` | Stock-3D effect family inventory. |
| `ctest:EasyGL_AlphaTestEffect_AlphaCutout` | `ctest` | `3d` | AlphaTestEffect geometry draw. |
| `ctest:EasyGL_SkinnedEffect_BoneDeformation` | `ctest` | `3d` | Bone-weight vertex transformation. |
| `ctest:EasyGL_AvatarRenderer_RealRender` | `ctest` | `3d` | Avatar model/skinning pipeline. |
| `ctest:EasyGL_AvatarRenderer_AttachPart` | `ctest` | `3d` | Avatar model/skinning pipeline. |
| `ctest:EasyGL_AvatarRenderer_TintRouting` | `ctest` | `3d` | Avatar model/effect pipeline. |
| `ctest:EasyGL_ShaderEffect_3D` | `ctest` | `3d` | Arbitrary shader plus 3D stream. |
| `ctest:EasyGL_PerPixelLighting_Shader` | `ctest` | `3d` | Normal/light vertex pipeline. |
| `ctest:EasyGL_PerPixelLighting_DiffuseOnly_Shader` | `ctest` | `3d` | Normal/light vertex pipeline. |
| `ctest:EasyGL_PerPixelLighting_VertexDiffusePixelPhong_Shader` | `ctest` | `3d` | Normal/light vertex pipeline. |
| `ctest:EasyGL_VertexLighting_Diffuse_Shader` | `ctest` | `3d` | Vertex lighting pipeline. |
| `ctest:EasyGL_VertexLighting_DiffusePhong_Shader` | `ctest` | `3d` | Vertex/specular lighting pipeline. |
| `ctest:EasyGL_VertexLighting_Directional_Shader` | `ctest` | `3d` | Directional-light vertex pipeline. |
| `ctest:EasyGL_FlatShaded_Shader` | `ctest` | `3d` | Indexed normal-bearing geometry. |
| `ctest:EasyGL_CartoonEffect_Lambert_Shader` | `ctest` | `3d` | Indexed normal-bearing geometry. |
| `ctest:EasyGL_CartoonEffect_Toon_Shader` | `ctest` | `3d` | Indexed normal-bearing geometry. |
| `ctest:EasyGL_CartoonEffect_NormalDepth_Shader` | `ctest` | `3d` | Normal/depth geometry output. |
| `ctest:EasyGL_ShadowMapping_CreateShadowMap_Shader` | `ctest` | `3d` | Geometry and depth-map pass. |
| `ctest:EasyGL_ShadowMapping_DrawWithShadowMap_Shader` | `ctest` | `3d` | Geometry and shadow-map sampling. |
| `ctest:EasyGL_ShaderEffect_CustomVertexLayout` | `ctest` | `3d` | Custom 3D vertex declaration. |
| `ctest:EasyGL_NormalMapping_Shader` | `ctest` | `3d` | Tangent-space vertex layout. |
| `ctest:EasyGL_Billboard_Shader` | `ctest` | `3d` | Custom billboard vertex layout. |
| `ctest:EasyGL_ShatterEffect_Shader` | `ctest` | `3d` | Custom animated vertex layout. |
| `ctest:EasyGL_ParticleEffect_Shader` | `ctest` | `3d` | Custom particle vertex layout. |
| `ctest:EasyGL_AnimSprite_Shader` | `ctest` | `3d` | Uses indexed VertexPositionTexture, not SpriteBatch. |
| `ctest:EasyGL_ShipGame_Blur_Shader` | `ctest` | `3d` | Uses indexed VertexPositionTexture draw. |
| `ctest:EasyGL_ShaderEffect_TextureCube` | `ctest` | `3d` | Requires cube texture sampling. |
| `ctest:EasyGL_ShaderEffect_Texture3D` | `ctest` | `3d` | Requires volume texture sampling. |
| `ctest:EasyGL_ShipGame_NormalMapping_Shader` | `ctest` | `3d` | Normal mapping and cube sampling. |
| `ctest:EasyGL_ShipGame_Particle_Shader` | `ctest` | `3d` | Custom particle geometry. |
| `ctest:EasyGL_InstancedModel_Shader` | `ctest` | `3d` | Requires instanced vertex streams. |
| `ctest:EasyGL_DualTextureEffect_Blend` | `ctest` | `3d` | Stock dual-texture geometry. |
| `ctest:EasyGL_EnvironmentMapEffect_Readback` | `ctest` | `3d` | Environment-map geometry draw. |
| `ctest:EasyGL_EmissiveAmbientComposition` | `ctest` | `3d` | Stock-effect lighting composition. |
| `ctest:EasyGL_EnvironmentMapEffect_AmountZero` | `ctest` | `3d` | Environment-map geometry draw. |
| `ctest:EasyGL_EnvironmentMapEffect_Fog` | `ctest` | `3d` | Environment-map geometry/fog. |
| `ctest:EasyGL_EnvironmentMapEffect_AmountOne` | `ctest` | `3d` | Environment-map geometry draw. |
| `ctest:EasyGL_EnvironmentMapEffect_Specular` | `ctest` | `3d` | Environment-map specular pipeline. |
| `ctest:EasyGL_EnvironmentMapEffect_AlphaScaledLerp` | `ctest` | `3d` | Environment-map geometry draw. |
| `ctest:EasyGL_EnvironmentMapEffect_Fresnel` | `ctest` | `3d` | Normal/eye reflection pipeline. |
| `ctest:EasyGL_EnvironmentMapEffect_Fresnel_Gradient` | `ctest` | `3d` | Normal/eye reflection pipeline. |
| `ctest:EasyGL_EnvironmentMapEffect_EyePosition` | `ctest` | `3d` | Normal/eye reflection pipeline. |
| `ctest:EasyGL_EnvironmentMapEffect_WorldTransform` | `ctest` | `3d` | World-space normal pipeline. |
| `ctest:EasyGL_EnvironmentMapEffect_Combined` | `ctest` | `3d` | Combined stock-3D effect states. |
| `ctest:EasyGL_EnvironmentMapEffect_MultiLight` | `ctest` | `3d` | Multi-light stock effect. |
| `ctest:EasyGL_SkinnedEffect_Fog` | `ctest` | `3d` | Skinned geometry/fog. |
| `ctest:EasyGL_ViewSpace_Fog` | `ctest` | `3d` | View-space geometry fog. |
| `ctest:EasyGL_SkinnedEffect_MultiLight` | `ctest` | `3d` | Skinned multi-light pipeline. |
| `ctest:EasyGL_SkinnedEffect_LightingConformance` | `ctest` | `3d` | Skinned lighting pipeline. |
| `ctest:EasyGL_SkinnedEffect_Specular` | `ctest` | `3d` | Skinned specular pipeline. |
| `ctest:EasyGL_SkinnedEffect_PreferPerPixelLighting` | `ctest` | `3d` | Skinned shader variant. |
| `ctest:EasyGL_SkinnedEffect_WeightsPerVertex` | `ctest` | `3d` | Bone-weight vertex pipeline. |
| `ctest:EasyGL_SkinnedEffect_IdentityBones` | `ctest` | `3d` | Bone matrix vertex pipeline. |
| `ctest:EasyGL_SkinnedEffect_TranslationBone` | `ctest` | `3d` | Bone matrix vertex pipeline. |
| `ctest:EasyGL_SkinnedEffect_TwoBoneBlend` | `ctest` | `3d` | Bone blend vertex pipeline. |
| `ctest:EasyGL_SkinnedEffect_Combined` | `ctest` | `3d` | Combined skinned effect states. |
| `ctest:EasyGL_RenderTargetCube_SampleAfterUnbind` | `ctest` | `3d` | Cube-face target/sampling path. |
| `ctest:EasyGL_ModelDraw_RedQuad` | `ctest` | `3d` | Model mesh vertex/index draw. |
| `ctest:EasyGL_ModelJsonReader_Quad` | `ctest` | `3d` | Loaded model geometry draw. |
| `ctest:EasyGL_ModelJsonReader_32BitIndices` | `ctest` | `3d` | Model 32-bit index pipeline. |
| `ctest:EasyGL_ModelJsonReader_Texture` | `ctest` | `3d` | Textured model draw. |
| `ctest:EasyGL_ModelJsonReader_BoneHierarchy` | `ctest` | `3d` | Model skeleton hierarchy. |
| `ctest:EasyGL_ModelJsonReader_Skeleton` | `ctest` | `3d` | Model skeleton/skinning path. |
| `ctest:EasyGL_Model_SkinnedAnimationPlayback` | `ctest` | `3d` | Animated skinned model draw. |
| `ctest:EasyGL_Model_TwoMeshesEffects` | `ctest` | `3d` | Multi-mesh effect draws. |
| `ctest:EasyGL_Model_HierarchyChildMesh` | `ctest` | `3d` | Hierarchical model draw. |
| `ctest:EasyGL_MRT_TwoAttachments` | `ctest` | `3d` | Requires multiple render targets. |
| `ctest:EasyGL_BasicEffect_MultiLightEmissive` | `ctest` | `3d` | Stock lighting geometry. |
| `ctest:EasyGL_BasicEffect_Specular` | `ctest` | `3d` | Stock specular geometry. |
| `ctest:EasyGL_BasicEffect_PreferPerPixelLighting` | `ctest` | `3d` | Stock shader variant. |
| `ctest:EasyGL_DrawNoVertexBuffer` | `ctest` | `3d` | 3D missing-binding validation. |
| `ctest:EasyGL_DrawNoIndexBuffer` | `ctest` | `3d` | 3D missing-binding validation. |
| `ctest:EasyGL_DrawRangeValidation` | `ctest` | `3d` | Vertex/index range validation. |
| `ctest:EasyGL_PrimitiveTypeValidation` | `ctest` | `3d` | Primitive-topology validation. |
| `ctest:EasyGL_VbSetData` | `ctest` | `3d` | Vertex-buffer upload path. |
| `ctest:EasyGL_DynamicBufferStress` | `ctest` | `3d` | Dynamic vertex/index buffers. |
| `ctest:EasyGL_BufferUsage` | `ctest` | `3d` | Vertex/index buffer usage. |
| `ctest:EasyGL_DisposedBuffer` | `ctest` | `3d` | 3D buffer lifetime. |
| `ctest:EasyGL_VertexBufferIndexBufferGetData` | `ctest` | `3d` | Vertex/index CPU transfer. |
| `ctest:EasyGL_BasicEffectCombinations` | `ctest` | `3d` | Stock effect combination matrix. |
| `ctest:EasyGL_BasicEffect_VertexColorDisabled` | `ctest` | `3d` | Vertex-color shader variant. |
| `ctest:EasyGL_BasicEffect_VertexColorEnabled` | `ctest` | `3d` | Vertex-color shader variant. |
| `ctest:EasyGL_BasicEffect_TextureEnabled` | `ctest` | `3d` | Textured stock effect. |
| `ctest:EasyGL_BasicEffect_TextureVertexColorEnabled` | `ctest` | `3d` | Textured vertex-color effect. |
| `ctest:EasyGL_BasicEffect_OneLight` | `ctest` | `3d` | One-light geometry path. |
| `ctest:EasyGL_BasicEffect_Emissive` | `ctest` | `3d` | Emissive stock-effect path. |
| `ctest:EasyGL_BasicEffect_Combined` | `ctest` | `3d` | Combined stock-effect states. |
| `ctest:EasyGL_AlphaTestModes` | `ctest` | `3d` | AlphaTestEffect shader variants. |
| `ctest:EasyGL_AlphaTest_CompareFunctionSweep` | `ctest` | `3d` | AlphaTestEffect comparison matrix. |
| `ctest:EasyGL_AlphaTest_VertexColorDiffuse` | `ctest` | `3d` | Vertex-color alpha-test path. |
| `ctest:EasyGL_AlphaTest_Fog` | `ctest` | `3d` | Alpha-test geometry/fog. |
| `ctest:EasyGL_AlphaTest_NullTexture` | `ctest` | `3d` | Stock effect resource validation. |
| `ctest:EasyGL_DualTextureEffect_Doubling` | `ctest` | `3d` | Dual-texture stock effect. |
| `ctest:EasyGL_DualTextureEffect_Alpha` | `ctest` | `3d` | Dual-texture stock effect. |
| `ctest:EasyGL_DualTextureEffect_NullTexture0` | `ctest` | `3d` | Stock effect resource validation. |
| `ctest:EasyGL_DualTextureEffect_NullTexture2` | `ctest` | `3d` | Stock effect resource validation. |
| `ctest:EasyGL_DualTextureEffect_Fog` | `ctest` | `3d` | Dual-texture geometry/fog. |
| `ctest:EasyGL_DualTextureEffect_Combined` | `ctest` | `3d` | Combined dual-texture states. |
| `ctest:EasyGL_DualTexture` | `ctest` | `3d` | Dual-texture geometry path. |
| `ctest:EasyGL_SkinnedBones` | `ctest` | `3d` | Bone matrix vertex path. |
| `ctest:EasyGL_BasicEffectDefaultLighting` | `ctest` | `3d` | Stock default lighting. |
| `ctest:EasyGL_BasicEffectFog` | `ctest` | `3d` | Stock geometry fog. |
| `ctest:EasyGL_VertexFormats_AllStrides` | `ctest` | `3d` | Multiple 3D vertex layouts. |
| `ctest:EasyGL_DrawUserPrimitives_VPC` | `ctest` | `3d` | User vertex primitive path. |
| `ctest:EasyGL_DrawUserPrimitives_CustomVD` | `ctest` | `3d` | Custom user vertex declaration. |
| `ctest:EasyGL_DrawUserIndexedPrimitives_VPC` | `ctest` | `3d` | User indexed primitive path. |
| `ctest:EasyGL_DrawUserIndexedPrimitives_32` | `ctest` | `3d` | 32-bit user index path. |
| `ctest:EasyGL_SamplerState_DualTextureEffect` | `ctest` | `3d` | Stock-effect sampler state. |
| `ctest:EasyGL_TextureAddressMode_Clamp_DualTextureEffect` | `ctest` | `3d` | Dual-texture stock sampler. |
| `ctest:EasyGL_TextureAddressMode_Mirror_DualTextureEffect` | `ctest` | `3d` | Dual-texture stock sampler. |
| `ctest:EasyGL_TextureMipFilter_DualTextureEffect` | `ctest` | `3d` | Stock effect plus mip chain. |
| `ctest:EasyGL_DepthStencilState_WriteEnable` | `ctest` | `3d` | Requires real depth writes. |
| `ctest:EasyGL_DepthStencilState_CompareFunction` | `ctest` | `3d` | Requires real depth comparison. |
| `ctest:EasyGL_DepthStencilState_StencilEnable` | `ctest` | `3d` | Requires stencil storage. |
| `ctest:EasyGL_DepthStencilState_StencilMask` | `ctest` | `3d` | Requires stencil storage. |
| `ctest:EasyGL_DepthStencilState_StencilOps` | `ctest` | `3d` | Requires stencil operations. |
| `ctest:EasyGL_DepthStencilState_StencilTwoSided` | `ctest` | `3d` | Requires winding and stencil operations. |
| `ctest:EasyGL_GraphicsDevice_ReferenceStencil` | `ctest` | `3d` | Requires stencil reference storage. |
| `ctest:EasyGL_GraphicsDevice_ClearStencil` | `ctest` | `3d` | Requires stencil attachment. |
| `ctest:EasyGL_GraphicsDevice_ClearDepth` | `ctest` | `3d` | Requires depth attachment. |
| `ctest:EasyGL_DualTextureEffect_VertexColor` | `ctest` | `3d` | Vertex-color dual-texture path. |
| `ctest:EasyGL_RasterizerState_CullMode` | `ctest` | `3d` | Triangle winding/culling. |
| `ctest:EasyGL_RasterizerState_CullMode_Camera` | `ctest` | `3d` | Camera-space winding/culling. |
| `ctest:EasyGL_RasterizerState_CullMode_IndexedBasicEffect` | `ctest` | `3d` | Indexed stock-effect culling. |
| `ctest:EasyGL_FillMode_Solid` | `ctest` | `3d` | Polygon fill mode. |
| `ctest:EasyGL_DepthBias` | `ctest` | `3d` | Depth rasterization bias. |
| `ctest:EasyGL_Sample_MovingQuad3D` | `ctest` | `3d` | 3D sample geometry. |
| `ctest:EasyGL_Sample_DualTextureSwap` | `ctest` | `3d` | DualTextureEffect sample. |
| `ctest:EasyGL_Sample_KeyboardCube3D` | `ctest` | `3d` | Cube 3D sample. |
| `ctest:EasyGL_SkinnedEffect_WorldNormal` | `ctest` | `3d` | Skinned world-normal path. |
| `ctest:EasyGL_ColorSpace_MidTone` | `ctest` | `3d` | Shared fixture includes stock 3D draws. |
| `ctest:EasyGL_RenderTarget_SamplingOrientation` | `ctest` | `3d` | Shared fixture includes stock 3D sampling. |
| `ctest:EasyGL_StockEffectSamplerContract` | `ctest` | `3d` | Explicit stock-3D sampler contract. |
| `ctest:EasyGL_TextureFilterOrdinalContract` | `ctest` | `3d` | Shared fixture exercises stock effect samplers. |
| `ctest:EasyGL_EnvMapCubeSamplerContract` | `ctest` | `3d` | Cube sampler and environment effect. |
| `ctest:EasyGL_DualTextureSlotSamplerContract` | `ctest` | `3d` | DualTextureEffect sampler slots. |
| `ctest:EasyGL_SamplerComponentIsolation` | `ctest` | `3d` | Shared fixture includes stock/cube/volume paths. |
| `ctest:EasyGL_TextureFilterMipContract` | `ctest` | `3d` | Shared fixture uses stock effect mip selection. |
| `ctest:EasyGL_DescriptorCapacityContract` | `ctest` | `3d` | Mixed fixture requires buffers/effects/descriptors. |
| `ctest:EasyGL_PointSamplingContract` | `ctest` | `3d` | Shared fixture includes BasicEffect sampling. |
| `ctest:EasyGL_RenderTarget_ProducerConsumer` | `ctest` | `3d` | Mixed fixture requires stock-3D producer/consumer legs. |
| `ctest:EasyGL_RenderTarget_EffectSource` | `ctest` | `3d` | Mixed fixture requires stock effects and cube path. |
| `ctest:EasyGL_DeferredSourceLifetime` | `ctest` | `3d` | Mixed deferred fixture requires 3D draw legs. |
| `ctest:EasyGL_BoundTargetLifetime` | `ctest` | `3d` | Mixed lifetime fixture requires 3D draw legs. |
| `ctest:EasyGL_PresentLifecycle` | `ctest` | `3d` | Mixed lifecycle fixture requires 3D draw legs. |
| `ctest:EasyGL_RenderTarget_BackbufferConsumer` | `ctest` | `3d` | Mixed target fixture requires stock-3D consumer. |
| `ctest:EasyGL_RenderTarget_FirstUse` | `ctest` | `3d` | Mixed target fixture requires 3D first-use legs. |
| `ctest:EasyGL_SpriteBatch3DOrder` | `ctest` | `3d` | Explicit SpriteBatch/3D ordering. |
| `ctest:EasyGL_FrontFaceWinding` | `ctest` | `3d` | Triangle front-face convention. |
| `ctest:EasyGL_CubeVolume_GetDataContract` | `ctest` | `3d` | Cube and volume readback. |
| `ctest:EasyGL_RenderTarget_DepthStencilUsage` | `ctest` | `3d` | Real target depth/stencil lifecycle. |
| `ctest:EasyGL_GraphicsDevice_OrderedClear` | `ctest` | `3d` | Shared sequence contains 3D clear/draw legs. |
| `ctest:EasyGL_Backbuffer_PassOrder` | `ctest` | `3d` | Shared sequence contains 3D pass legs. |
| `ctest:EasyGL_Deferred_Viewport` | `ctest` | `3d` | Mixed deferred fixture requires 3D viewport legs. |
| `ctest:EasyGL_Deferred_Scissor` | `ctest` | `3d` | Mixed deferred fixture requires 3D scissor legs. |
| `ctest:EasyGL_CubeVolume_SetDataContract` | `ctest` | `3d` | Cube and volume upload. |

## Device-dependent registrations

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `ctest:EasyGL_RenderTarget2D_MsaaResolve` | `ctest` | `device-dependent` | Needs an accelerated sampled surface; SKIA-76–77. |
| `ctest:EasyGL_OcclusionQuery_Cycle` | `ctest` | `device-dependent` | Query capability/probe; SKIA-104–105. |
| `ctest:EasyGL_OcclusionQuery_VisibleQuad` | `ctest` | `device-dependent` | Query capability plus 3D visibility; SKIA-104–105. |
| `ctest:EasyGL_OcclusionQuery_OccludedQuad` | `ctest` | `device-dependent` | Query capability plus depth visibility; SKIA-104–105. |
| `ctest:EasyGL_DXT1_FromStream_Readback` | `ctest` | `device-dependent` | Compressed-format/device support is not raster RGBA8. |
| `ctest:EasyGL_RenderTargetCube_DepthFormat` | `ctest` | `device-dependent` | Requires probed cube/depth attachment support. |
| `ctest:EasyGL_MSAA_4x_Readback` | `ctest` | `device-dependent` | Needs accelerated MSAA resolve/readback; SKIA-76–77. |
| `ctest:EasyGL_PresentationParameters` | `ctest` | `device-dependent` | Display/window parameter behavior; SKIA-8, SKIA-13–15. |
| `ctest:EasyGL_DeviceResetEvents` | `ctest` | `device-dependent` | Backend device/reset lifecycle; SKIA-16. |
| `ctest:EasyGL_HandleRelease` | `ctest` | `device-dependent` | Native-handle lifetime differs by selected surface mode. |
| `ctest:EasyGL_PresentInterval` | `ctest` | `device-dependent` | SDL/driver interval result; SKIA-15. |
| `ctest:EasyGL_FullScreenField` | `ctest` | `device-dependent` | Window-system fullscreen behavior; SKIA-8. |
| `ctest:EasyGL_BackbufferResize` | `ctest` | `device-dependent` | Presenter/output resize behavior; SKIA-8, SKIA-13. |
| `ctest:EasyGL_RealWindowResize` | `ctest` | `device-dependent` | Requires actual display/window synchronization; SKIA-8. |
| `ctest:EasyGL_DepthFormat` | `ctest` | `device-dependent` | Actual depth attachment format capability. |
| `ctest:EasyGL_MsaaChange` | `ctest` | `device-dependent` | Runtime sample-count capability; SKIA-76–77. |
| `ctest:EasyGL_TextureAnisotropic_DualTextureEffect` | `ctest` | `device-dependent` | Requires probed anisotropy; SKIA-78–79. |
| `ctest:EasyGL_Texture2D_AnisotropicSingleLevel` | `ctest` | `device-dependent` | Requires probed anisotropy; SKIA-78–79. |
| `ctest:EasyGL_Anisotropic_GlState` | `ctest` | `device-dependent` | Explicit GL/device anisotropy state; SKIA-78–79. |
| `ctest:EasyGL_GraphicsDevice_DefaultStateOcclusion` | `ctest` | `device-dependent` | Occlusion result depends on real depth/query pipeline. |
| `ctest:EasyGL_GraphicsDeviceManager_Vsync` | `ctest` | `device-dependent` | Presenter/driver swap policy; SKIA-15. |
| `ctest:EasyGL_MsaaDepthContract` | `ctest` | `device-dependent` | Coupled samples/depth attachment capability. |
| `ctest:EasyGL_MsaaFirstReadback` | `ctest` | `device-dependent` | Accelerated resolve completion contract. |
| `ctest:EasyGL_MsaaMipReadback` | `ctest` | `device-dependent` | Accelerated resolve plus mip chain. |
| `ctest:EasyGL_RenderTargetCube_MsaaFace` | `ctest` | `device-dependent` | Cube-face accelerated MSAA capability. |

## Manual EasyGL comparison tools

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `tool:cna_diag_easygl` | `tool` | `3d` | Mixed diagnostic scene includes EasyGL 3D; a 2D-only Skia diagnostic must select a subset. |
| `tool:cna_oracle_render_easygl` | `tool` | `3d` | Renderer covers the complete mixed oracle corpus; only sprite scenes are raster candidates. |

## EasyGL golden images

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `golden:easygl_goldenimage_smoke_test.png` | `golden` | `2d-direct` | Reusable comparison canary for Skia raster output. |
| `golden:easygl_spritebatch_rotation_golden_test.png` | `golden` | `2d-direct` | Reusable SpriteBatch rotation oracle. |
| `golden:easygl_texture_filter_linear_golden_test.png` | `golden` | `2d-direct` | Reusable linear-sampling oracle. |
| `golden:easygl_blendstate_additive_golden_test.png` | `golden` | `2d-direct` | Reusable Additive blend oracle. |
| `golden:easygl_alphatesteffect_golden_test.png` | `golden` | `3d` | AlphaTestEffect geometry oracle. |
| `golden:easygl_basiceffect_golden_test.png` | `golden` | `3d` | BasicEffect geometry oracle. |
| `golden:easygl_depthstencilstate_write_enable_golden_test.png` | `golden` | `3d` | Depth-write oracle. |
| `golden:easygl_dualtextureeffect_golden_test.png` | `golden` | `3d` | DualTextureEffect geometry oracle. |
| `golden:easygl_environmentmapeffect_golden_test.png` | `golden` | `3d` | Environment/cube geometry oracle. |
| `golden:easygl_pbreffect_golden_test_a.png` | `golden` | `3d` | PBR material oracle A. |
| `golden:easygl_pbreffect_golden_test_b.png` | `golden` | `3d` | PBR material oracle B. |
| `golden:easygl_pbreffect_golden_test_c.png` | `golden` | `3d` | PBR material oracle C. |
| `golden:easygl_pbreffect_golden_test_d.png` | `golden` | `3d` | PBR material oracle D. |
| `golden:easygl_rasterizerstate_cullmode_golden_test.png` | `golden` | `3d` | Triangle-culling oracle. |
| `golden:easygl_skinnedeffect_golden_test.png` | `golden` | `3d` | Skinned geometry oracle. |
| `golden:easygl_skinnedeffect_vertexcolor_test_a.png` | `golden` | `3d` | Skinned vertex-color oracle A. |
| `golden:easygl_skinnedeffect_vertexcolor_test_b.png` | `golden` | `3d` | Skinned vertex-color oracle B. |

## XNA-oracle scenes

| Entry | Kind | Category | Skia route or evidence |
|---|---|---|---|
| `oracle:sprite_basic_quad.scene` | `oracle` | `2d-direct` | Direct SpriteBatch image oracle candidate. |
| `oracle:sprite_flipped_quad.scene` | `oracle` | `2d-direct` | Direct SpriteEffects oracle candidate. |
| `oracle:sprite_mirror_quad.scene` | `oracle` | `2d-direct` | Direct mirror-address oracle candidate. |
| `oracle:sprite_multitexture_quad.scene` | `oracle` | `2d-direct` | Direct multiple-texture SpriteBatch sequence. |
| `oracle:sprite_rotated_quad.scene` | `oracle` | `2d-direct` | Direct rotation/origin oracle candidate. |
| `oracle:sprite_sortmode_backtofront_quad.scene` | `oracle` | `2d-direct` | Direct sort-order oracle candidate. |
| `oracle:sprite_sortmode_deferred_quad.scene` | `oracle` | `2d-direct` | Direct deferred-order oracle candidate. |
| `oracle:sprite_sortmode_fronttoback_quad.scene` | `oracle` | `2d-direct` | Direct sort-order oracle candidate. |
| `oracle:sprite_wrap_quad.scene` | `oracle` | `2d-direct` | Direct wrap-address oracle candidate. |
| `oracle:alphatest_always_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_equal_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_greaterequal_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_less_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_lessequal_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_never_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_notequal_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:alphatest_quad.scene` | `oracle` | `3d` | AlphaTestEffect geometry scene. |
| `oracle:colored3d.scene` | `oracle` | `3d` | Colored 3D primitive scene. |
| `oracle:colored_linelist_quad.scene` | `oracle` | `3d` | Line-list primitive scene. |
| `oracle:colored_linestrip_quad.scene` | `oracle` | `3d` | Line-strip primitive scene. |
| `oracle:colored_trianglestrip_quad.scene` | `oracle` | `3d` | Triangle-strip primitive scene. |
| `oracle:cullmode_ccwface_quad.scene` | `oracle` | `3d` | Triangle winding/cull scene. |
| `oracle:cullmode_cwface_quad.scene` | `oracle` | `3d` | Triangle winding/cull scene. |
| `oracle:cullmode_none_quad.scene` | `oracle` | `3d` | Triangle winding/cull scene. |
| `oracle:dualtexture_quad.scene` | `oracle` | `3d` | DualTextureEffect scene. |
| `oracle:envmap_fresnel_quad.scene` | `oracle` | `3d` | Environment/cube scene. |
| `oracle:envmap_quad.scene` | `oracle` | `3d` | Environment/cube scene. |
| `oracle:envmap_specular_quad.scene` | `oracle` | `3d` | Environment/cube scene. |
| `oracle:fog_gradient_quad.scene` | `oracle` | `3d` | View-space fog geometry scene. |
| `oracle:lit_textured_quad.scene` | `oracle` | `3d` | Lit textured geometry scene. |
| `oracle:lit_textured_quad_pixellighting.scene` | `oracle` | `3d` | Per-pixel lit geometry scene. |
| `oracle:multilight_textured_quad.scene` | `oracle` | `3d` | Multi-light geometry scene. |
| `oracle:skinned_fourbone_quad.scene` | `oracle` | `3d` | Four-weight skinning scene. |
| `oracle:skinned_pixellighting_fourbone_quad.scene` | `oracle` | `3d` | Lit four-weight skinning scene. |
| `oracle:skinned_pixellighting_quad.scene` | `oracle` | `3d` | Lit skinned geometry scene. |
| `oracle:skinned_pixellighting_twobone_quad.scene` | `oracle` | `3d` | Lit two-weight skinning scene. |
| `oracle:skinned_quad.scene` | `oracle` | `3d` | Skinned geometry scene. |
| `oracle:skinned_twobone_quad.scene` | `oracle` | `3d` | Two-weight skinning scene. |
| `oracle:textured_quad.scene` | `oracle` | `3d` | Vertex/index textured geometry scene. |
