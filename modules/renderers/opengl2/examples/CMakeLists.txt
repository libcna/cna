# opengl2 renderer example/test registration (module-local; this file is the
# former cmake/Tests/OpenGL2Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_opengl2.md: first end-to-end proof for the native OpenGL 2.1 graphics renderer -- a real
# window, a real SDL GL context, and Clear/Present + Texture2D/SpriteBatch vertical-slice tests.
# Needs a real display/GPU, same as the SDL_GPU/Vulkan/Bgfx/D3D11 CTest entries above.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "OPENGL2")
    enable_testing()

    macro(cna_opengl2_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # plan_opengl2.md: device/window/GL-context lifecycle + color/depth/stencil clear/present.
    cna_opengl2_test(cna_test_opengl2_smoke opengl2_smoke_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Smoke COMMAND cna_test_opengl2_smoke
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: 2D vertical slice -- real Texture2D + SpriteBatch, verified via
    # ReadBackbuffer pixel readback (UV mapping, opaque draw, BlendState::AlphaBlend factors).
    cna_opengl2_test(cna_test_opengl2_2d opengl2_2d_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_2D COMMAND cna_test_opengl2_2d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: pixel-exact 3D vertex-format proof -- colored3d, textured3d,
    # colored_textured3d, and a real depth-test occlusion proof.
    cna_opengl2_test(cna_test_opengl2_3d opengl2_3d_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_3D COMMAND cna_test_opengl2_3d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: RenderTarget2D/FBO proof -- off-screen render, direct readback,
    # sampled-as-Texture2D via SpriteBatch, and depth-test occlusion inside the FBO.
    cna_opengl2_test(cna_test_opengl2_rendertarget2d opengl2_rendertarget2d_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_RenderTarget2D COMMAND cna_test_opengl2_rendertarget2d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: AlphaTestEffect, DualTextureEffect, BasicEffect lighting and fog.
    cna_opengl2_test(cna_test_opengl2_effects opengl2_effects_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Effects COMMAND cna_test_opengl2_effects
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: FixedHeightDynamicWidth presentation-mode resize proof.
    cna_opengl2_test(cna_test_opengl2_presentation opengl2_presentation_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Presentation COMMAND cna_test_opengl2_presentation
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: OcclusionQuery (GL_SAMPLES_PASSED) proof.
    cna_opengl2_test(cna_test_opengl2_occlusionquery opengl2_occlusionquery_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_OcclusionQuery COMMAND cna_test_opengl2_occlusionquery
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: TextureCube (GL_TEXTURE_CUBE_MAP) SetData/GetData round-trip proof.
    cna_opengl2_test(cna_test_opengl2_texturecube opengl2_texturecube_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_TextureCube COMMAND cna_test_opengl2_texturecube
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: Texture3D (GL_TEXTURE_3D) SetData/GetData round-trip proof.
    cna_opengl2_test(cna_test_opengl2_texture3d opengl2_texture3d_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Texture3D COMMAND cna_test_opengl2_texture3d
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: RenderTargetCube proof -- per-face FBO, depth occlusion, mipmap generation.
    cna_opengl2_test(cna_test_opengl2_rendertargetcube opengl2_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_RenderTargetCube COMMAND cna_test_opengl2_rendertargetcube
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: EnvironmentMapEffect proof -- reflection blend, Fresnel suppression,
    # correct cube face sampled, cross-renderer consistency vs the EasyGL golden scene, and
    # default-white sampler fallback when Texture/EnvironmentMap are unset.
    cna_opengl2_test(cna_test_opengl2_environmentmapeffect opengl2_environmentmapeffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_EnvironmentMapEffect COMMAND cna_test_opengl2_environmentmapeffect
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: SkinnedEffect proof -- bone-palette vertex skinning, WeightsPerVertex
    # gating, per-fragment lighting, and default-white sampler fallback.
    cna_opengl2_test(cna_test_opengl2_skinnedeffect opengl2_skinnedeffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SkinnedEffect COMMAND cna_test_opengl2_skinnedeffect
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: CNA::GraphicsCapability::SupportsCapability() real-value proof (twin of
    # dx3/sdlrenderer/canvas_graphics_capability_test.cpp).
    cna_opengl2_test(cna_test_opengl2_graphics_capability opengl2_graphics_capability_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_GraphicsCapability COMMAND cna_test_opengl2_graphics_capability
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: ShaderEffect (CreateEffectRenderer) proof -- runtime-compiled custom GLSL,
    # World/View/Projection wiring, uniform-by-name binding, and texture-unit binding.
    cna_opengl2_test(cna_test_opengl2_shadereffect opengl2_shadereffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_ShaderEffect COMMAND cna_test_opengl2_shadereffect
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: SpriteBatch::Begin(..., Effect*) proof -- custom sprite shader via
    # MatrixTransform (real XNA SpriteEffect.fx convention), texture-unit-0 binding, and
    # restoring the built-in shader on a subsequent Begin() without an effect.
    cna_opengl2_test(cna_test_opengl2_spritebatch_customeffect opengl2_spritebatch_customeffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteBatchCustomEffect COMMAND cna_test_opengl2_spritebatch_customeffect
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: real CnaPresentationMode::Letterbox/Overscan/Stretch proof (one process per
    # mode -- GraphicsDevice::SetPresentationMode is only reachable via
    # GraphicsDeviceManager::ApplyChanges(), which also re-asserts window size from
    # PresentationParameters, so mode and an independently-resized window can't both be exercised
    # through a single running process/ApplyChanges() call -- see each file's own header comment).
    cna_opengl2_test(cna_test_opengl2_presentationmode_letterbox opengl2_presentationmode_letterbox_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_PresentationMode_Letterbox COMMAND cna_test_opengl2_presentationmode_letterbox
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    cna_opengl2_test(cna_test_opengl2_presentationmode_overscan opengl2_presentationmode_overscan_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_PresentationMode_Overscan COMMAND cna_test_opengl2_presentationmode_overscan
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    cna_opengl2_test(cna_test_opengl2_presentationmode_stretch opengl2_presentationmode_stretch_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_PresentationMode_Stretch COMMAND cna_test_opengl2_presentationmode_stretch
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: PbrEffect/SkinnedPbrEffect proof -- metallic-roughness BRDF, tangent-space
    # normal mapping, occlusion/emissive maps, and bone-palette skinning combined with PBR.
    cna_opengl2_test(cna_test_opengl2_pbreffect opengl2_pbreffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_PbrEffect COMMAND cna_test_opengl2_pbreffect
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-343/344: IOR-derived dielectric F0 and factor-only KHR_materials_specular F0/F90,
    # repeated on both rigid and identity-skinned OpenGL2 PBR programs.
    cna_opengl2_test(cna_test_opengl2_pbr_fresnel_factors
                     ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Pbr_FresnelFactors COMMAND cna_test_opengl2_pbr_fresnel_factors
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: genuine cross-renderer visual-parity proof -- compares OpenGL2's own
    # rendered pixels against EasyGL's own checked-in golden PNG for the identical scene.
    cna_opengl2_test(cna_test_opengl2_environmentmapeffect_golden opengl2_environmentmapeffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_EnvironmentMapEffect_Golden COMMAND cna_test_opengl2_environmentmapeffect_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_basiceffect_golden opengl2_basiceffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_BasicEffect_Golden COMMAND cna_test_opengl2_basiceffect_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_alphatesteffect_golden opengl2_alphatesteffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_AlphaTestEffect_Golden COMMAND cna_test_opengl2_alphatesteffect_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_skinnedeffect_golden opengl2_skinnedeffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SkinnedEffect_Golden COMMAND cna_test_opengl2_skinnedeffect_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: genuinely custom VertexDeclaration proof (Task 1080) -- a reordered
    # attribute layout matching none of this renderer's fixed byte-strides, bound generically by
    # attribute name via BindVertexAttributesForDeclaration(), combined with a custom ShaderEffect.
    cna_opengl2_test(cna_test_opengl2_customvertexdeclaration opengl2_customvertexdeclaration_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_CustomVertexDeclaration COMMAND cna_test_opengl2_customvertexdeclaration
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: BlendState.BlendFactor propagation proof (IGraphicsRenderer::SetBlendFactor
    # was never overridden -> glBlendColor() was never called -> the GL constant-color register
    # stayed at (0,0,0,0) regardless of BlendState.BlendFactor).
    cna_opengl2_test(cna_test_opengl2_blendstate_blendfactor opengl2_blendstate_blendfactor_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_BlendState_BlendFactor COMMAND cna_test_opengl2_blendstate_blendfactor
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: SetDataOptions (Discard/NoOverwrite) upload-path proof --
    # SetDataWithOptions()/SetData16WithOptions()/SetData32WithOptions() were never overridden on
    # OpenGL2 (base-class default silently ignored `options`); now implements the same
    # orphan-then-glBufferSubData (Discard) / glBufferSubData (NoOverwrite) strategy as EasyGL.
    cna_opengl2_test(cna_test_opengl2_dynamicbuffer_stress opengl2_dynamicbuffer_stress_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_DynamicBuffer_Stress COMMAND cna_test_opengl2_dynamicbuffer_stress
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: real Texture2D mip-level upload + mip-aware filter selection proof --
    # UpdatePixelsLevel() was never overridden (mip levels beyond 0 silently vanished) and
    # GL_TEXTURE_MAX_LEVEL was never clamped (a real mip chain would have rendered solid black
    # under a mip-aware GL filter per the GL "incomplete mipmap texture" rule).
    cna_opengl2_test(cna_test_opengl2_texture_mip_filter opengl2_texture_mip_filter_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Texture_MipFilter COMMAND cna_test_opengl2_texture_mip_filter
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: single-level (mipMap=false, the common case) texture must still render
    # correctly under TextureFilter::Anisotropic, and ApplySamplerState()'s previously-discarded
    # MaxAnisotropy now actually reaches GL_TEXTURE_MAX_ANISOTROPY_EXT.
    cna_opengl2_test(cna_test_opengl2_texture2d_anisotropic_singlelevel opengl2_texture2d_anisotropic_singlelevel_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Texture2D_Anisotropic_SingleLevel COMMAND cna_test_opengl2_texture2d_anisotropic_singlelevel
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: real Texture3D mip-level GPU storage proof (Texture3DRenderer was
    # level-0-only; SetData(level>0,...) wrote via glTexSubImage3D into never-allocated storage).
    cna_opengl2_test(cna_test_opengl2_texture3d_mip opengl2_texture3d_mip_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Texture3D_Mip COMMAND cna_test_opengl2_texture3d_mip
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: real Multiple Render Targets (MRT) proof via SetRenderTargets.
    cna_opengl2_test(cna_test_opengl2_mrt opengl2_mrt_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_MRT COMMAND cna_test_opengl2_mrt
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md session 13: real depth + MSAA support for MRT (previously mrtFbo_ had no
    # depth/stencil attachment at all, and always attached a target's single-sample colorTex even
    # when it was created with multiSampleCount>0, silently discarding the requested AA).
    cna_opengl2_test(cna_test_opengl2_mrt_depth_msaa opengl2_mrt_depth_msaa_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_MRT_Depth_MSAA COMMAND cna_test_opengl2_mrt_depth_msaa
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md session 13: real GraphicsDevice.ReferenceStencil support
    # (SetReferenceStencil() was previously the shared IGraphicsRenderer base class's silent no-op).
    cna_opengl2_test(cna_test_opengl2_referencestencil opengl2_referencestencil_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_ReferenceStencil COMMAND cna_test_opengl2_referencestencil
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: real GPU hardware-instancing proof via DrawInstancedPrimitivesEx.
    cna_opengl2_test(cna_test_opengl2_instancedmodel opengl2_instancedmodel_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_InstancedModel COMMAND cna_test_opengl2_instancedmodel
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: real context-loss recovery proof via DebugSimulateContextLoss().
    cna_opengl2_test(cna_test_opengl2_context_loss_recovery opengl2_context_loss_recovery_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_ContextLossRecovery COMMAND cna_test_opengl2_context_loss_recovery
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl2.md: broader cross-renderer golden-image visual-parity sweep.
    cna_opengl2_test(cna_test_opengl2_blendstate_additive_golden opengl2_blendstate_additive_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_BlendState_Additive_Golden COMMAND cna_test_opengl2_blendstate_additive_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_rasterizerstate_cullmode_golden opengl2_rasterizerstate_cullmode_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_RasterizerState_CullMode_Golden COMMAND cna_test_opengl2_rasterizerstate_cullmode_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_dualtextureeffect_golden opengl2_dualtextureeffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_DualTextureEffect_Golden COMMAND cna_test_opengl2_dualtextureeffect_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_texture_filter_linear_golden opengl2_texture_filter_linear_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_Texture_Filter_Linear_Golden COMMAND cna_test_opengl2_texture_filter_linear_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_depthstencilstate_write_enable_golden opengl2_depthstencilstate_write_enable_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_DepthStencilState_WriteEnable_Golden COMMAND cna_test_opengl2_depthstencilstate_write_enable_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_spritebatch_rotation_golden opengl2_spritebatch_rotation_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteBatch_Rotation_Golden COMMAND cna_test_opengl2_spritebatch_rotation_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_goldenimage_smoke opengl2_goldenimage_smoke_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_GoldenImage_Smoke COMMAND cna_test_opengl2_goldenimage_smoke
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_pbreffect_golden opengl2_pbreffect_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_PbrEffect_Golden COMMAND cna_test_opengl2_pbreffect_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_skinnedeffect_vertexcolor_golden opengl2_skinnedeffect_vertexcolor_golden_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SkinnedEffect_VertexColor_Golden COMMAND cna_test_opengl2_skinnedeffect_vertexcolor_golden
        TIMEOUT 60 LABELS "OpenGL2" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_spritebatch_scale opengl2_spritebatch_scale_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteBatch_Scale COMMAND cna_test_opengl2_spritebatch_scale
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_spritebatch_sourcerect opengl2_spritebatch_sourcerect_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteBatch_SourceRect COMMAND cna_test_opengl2_spritebatch_sourcerect
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_sprite_effects opengl2_sprite_effects_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteEffects COMMAND cna_test_opengl2_sprite_effects
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_spritebatch_layerdepth opengl2_spritebatch_layerdepth_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteBatch_LayerDepth COMMAND cna_test_opengl2_spritebatch_layerdepth
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl2_test(cna_test_opengl2_spritebatch_blendstate_leak opengl2_spritebatch_blendstate_leak_test.cpp)
    cna_register_renderer_test(NAME OpenGL2_SpriteBatch_BlendStateLeak COMMAND cna_test_opengl2_spritebatch_blendstate_leak
        TIMEOUT 60 LABELS "OpenGL2" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
