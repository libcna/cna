# opengl4 renderer example/test registration (module-local; this file is the
# former cmake/Tests/OpenGL4Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics renderer CTest
# registration. Needs a real display/GPU, same as the Vulkan/Bgfx/D3D11/SDL_GPU entries above.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

# plan_opengl4.md GL4-1..GL4-12: real desktop OpenGL 4.x core-profile graphics renderer CTest
# registration. Needs a real display/GPU (or a virtual one, e.g. Xvfb) -- same
# SDL_VIDEODRIVER=x11/CNA_TEST_DISPLAY convention as the SDL_GPU/Vulkan/Bgfx/D3D11 registrations
# above; a genuine OpenGL 4.x core-profile context cannot be created without one.
if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "OPENGL4")
    enable_testing()

    macro(cna_opengl4_test target src)
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

    # plan_opengl4.md GL4-1..GL4-9: device/window/context lifecycle, VertexBuffer/IndexBuffer
    # round-trip, and a 60-frame Clear()+Present() loop.
    cna_opengl4_test(cna_test_opengl4_smoke opengl4_smoke_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Smoke COMMAND cna_test_opengl4_smoke
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-10: real pixel-readback proof (Clear/SpriteBatch/blend/sampler
    # address-mode), adapted from webgpu_clear_readback_test.cpp's own check list.
    cna_opengl4_test(cna_test_opengl4_readback opengl4_readback_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Readback COMMAND cna_test_opengl4_readback
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-11/GL4-12: DrawColoredPrimitives/DrawIndexedColoredPrimitives, with a
    # real pixel-readback depth-test occlusion proof (not just "didn't throw").
    cna_opengl4_test(cna_test_opengl4_3d opengl4_3d_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_3D COMMAND cna_test_opengl4_3d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-13: DrawPrimitivesEx/DrawIndexedPrimitivesEx stride dispatch --
    # textured3d/colored_textured3d/lit_textured3d (BasicEffect.TextureEnabled/VertexColorEnabled/
    # EnableDefaultLighting), with real pixel-readback proofs.
    cna_opengl4_test(cna_test_opengl4_textured3d opengl4_textured3d_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Textured3D COMMAND cna_test_opengl4_textured3d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-14: real FBO-backed RenderTarget2D -- colour texture attachment, depth/
    # stencil renderbuffer, MSAA renderbuffer resolved via glBlitFramebuffer on unbind, mip chain
    # regenerated via glGenerateMipmap on unbind, and real RenderTarget2D::GetData() readback.
    cna_opengl4_test(cna_test_opengl4_rendertarget2d opengl4_rendertarget2d_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_RenderTarget2D COMMAND cna_test_opengl4_rendertarget2d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-15: real per-face FBO-backed RenderTargetCube (colour cube-map texture,
    # depth/stencil renderbuffer, MSAA resolved via glBlitFramebuffer, mip chain regenerated via
    # glGenerateMipmap) and real MRT (SetRenderTargets, plural -- a persistent multi-attachment
    # FBO + glDrawBuffers).
    cna_opengl4_test(cna_test_opengl4_rendertargetcube_mrt opengl4_rendertargetcube_mrt_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_RenderTargetCube_MRT COMMAND cna_test_opengl4_rendertargetcube_mrt
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-16: real dynamic BlendState/DepthStencilState/RasterizerState mapping --
    # blend factors/equations, glBlendColor, real depth+stencil test state, cull mode, scissor
    # test, and wireframe fill mode.
    cna_opengl4_test(cna_test_opengl4_renderstate opengl4_renderstate_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_RenderState COMMAND cna_test_opengl4_renderstate
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-17: real backbuffer (window-level) MSAA -- a manually-managed multisample
    # FBO resolved via glBlitFramebuffer before Present()/ReadBackbuffer, fixed at construction
    # time (mirrors EasyGLRenderer's own CreateMsaaBuffers/ResolveMsaa approach).
    cna_opengl4_test(cna_test_opengl4_msaa opengl4_msaa_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_MSAA COMMAND cna_test_opengl4_msaa
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-18: real Texture2D mip level support -- UpdatePixelsLevel() uploads real
    # per-level data, GL_TEXTURE_MAX_LEVEL is clamped to the real level count, and mip-aware
    # TextureFilter values now map to real GL_*_MIPMAP_* sampler filters.
    cna_opengl4_test(cna_test_opengl4_mipmap opengl4_mipmap_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Mipmap COMMAND cna_test_opengl4_mipmap
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-19: AlphaTestEffect + DualTextureEffect -- real GPU discard proof (alpha
    # test against a reference value, swept across a representative CompareFunction subset) and the
    # real XNA/D3D "2x-modulate" dual-texture blend (tex1*2 * tex2 * diffuseColor), both folded into
    # the existing textured3d/colored_textured3d stride-20/24 shader programs (GL4-13) rather than
    # new stride cases.
    cna_opengl4_test(cna_test_opengl4_alphatest_dualtexture opengl4_alphatest_dualtexture_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_AlphaTestDualTexture COMMAND cna_test_opengl4_alphatest_dualtexture
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-20: plain (non-render-target) Texture3D + TextureCube -- real
    # GL_TEXTURE_3D/GL_TEXTURE_CUBE_MAP allocation, per-mip-level pre-allocated storage, and real
    # pixel-readback round-trips (per-slice/per-face aliasing, sub-box offsets, no-Y-flip proof
    # for TextureCube, and mip level 1 storage).
    cna_opengl4_test(cna_test_opengl4_texture3d opengl4_texture3d_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Texture3D COMMAND cna_test_opengl4_texture3d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    cna_opengl4_test(cna_test_opengl4_texturecube opengl4_texturecube_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_TextureCube COMMAND cna_test_opengl4_texturecube
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-21: EnvironmentMapEffect -- a dedicated env_map3d GLSL program (stride
    # 32) selected instead of lit_textured3d when GpuDrawParams::envMapping is set, reusing the
    # real XNA reflection/Fresnel/lerp/alpha-scaling formula already cross-verified on 3 other
    # renderers (see docs/environmentmapeffect-support.md).
    cna_opengl4_test(cna_test_opengl4_environmentmapeffect opengl4_environmentmapeffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_EnvironmentMapEffect COMMAND cna_test_opengl4_environmentmapeffect
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-22: SkinnedEffect -- a dedicated skinned3d GLSL program (new stride
    # 52/56, VertexPositionNormalTextureSkinned + optional trailing Color), real GPU vertex
    # skinning (weighted bone-matrix blend of position+normal, matching real XNA's
    # Skin(vin,boneCount) formula), and OpenGL4VertexBufferRenderer::ApplyLayout's new stride-52/56
    # attribute cases (BlendIndices via the newly-loaded gl4_glVertexAttribIPointer).
    cna_opengl4_test(cna_test_opengl4_skinnedeffect opengl4_skinnedeffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_SkinnedEffect COMMAND cna_test_opengl4_skinnedeffect
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-23: PbrEffect -- a dedicated pbr3d GLSL program (new stride 48,
    # VertexPositionNormalTangentTexture) implementing the real glTF metallic-roughness BRDF, plus
    # OpenGL4VertexBufferRenderer::ApplyLayout's new stride-48/68 vertex attribute cases.
    cna_opengl4_test(cna_test_opengl4_pbreffect opengl4_pbreffect_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_PbrEffect COMMAND cna_test_opengl4_pbreffect
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # GLTF-343/344: IOR-derived dielectric F0 and factor-only KHR_materials_specular F0/F90,
    # repeated on both rigid and identity-skinned OpenGL4 PBR programs.
    cna_opengl4_test(cna_test_opengl4_pbr_fresnel_factors
                     ${CMAKE_SOURCE_DIR}/modules/renderers/easygl/examples/easygl_pbr_fresnel_factors_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Pbr_FresnelFactors COMMAND cna_test_opengl4_pbr_fresnel_factors
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-24: real GL_SAMPLES_PASSED occlusion queries.
    cna_opengl4_test(cna_test_opengl4_occlusionquery opengl4_occlusionquery_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_OcclusionQuery COMMAND cna_test_opengl4_occlusionquery
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-25: real fog on all 7 GpuDrawParams-driven stride/dispatch shaders,
    # plus a new GpuDrawParams-aware stride-16 (coloredParams3d) program.
    cna_opengl4_test(cna_test_opengl4_fog opengl4_fog_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_Fog COMMAND cna_test_opengl4_fog
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-26: real dynamic SamplerState for direct 3D draws (was previously
    # always overwritten with a hardcoded Linear/Clamp value after the real state was applied).
    cna_opengl4_test(cna_test_opengl4_samplerstate opengl4_samplerstate_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_SamplerState COMMAND cna_test_opengl4_samplerstate
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-27: real GpuDrawParams::baseVertex support via glDrawElementsBaseVertex.
    cna_opengl4_test(cna_test_opengl4_basevertex opengl4_basevertex_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_BaseVertex COMMAND cna_test_opengl4_basevertex
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-28: real TransformWindowToLogical/TransformLogicalToWindow physical<->
    # logical coordinate mapping (Mouse::SetPosition / SdlInputBridge input mapping).
    cna_opengl4_test(cna_test_opengl4_transformcoords opengl4_transformcoords_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_TransformCoords COMMAND cna_test_opengl4_transformcoords
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-29: real PreferPerPixelLighting vertex-lit shader variant for
    # BasicEffect/SkinnedEffect (XNA's own default is PreferPerPixelLighting=false).
    cna_opengl4_test(cna_test_opengl4_preferperpixellighting opengl4_preferperpixellighting_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_PreferPerPixelLighting COMMAND cna_test_opengl4_preferperpixellighting
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-30: real custom ShaderEffect (CreateEffectRenderer) for a 3D
    # DrawIndexedPrimitives call, bypassing the built-in stride-dispatched shaders.
    cna_opengl4_test(cna_test_opengl4_shadereffect_3d opengl4_shadereffect_3d_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_ShaderEffect3D COMMAND cna_test_opengl4_shadereffect_3d
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-31: real 32-bit index buffer support (GL_UNSIGNED_INT), found as a
    # separate gap while scoping GL4-27.
    cna_opengl4_test(cna_test_opengl4_indexbuffer32 opengl4_indexbuffer32_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_IndexBuffer32 COMMAND cna_test_opengl4_indexbuffer32
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-32: real SpriteBatch::SetCustomEffect integration, found as a separate
    # gap while scoping GL4-30.
    cna_opengl4_test(cna_test_opengl4_shadereffect_spritebatch opengl4_shadereffect_spritebatch_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_ShaderEffectSpriteBatch COMMAND cna_test_opengl4_shadereffect_spritebatch
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_opengl4.md GL4-33: real hardware instancing (DrawInstancedPrimitivesEx), found in a
    # final audit after GL4-32 closed out.
    cna_opengl4_test(cna_test_opengl4_instancedmodel opengl4_instancedmodel_shader_test.cpp)
    cna_register_renderer_test(NAME OpenGL4_InstancedModel COMMAND cna_test_opengl4_instancedmodel
        TIMEOUT 60 LABELS "OpenGL4" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
