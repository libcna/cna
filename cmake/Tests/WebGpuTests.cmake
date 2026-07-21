if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    enable_testing()
    if(TARGET cna_demo_2d)
        cna_register_backend_test(NAME WebGPU_Native2D_Smoke COMMAND ${CMAKE_COMMAND} -DCNA_WEBGPU_DEMO=$<TARGET_FILE:cna_demo_2d> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebGPUNativeSmokeTest.cmake
            TIMEOUT 70 LABELS "GraphicsSmoke;WebGPU" SKIP_REGULAR_EXPRESSION "\\[SKIP\\] CNA WebGPU native smoke")
    else()
        message(WARNING "CNA WebGPU: native smoke test requires CNA_BUILD_EXAMPLES=ON")
    endif()

    # WEBGPU-91/92: pixel-asserted readback coverage -- registered the same way as
    # WebGPU_Native2D_Smoke (SKIP_REGULAR_EXPRESSION for a headless/no-display environment),
    # since ReadBackbuffer() needs a real GPU-backed surface exactly like the smoke test does.
    macro(cna_webgpu_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(CNA_WEBGPU_RUNTIME_LIBRARY)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${target}>"
                    COMMENT "Copying wgpu-native runtime next to ${target}")
            if(UNIX AND NOT APPLE)
                set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
    endmacro()

    cna_webgpu_test(cna_test_webgpu_clear_readback examples/webgpu_clear_readback_test.cpp)
    cna_register_backend_test(NAME WebGPU_Clear_Readback COMMAND cna_test_webgpu_clear_readback
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 57/63 vertical slice: DrawColoredPrimitives/DrawIndexedColoredPrimitives.
    cna_webgpu_test(cna_test_webgpu_colored3d examples/webgpu_colored3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Colored3D COMMAND cna_test_webgpu_colored3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 63: DrawPrimitivesEx/DrawIndexedPrimitivesEx real GpuDrawParams dispatch (stride 16).
    cna_webgpu_test(cna_test_webgpu_drawprimitivesex examples/webgpu_drawprimitivesex_test.cpp)
    cna_register_backend_test(NAME WebGPU_DrawPrimitivesEx COMMAND cna_test_webgpu_drawprimitivesex
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 58/59/63: textured3d.wgsl / GetOrCreatePipelineTextured3D / stride-20 dispatch.
    cna_webgpu_test(cna_test_webgpu_textured3d examples/webgpu_textured3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Textured3D COMMAND cna_test_webgpu_textured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase 58/59/63: colored_textured3d.wgsl / GetOrCreatePipelineColoredTextured3D / stride-24.
    cna_webgpu_test(cna_test_webgpu_coloredtextured3d examples/webgpu_coloredtextured3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_ColoredTextured3D COMMAND cna_test_webgpu_coloredtextured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-22: lit_textured3d.wgsl / GetOrCreatePipelineLitTextured3D / stride-32 dispatch,
    # real Blinn-Phong lighting.
    cna_webgpu_test(cna_test_webgpu_littextured3d examples/webgpu_littextured3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_LitTextured3D COMMAND cna_test_webgpu_littextured3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Task 1105 (plan_graphics.md Phase 80): GetOrCreatePipelineLitTextured3DVertexLit /
    # BasicEffect.PreferPerPixelLighting real dispatch.
    cna_webgpu_test(cna_test_webgpu_basiceffect_preferperpixellighting examples/webgpu_basiceffect_preferperpixellighting_test.cpp)
    cna_register_backend_test(NAME WebGPU_BasicEffect_PreferPerPixelLighting COMMAND cna_test_webgpu_basiceffect_preferperpixellighting
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-23/34/72: alpha_test3d.wgsl / GetOrCreatePipelineAlphaTest3D / AlphaTestEffect
    # per-pixel discard, strides 20/24/32.
    cna_webgpu_test(cna_test_webgpu_alphatest3d examples/webgpu_alphatest3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_AlphaTest3D COMMAND cna_test_webgpu_alphatest3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-24: dual_texture3d.wgsl / GetOrCreatePipelineDualTexture3D / DualTextureEffect,
    # strides 20/24, second texture bind group.
    cna_webgpu_test(cna_test_webgpu_dualtexture3d examples/webgpu_dualtexture3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_DualTexture3D COMMAND cna_test_webgpu_dualtexture3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-25/36/74: env_map3d.wgsl / GetOrCreatePipelineEnvMap3D / EnvironmentMapEffect (stride
    # 32, this backend's first cube-map shader) -- also exercises WebGPUTextureCubeBackend
    # (CreateTextureCube()/SetData()), this backend's first cube-map texture support.
    cna_webgpu_test(cna_test_webgpu_envmap3d examples/webgpu_envmap3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_EnvMap3D COMMAND cna_test_webgpu_envmap3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-27/38/68: instanced3d.wgsl / GetOrCreatePipelineInstanced3D / DrawInstancedPrimitivesEx
    # -- a genuine second (WGPUVertexStepMode_Instance) vertex buffer binding carrying a
    # per-instance mat4 world transform.
    cna_webgpu_test(cna_test_webgpu_instanced3d examples/webgpu_instanced3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Instanced3D COMMAND cna_test_webgpu_instanced3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md CNB-58 WebGPU counterpart: pbr3d.wgsl / GetOrCreatePipelinePbr3D / PbrEffect
    # (unskinned only, stride 48), the WebGPU backend's real glTF 2.0 metallic-roughness BRDF.
    cna_webgpu_test(cna_test_webgpu_pbr3d examples/webgpu_pbr3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Pbr3D COMMAND cna_test_webgpu_pbr3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md Phase 14J WebGPU counterpart: skinned3d.wgsl family / GetOrCreatePipelineSkinned3D
    # / SkinnedEffect (strides 52/56, both PreferPerPixelLighting variants, VertexColorEnabled) --
    # closes this backend's pre-existing "no skinning shader at all" gap.
    cna_webgpu_test(cna_test_webgpu_skinned3d examples/webgpu_skinned3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Skinned3D COMMAND cna_test_webgpu_skinned3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # plan_cnj.md Phase 14J WebGPU counterpart: skinned_pbr3d.wgsl / GetOrCreatePipelineSkinnedPbr3D
    # / SkinnedPbrEffect (stride 68, PBR + skinning combo).
    cna_webgpu_test(cna_test_webgpu_skinnedpbr3d examples/webgpu_skinnedpbr3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_SkinnedPbr3D COMMAND cna_test_webgpu_skinnedpbr3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-006: SkinnedEffect world-space normal transform under non-identity World (rotation,
    # non-uniform scale, and both) for both PreferPerPixelLighting variants -- the cases every
    # existing skinned test, all of which use World = Identity, structurally cannot detect.
    cna_webgpu_test(cna_test_webgpu_skinnedeffect_world_normal examples/webgpu_skinnedeffect_world_normal_test.cpp)
    cna_register_backend_test(NAME WebGPU_SkinnedEffect_WorldNormal COMMAND cna_test_webgpu_skinnedeffect_world_normal
        TIMEOUT 60 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-41/77/78/79/80/81/82/83: real BlendState/RasterizerState (cull mode, wireframe)/
    # scissor/viewport wiring -- previously ApplyBlendState/ApplyRasterizerState/SetScissorRect/
    # SetViewport had no override at all on this backend, silently falling back to
    # IGraphicsBackend's no-op defaults for every 3D draw.
    cna_webgpu_test(cna_test_webgpu_graphicsstate examples/webgpu_graphicsstate_test.cpp)
    cna_register_backend_test(NAME WebGPU_GraphicsState COMMAND cna_test_webgpu_graphicsstate
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-53/54: WebGPURenderTargetBackend / CreateRenderTarget2D() / SetRenderTarget2D() --
    # this backend's first real RenderTarget2D support (Clear-only, real BasicEffect 3D draw, and
    # depth+stencil-tested round trips via GetData(), sampling a RenderTarget2D back through
    # SpriteBatch, and the critical "an intervening RT-targeted Clear() must not leak into the
    # backbuffer's own render pass" architecture check).
    cna_webgpu_test(cna_test_webgpu_rendertarget2d examples/webgpu_rendertarget2d_test.cpp)
    cna_register_backend_test(NAME WebGPU_RenderTarget2D COMMAND cna_test_webgpu_rendertarget2d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-58: MSAA infrastructure for both the swapchain backbuffer and RenderTarget2D --
    # backbuffer MultiSampleCount=0 vs. PreferMultiSampling+ApplyChanges() (the real runtime
    # ApplyMultiSampleCount() reconfigure path, mirroring vulkan_msaa_test.cpp's own Task 902
    # verification), ApplyMultiSampleCount()'s clamped-return-value contract (8 -> 0 or 4, never
    # the raw unsupported request), and a RenderTarget2D created after backbuffer MSAA is engaged
    # unconditionally mirroring that same real sample count. KNOWN OPEN ISSUE, intentionally left
    # registered and failing rather than hidden: 3 of 6 checks in this test currently FAIL because
    # genuine multisample-resolved rendering does not yet work end-to-end through the real
    # GraphicsDevice/BasicEffect draw path, despite the infrastructure above being real and
    # individually verified -- see this test's own top-of-file comment and plan_webgpu.md's
    # WEBGPU-58 row for the full investigation.
    cna_webgpu_test(cna_test_webgpu_msaa examples/webgpu_msaa_test.cpp)
    cna_register_backend_test(NAME WebGPU_Msaa COMMAND cna_test_webgpu_msaa
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-51: WebGPUTextureBackend::GetData() -- real staged-copy CPU readback for an arbitrary
    # Texture2D backend (previously the ITextureBackend::GetData() no-op default, since only the
    # swapchain (ReadBackbuffer, WEBGPU-91) and RenderTarget2D (WEBGPU-53/54) had real readback).
    cna_webgpu_test(cna_test_webgpu_texture2d_getdata examples/webgpu_texture2d_getdata_test.cpp)
    cna_register_backend_test(NAME WebGPU_Texture2D_GetData COMMAND cna_test_webgpu_texture2d_getdata
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-113: WebGPUTextureCubeBackend::GetData() -- real per-face staged-copy CPU readback,
    # closing the no-op default WEBGPU-56/74 deliberately left in place. Also exercises a real
    # TextureCube mip level 1 SetData()/GetData() round trip (untested since WEBGPU-56/74).
    cna_webgpu_test(cna_test_webgpu_texturecube_getdata examples/webgpu_texturecube_getdata_test.cpp)
    cna_register_backend_test(NAME WebGPU_TextureCube_GetData COMMAND cna_test_webgpu_texturecube_getdata
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-57/112: WebGPUTexture3DBackend / CreateTexture3D() -- this backend's first Texture3D
    # (volume texture) support (previously IGraphicsBackend's own nullptr-returning default).
    cna_webgpu_test(cna_test_webgpu_texture3d examples/webgpu_texture3d_test.cpp)
    cna_register_backend_test(NAME WebGPU_Texture3D COMMAND cna_test_webgpu_texture3d
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-114: WebGPURenderTargetCubeBackend / CreateRenderTargetCube() -- this backend's first
    # real render-into-a-cube-face support (previously IGraphicsBackend's own nullptr-returning
    # default). Direct face-to-face switching (no intervening backbuffer bind), a real BasicEffect
    # 3D draw into a face, an EnvironmentMapEffect sampling round trip proving IWebGPUCubeSamplable
    # resolves a RenderTargetCube (not just a plain TextureCube), and the "an intervening cube-
    # face-targeted Clear() must not leak into the backbuffer's own render pass" architecture check.
    cna_webgpu_test(cna_test_webgpu_rendertargetcube examples/webgpu_rendertargetcube_test.cpp)
    cna_register_backend_test(NAME WebGPU_RenderTargetCube COMMAND cna_test_webgpu_rendertargetcube
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # WEBGPU-52: real, render-pass-based, genuinely-linear-filtered mip generation for a plain
    # Texture2D/TextureCube from level 0 -- previously mipMap=true only pre-allocated empty
    # levels. A hard-edged red/blue stripe proves genuine LINEAR blending at the boundary (not a
    # nearest-neighbor copy), for both Texture2D and one TextureCube face; also documents the
    # deliberate divergence this introduces from FNA/every sibling CNA backend (which only
    # auto-regenerate mips for a RENDER TARGET being unbound, never a plain texture).
    cna_webgpu_test(cna_test_webgpu_mipgen examples/webgpu_mipgen_test.cpp)
    cna_register_backend_test(NAME WebGPU_MipGen COMMAND cna_test_webgpu_mipgen
        TIMEOUT 30 LABELS "WebGPU" ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
