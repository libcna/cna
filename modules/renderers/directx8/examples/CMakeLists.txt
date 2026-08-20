# directx8 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX8Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_dx8.md: real DirectX 8 renderer -- DXVK-delivered (not DirectDraw-based), fixed-function 3D
# only, real GPU-quad SpriteBatch; same Windows-only/MinGW-cross-compile/Wine-wrapper shape.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX8")
    enable_testing()

    macro(cna_directx8_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
            target_link_options(${target} PRIVATE -Wl,--allow-multiple-definition)
            cna_copy_mingw_runtime(${target})
            cna_copy_sdl_runtime(${target})
        endif()
    endmacro()

    # plans/plan_dx8.md design decision 15: when cross-compiled from Linux (DIRECTX8 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-directx8.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring DirectX1Tests.cmake's own
    # cna_directx1_ctest_command exactly.
    macro(cna_directx8_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-directx8.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plans/plan_dx8.md design decision 16: a real, automated proof this renderer never quietly reaches
    # for any DirectDraw interface at all (DIRECTX8 has no DirectDraw concept -- "DirectDraw+Direct3D
    # merged"), the removed D3DTLVERTEX macro/D3DVT_* old vertex-type enum, or the legacy
    # D3DRENDERSTATE_* render-state naming (DIRECTX8 uses only the modern D3DRS_* names) -- pure text
    # check via scripts/check-directx8-legacy-interface-discipline.sh, no compiled binary, no Wine
    # needed, runs identically whether cross-compiling or not. Registered FIRST, mirroring
    # DirectX1Tests.cmake's own DirectX1_V1OnlyDiscipline placement.
    add_test(NAME DirectX8_LegacyInterfaceDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-directx8-legacy-interface-discipline.sh" "${CMAKE_SOURCE_DIR}")
    # Historical root-scope registration CWD, preserved (see cna_register_renderer_test).
    set_tests_properties(DirectX8_LegacyInterfaceDiscipline PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(DirectX8_LegacyInterfaceDiscipline PROPERTIES LABELS "DIRECTX8")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_directx8_test(cna_test_directx8_smoke directx8_smoke_test.cpp)
    cna_directx8_ctest_command(_directx8_smoke_cmd cna_test_directx8_smoke)
    cna_register_renderer_test(NAME DirectX8_Smoke COMMAND ${_directx8_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target renderer CTest.
    cna_directx8_test(cna_test_directx8_texture_rendertarget directx8_texture_rendertarget_test.cpp)
    cna_directx8_ctest_command(_directx8_texture_rendertarget_cmd cna_test_directx8_texture_rendertarget)
    cna_register_renderer_test(NAME DirectX8_TextureRenderTarget COMMAND ${_directx8_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_directx8_test(cna_test_directx8_spritebatch directx8_spritebatch_test.cpp)
    cna_directx8_ctest_command(_directx8_spritebatch_cmd cna_test_directx8_spritebatch)
    cna_register_renderer_test(NAME DirectX8_SpriteBatch COMMAND ${_directx8_spritebatch_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_directx8_test(cna_test_directx8_blend directx8_blend_test.cpp)
    cna_directx8_ctest_command(_directx8_blend_cmd cna_test_directx8_blend)
    cna_register_renderer_test(NAME DirectX8_Blend COMMAND ${_directx8_blend_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_directx8_test(cna_test_directx8_sampling directx8_sampling_test.cpp)
    cna_directx8_ctest_command(_directx8_sampling_cmd cna_test_directx8_sampling)
    cna_register_renderer_test(NAME DirectX8_AddressMode COMMAND ${_directx8_sampling_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_directx8_test(cna_test_directx8_spritefont directx8_spritefont_test.cpp)
    cna_directx8_ctest_command(_directx8_spritefont_cmd cna_test_directx8_spritefont)
    cna_register_renderer_test(NAME DirectX8_SpriteFont COMMAND ${_directx8_spritefont_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # CNA::GraphicsCapability: DIRECTX8 is fully 3D-capable from day one (a port of DIRECTX3's own, itself a port of DIRECTX2's
    # post-Phase-O9 state -- no "3D lands later" scope gap the way DIRECTX2's own Phase O1/O2 history
    # had) -- SupportsCapability() reports which capabilities are genuinely unavailable at this
    # DirectX era vs. real.
    cna_directx8_test(cna_test_directx8_graphics_capability directx8_graphics_capability_test.cpp)
    cna_directx8_ctest_command(_directx8_graphics_capability_cmd cna_test_directx8_graphics_capability)
    cna_register_renderer_test(NAME DirectX8_GraphicsCapability COMMAND ${_directx8_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_directx8_test(cna_test_directx8_logical_transform directx8_logical_transform_test.cpp)
    cna_directx8_ctest_command(_directx8_logical_transform_cmd cna_test_directx8_logical_transform)
    cna_register_renderer_test(NAME DirectX8_LogicalTransform COMMAND ${_directx8_logical_transform_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # NOTE: DirectX1_No3D has no DIRECTX8 equivalent here either, same reasoning as DIRECTX2/DIRECTX3 -- DIRECTX8's
    # 3D layer is real (not a permanent throw like DIRECTX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_directx8_test(cna_test_directx8_device3d_smoke directx8_device3d_smoke_test.cpp)
    cna_directx8_ctest_command(_directx8_device3d_smoke_cmd cna_test_directx8_device3d_smoke)
    cna_register_renderer_test(NAME DirectX8_Device3DSmoke COMMAND ${_directx8_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O5 (DX2-40..DX2-42): DirectX2VertexBufferRenderer/DirectX2IndexBufferRenderer CTest.
    cna_directx8_test(cna_test_directx8_vertex_index_buffer directx8_vertex_index_buffer_test.cpp)
    cna_directx8_ctest_command(_directx8_vertex_index_buffer_cmd cna_test_directx8_vertex_index_buffer)
    cna_register_renderer_test(NAME DirectX8_VertexIndexBuffer COMMAND ${_directx8_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_directx8_test(cna_test_directx8_colored_primitives directx8_colored_primitives_test.cpp)
    cna_directx8_ctest_command(_directx8_colored_primitives_cmd cna_test_directx8_colored_primitives)
    cna_register_renderer_test(NAME DirectX8_ColoredPrimitives COMMAND ${_directx8_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_directx8_test(cna_test_directx8_indexed_primitives directx8_indexed_primitives_test.cpp)
    cna_directx8_ctest_command(_directx8_indexed_primitives_cmd cna_test_directx8_indexed_primitives)
    cna_register_renderer_test(NAME DirectX8_IndexedPrimitives COMMAND ${_directx8_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_directx8_test(cna_test_directx8_ztest directx8_ztest_test.cpp)
    cna_directx8_ctest_command(_directx8_ztest_cmd cna_test_directx8_ztest)
    cna_register_renderer_test(NAME DirectX8_ZTest COMMAND ${_directx8_ztest_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via SetTexture+SetTextureStageState CTest.
    cna_directx8_test(cna_test_directx8_texture3d directx8_texture3d_test.cpp)
    cna_directx8_ctest_command(_directx8_texture3d_cmd cna_test_directx8_texture3d)
    cna_register_renderer_test(NAME DirectX8_Texture3D COMMAND ${_directx8_texture3d_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_directx8_test(cna_test_directx8_clipping directx8_clipping_test.cpp)
    cna_directx8_ctest_command(_directx8_clipping_cmd cna_test_directx8_clipping)
    cna_register_renderer_test(NAME DirectX8_Clipping COMMAND ${_directx8_clipping_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsRenderer entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_directx8_test(cna_test_directx8_remaining_defaults directx8_remaining_defaults_test.cpp)
    cna_directx8_ctest_command(_directx8_remaining_defaults_cmd cna_test_directx8_remaining_defaults)
    cna_register_renderer_test(NAME DirectX8_RemainingDefaults COMMAND ${_directx8_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_directx8_test(cna_test_directx8_lighting directx8_lighting_test.cpp)
    cna_directx8_ctest_command(_directx8_lighting_cmd cna_test_directx8_lighting)
    cna_register_renderer_test(NAME DirectX8_Lighting COMMAND ${_directx8_lighting_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # WireFrame/AnisotropicFiltering re-verification -- both report true on this renderer (a real
    # DXVK-backed GPU, unlike DIRECTX2..DIRECTX7's own software rasterizer where anisotropic filtering was
    # empirically confirmed absent).
    cna_directx8_test(cna_test_directx8_wireframe_aniso directx8_wireframe_aniso_test.cpp)
    cna_directx8_ctest_command(_directx8_wireframe_aniso_cmd cna_test_directx8_wireframe_aniso)
    cna_register_renderer_test(NAME DirectX8_WireframeAniso COMMAND ${_directx8_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")

    # Phase S6 (plans/plan_dx8.md): real stencil buffer write-then-test through the full XNA public API
    # (GraphicsDevice.DepthStencilState), proving stencil (unchanged from DIRECTX6) survives the removed
    # viewport object -- mirrors the DX8-0 spike's own Test E/F shape (write REPLACE on half the
    # target, then test EQUAL against the full target and confirm the untouched half is correctly
    # rejected).
    cna_directx8_test(cna_test_directx8_stencil directx8_stencil_test.cpp)
    cna_directx8_ctest_command(_directx8_stencil_cmd cna_test_directx8_stencil)
    cna_register_renderer_test(NAME DirectX8_Stencil COMMAND ${_directx8_stencil_cmd}
        TIMEOUT 60 LABELS "DIRECTX8")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
