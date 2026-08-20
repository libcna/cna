# igl renderer example/test registration (module-local, the shape every renderer family uses since
# the module-examples campaign).
#
# plans/plan_igl.md: IGL (facebook/igl) renderer integration tests. Like the LLGL/VULKAN/MAGNUM blocks
# these need a real windowing system and a real driver -- IGL creates its device from the platform
# window itself -- so they run against CNA_TEST_DISPLAY (a live X server, or an Xvfb one with Mesa's
# software rasterizer behind it). Which native API is underneath is decided at runtime by
# CNA_IGL_BACKEND, so the same binaries exercise whichever backend was selected.

# Example/test executables historically live at the build root (scripts, docs and the golden-image
# working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by the graphics
# module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND CNA_GRAPHICS_RENDERER STREQUAL "IGL")
    enable_testing()

    macro(cna_igl_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            # CNA and the renderer target call into each other (GraphicsDevice constructs the
            # renderer; the renderer's factory lives in the renderer library), so they are linked as
            # one group rather than in a single pass -- the same shape every other renderer's own
            # test macro uses here.
            target_link_libraries(${target} PRIVATE
                CNA
                SHARP_RUNTIME SDL3::SDL3)
        else()
            target_link_libraries(${target} PRIVATE CNA ${RENDERER_TARGET} SHARP_RUNTIME SDL3::SDL3)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # IGL-9: the device comes up, names its backend, and survives a real frame loop.
    cna_igl_test(cna_test_igl_smoke igl_smoke_test.cpp)
    cna_register_renderer_test(NAME Igl_Smoke COMMAND cna_test_igl_smoke
        TIMEOUT 90 LABELS "GraphicsSmoke;Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-14: SpriteBatch geometry, sampling, tinting and blend state, checked by pixel readback.
    cna_igl_test(cna_test_igl_2d igl_2d_test.cpp)
    cna_register_renderer_test(NAME Igl_2D COMMAND cna_test_igl_2d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-20: vertex/index buffers, the generated effect shader and a real depth test.
    cna_igl_test(cna_test_igl_3d igl_3d_test.cpp)
    cna_register_renderer_test(NAME Igl_3D COMMAND cna_test_igl_3d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # IGL-60: same binary, forced onto Vulkan -- regression coverage for the NormalizedZRange fix
    # (this exact scene was the one that first proved it: the depth test only gates correctly once
    # the OpenGL-only [-1,1] Z remap stops being wrongly applied to Vulkan's native [0,1] range).
    cna_register_renderer_test(NAME Igl_3D_Vulkan COMMAND cna_test_igl_3d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")

    # IGL-24: off-screen targets, their readback row order, and their sampling orientation.
    cna_igl_test(cna_test_igl_rendertarget igl_rendertarget_test.cpp)
    cna_register_renderer_test(NAME Igl_RenderTarget COMMAND cna_test_igl_rendertarget
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # IGL-60: same binary, forced onto Vulkan -- also regressed by the NormalizedZRange bug.
    cna_register_renderer_test(NAME Igl_RenderTarget_Vulkan COMMAND cna_test_igl_rendertarget
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")

    # IGL-55: the first stock-effect parity test beyond BasicEffect's colour-only path.
    cna_igl_test(cna_test_igl_alphatesteffect igl_alphatesteffect_test.cpp)
    cna_register_renderer_test(NAME Igl_AlphaTestEffect COMMAND cna_test_igl_alphatesteffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-55: DualTextureEffect -- two texture units and the diffuse tint in one shader.
    cna_igl_test(cna_test_igl_dualtextureeffect igl_dualtextureeffect_test.cpp)
    cna_register_renderer_test(NAME Igl_DualTextureEffect COMMAND cna_test_igl_dualtextureeffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-38/IGL-55: BasicEffect fog -- FNA's fogVector view-space formulation.
    cna_igl_test(cna_test_igl_fog igl_fog_test.cpp)
    cna_register_renderer_test(NAME Igl_Fog COMMAND cna_test_igl_fog
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-39/IGL-55: DepthStencilState's stencil test wired into the real 3D pipeline.
    cna_igl_test(cna_test_igl_stencil igl_stencil_test.cpp)
    cna_register_renderer_test(NAME Igl_Stencil COMMAND cna_test_igl_stencil
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-22/IGL-55: multiple simultaneous colour attachments (MRT), first cut.
    cna_igl_test(cna_test_igl_mrt igl_mrt_test.cpp)
    cna_register_renderer_test(NAME Igl_Mrt COMMAND cna_test_igl_mrt
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-15/IGL-55: real MSAA resolved into a RenderTarget2D.
    cna_igl_test(cna_test_igl_msaa igl_msaa_test.cpp)
    cna_register_renderer_test(NAME Igl_Msaa COMMAND cna_test_igl_msaa
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-17/IGL-35/IGL-55: TextureCube sampling and EnvironmentMapEffect.
    cna_igl_test(cna_test_igl_environmentmapeffect igl_environmentmapeffect_test.cpp)
    cna_register_renderer_test(NAME Igl_EnvironmentMapEffect COMMAND cna_test_igl_environmentmapeffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-36/IGL-55: SkinnedEffect's 72-bone std140 uniform block, identity palette.
    cna_igl_test(cna_test_igl_skinnedeffect igl_skinnedeffect_test.cpp)
    cna_register_renderer_test(NAME Igl_SkinnedEffect COMMAND cna_test_igl_skinnedeffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-37/IGL-55: PbrEffect's GGX metallic-roughness BRDF, hand-derived expected value.
    cna_igl_test(cna_test_igl_pbreffect igl_pbreffect_test.cpp)
    cna_register_renderer_test(NAME Igl_PbrEffect COMMAND cna_test_igl_pbreffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-21: RenderTargetCube -- per-face binding, readback and back-buffer isolation.
    cna_igl_test(cna_test_igl_rendertargetcube igl_rendertargetcube_test.cpp)
    cna_register_renderer_test(NAME Igl_RenderTargetCube COMMAND cna_test_igl_rendertargetcube
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-17/IGL-42..45/IGL-55: custom ShaderEffect compiling and sampling a real Texture3D.
    cna_igl_test(cna_test_igl_shadereffect_texture3d igl_shadereffect_texture3d_test.cpp)
    cna_register_renderer_test(NAME Igl_ShaderEffectTexture3D COMMAND cna_test_igl_shadereffect_texture3d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-31/IGL-55: DrawInstancedPrimitives with a real per-instance vertex stream.
    cna_igl_test(cna_test_igl_instancing igl_instancing_test.cpp)
    cna_register_renderer_test(NAME Igl_Instancing COMMAND cna_test_igl_instancing
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-36/IGL-55: SkinnedEffect with a real, non-identity bone transform.
    cna_igl_test(cna_test_igl_skinnedeffect_translation_bone igl_skinnedeffect_translation_bone_test.cpp)
    cna_register_renderer_test(NAME Igl_SkinnedEffectTranslationBone
        COMMAND cna_test_igl_skinnedeffect_translation_bone
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-35/IGL-55: EnvironmentMapEffect's Fresnel edge-weighting.
    cna_igl_test(cna_test_igl_environmentmapeffect_fresnel igl_environmentmapeffect_fresnel_test.cpp)
    cna_register_renderer_test(NAME Igl_EnvironmentMapEffectFresnel
        COMMAND cna_test_igl_environmentmapeffect_fresnel
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-37/IGL-55: PbrEffect's NormalMap, EmissiveFactor, OcclusionMap and null-map fallback.
    cna_igl_test(cna_test_igl_pbreffect_maps igl_pbreffect_maps_test.cpp)
    cna_register_renderer_test(NAME Igl_PbrEffectMaps COMMAND cna_test_igl_pbreffect_maps
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-45/IGL-55: SpriteBatch.Begin(effect) -- a custom ShaderEffect driving the 2D pipeline.
    cna_igl_test(cna_test_igl_spritebatch_shadereffect igl_spritebatch_shadereffect_test.cpp)
    cna_register_renderer_test(NAME Igl_SpriteBatchShaderEffect
        COMMAND cna_test_igl_spritebatch_shadereffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-22/IGL-55: MRT at the full 4-slot count.
    cna_igl_test(cna_test_igl_mrt4 igl_mrt4_test.cpp)
    cna_register_renderer_test(NAME Igl_Mrt4 COMMAND cna_test_igl_mrt4
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-22: a RenderTargetCube face is refused in a multi-render-target bind.
    cna_igl_test(cna_test_igl_mrt_cube_refuse igl_mrt_cube_refuse_test.cpp)
    cna_register_renderer_test(NAME Igl_MrtCubeRefuse COMMAND cna_test_igl_mrt_cube_refuse
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # IGL-60: the Vulkan back buffer's real BGRA byte order must not silently swap red and blue on
    # readback. Forced onto the Vulkan backend specifically (every other test here runs whatever
    # CNA_IGL_BACKEND the environment provides, which defaults to OpenGL) -- see RunPixelTest's own
    # VK_ERROR_SURFACE_LOST_KHR handling for the skip-if-no-Vulkan-WSI safety net this relies on.
    cna_igl_test(cna_test_igl_vulkan_backbuffer_bgr igl_vulkan_backbuffer_bgr_test.cpp)
    cna_register_renderer_test(NAME Igl_VulkanBackBufferBgr
        COMMAND cna_test_igl_vulkan_backbuffer_bgr
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")

    # IGL-65: surface-format transfers against a real device -- one upload/readback round trip per
    # byte class, which is what a `width * 4` row pitch cannot survive.
    cna_igl_test(cna_test_igl_surfaceformat igl_surfaceformat_test.cpp)
    cna_register_renderer_test(NAME Igl_SurfaceFormat COMMAND cna_test_igl_surfaceformat
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # The same binary on the other backend: IGL's two backends have genuinely different format
    # tables, so a format this renderer claims must survive both.
    cna_register_renderer_test(NAME Igl_SurfaceFormat_Vulkan COMMAND cna_test_igl_surfaceformat
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")

    # IGL-60/IGL-67: the row order AND the row origin of every readback, with no draw calls at all.
    # A rendering-based test cannot see this class of bug, because a readback flip and a projection
    # flip cancel each other -- which is how it survived every pixel test this renderer already had.
    cna_igl_test(cna_test_igl_readback_orientation igl_readback_orientation_test.cpp)
    cna_register_renderer_test(NAME Igl_ReadbackOrientation
        COMMAND cna_test_igl_readback_orientation
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    # Both backends: IGL's OpenGL and Vulkan paths measure a readback rectangle's y from OPPOSITE
    # edges, so a fix that satisfied one by breaking the other must fail here.
    cna_register_renderer_test(NAME Igl_ReadbackOrientation_Vulkan
        COMMAND cna_test_igl_readback_orientation
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")

    # IGL-42/IGL-43: the custom ShaderEffect path on BOTH backends. The three existing custom-effect
    # tests all set effect parameters, which design decision 8 refuses on Vulkan, so none of them
    # could report anything about the path itself there. This one is deliberately uniform-free.
    cna_igl_test(cna_test_igl_custom_effect_backend igl_custom_effect_backend_test.cpp)
    cna_register_renderer_test(NAME Igl_CustomEffectBackend
        COMMAND cna_test_igl_custom_effect_backend
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    cna_register_renderer_test(NAME Igl_CustomEffectBackend_Vulkan
        COMMAND cna_test_igl_custom_effect_backend
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")

    # IGL-65 follow-up: the conformance pass a SurfaceFormat must survive before this renderer
    # promotes it to the public XNA texture API -- upload through the typed overload, sampling in a
    # real draw, readback, and render-target use. Storage alone is what the earlier audit rightly
    # refused to promote on.
    cna_igl_test(cna_test_igl_public_surfaceformat igl_public_surfaceformat_test.cpp)
    cna_register_renderer_test(NAME Igl_PublicSurfaceFormat
        COMMAND cna_test_igl_public_surfaceformat
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
    cna_register_renderer_test(NAME Igl_PublicSurfaceFormat_Vulkan
        COMMAND cna_test_igl_public_surfaceformat
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")
    # plans/plan_igl.md IGL-43: the three custom-effect tests now carry a SPIR-V variant of their shaders
    # and declare the std140 block their parameters live in, so they run on Vulkan too. Registered
    # here rather than left to a manual run: the whole point of IGL-43 was that nothing guarded the
    # Vulkan half of the custom-effect path.
    cna_register_renderer_test(NAME Igl_ShaderEffectTexture3D_Vulkan
        COMMAND cna_test_igl_shadereffect_texture3d
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")
    cna_register_renderer_test(NAME Igl_SpriteBatchShaderEffect_Vulkan
        COMMAND cna_test_igl_spritebatch_shadereffect
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")
    cna_register_renderer_test(NAME Igl_Instancing_Vulkan
        COMMAND cna_test_igl_instancing
        TIMEOUT 90 LABELS "Igl"
        ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY};CNA_IGL_BACKEND=vulkan")
endif()
