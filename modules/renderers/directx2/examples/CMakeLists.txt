# directx2 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX2Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_dx2.md Phase O1/O2: DIRECTX2 (real DirectDraw v1 2D layer ported from DIRECTX1 + Direct3D v2
# DrawPrimitive 3D layer, landing in a later phase) CTest registration, mirroring DirectX1Tests.cmake's
# own shape (Windows-only, MinGW cross-compile + Wine wrapper).

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX2")
    enable_testing()

    macro(cna_directx2_test target src)
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

    # plans/plan_dx2.md design decision 11: when cross-compiled from Linux (DIRECTX2 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-directx2.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring DirectX1Tests.cmake's own
    # cna_directx1_ctest_command exactly.
    macro(cna_directx2_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-directx2.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plans/plan_dx2.md design decision 12: a real, automated proof this renderer never quietly reaches
    # for the proven-broken execute-buffer Direct3D path instead of the working
    # IDirect3DDevice2::DrawPrimitive one -- pure text check via
    # scripts/check-directx2-execute-buffer-discipline.sh, no compiled binary, no Wine needed, runs
    # identically whether cross-compiling or not. Registered FIRST, mirroring DirectX1Tests.cmake's own
    # DirectX1_V1OnlyDiscipline placement.
    add_test(NAME DirectX2_ExecuteBufferDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-directx2-execute-buffer-discipline.sh" "${CMAKE_SOURCE_DIR}")
    # Historical root-scope registration CWD, preserved (see cna_register_renderer_test).
    set_tests_properties(DirectX2_ExecuteBufferDiscipline PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(DirectX2_ExecuteBufferDiscipline PROPERTIES LABELS "DIRECTX2")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_directx2_test(cna_test_directx2_smoke directx2_smoke_test.cpp)
    cna_directx2_ctest_command(_directx2_smoke_cmd cna_test_directx2_smoke)
    cna_register_renderer_test(NAME DirectX2_Smoke COMMAND ${_directx2_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target renderer CTest.
    cna_directx2_test(cna_test_directx2_texture_rendertarget directx2_texture_rendertarget_test.cpp)
    cna_directx2_ctest_command(_directx2_texture_rendertarget_cmd cna_test_directx2_texture_rendertarget)
    cna_register_renderer_test(NAME DirectX2_TextureRenderTarget COMMAND ${_directx2_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_directx2_test(cna_test_directx2_spritebatch directx2_spritebatch_test.cpp)
    cna_directx2_ctest_command(_directx2_spritebatch_cmd cna_test_directx2_spritebatch)
    cna_register_renderer_test(NAME DirectX2_SpriteBatch COMMAND ${_directx2_spritebatch_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_directx2_test(cna_test_directx2_blend directx2_blend_test.cpp)
    cna_directx2_ctest_command(_directx2_blend_cmd cna_test_directx2_blend)
    cna_register_renderer_test(NAME DirectX2_Blend COMMAND ${_directx2_blend_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_directx2_test(cna_test_directx2_sampling directx2_sampling_test.cpp)
    cna_directx2_ctest_command(_directx2_sampling_cmd cna_test_directx2_sampling)
    cna_register_renderer_test(NAME DirectX2_AddressMode COMMAND ${_directx2_sampling_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_directx2_test(cna_test_directx2_spritefont directx2_spritefont_test.cpp)
    cna_directx2_ctest_command(_directx2_spritefont_cmd cna_test_directx2_spritefont)
    cna_register_renderer_test(NAME DirectX2_SpriteFont COMMAND ${_directx2_spritefont_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # CNA::GraphicsCapability: DIRECTX2 is 2D-only for now (Phase O1/O2 scope -- 3D lands in a later
    # phase, unlike DIRECTX1's permanent 2D-only boundary) -- SupportsCapability() reports which
    # capabilities are not yet supported.
    cna_directx2_test(cna_test_directx2_graphics_capability directx2_graphics_capability_test.cpp)
    cna_directx2_ctest_command(_directx2_graphics_capability_cmd cna_test_directx2_graphics_capability)
    cna_register_renderer_test(NAME DirectX2_GraphicsCapability COMMAND ${_directx2_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_directx2_test(cna_test_directx2_logical_transform directx2_logical_transform_test.cpp)
    cna_directx2_ctest_command(_directx2_logical_transform_cmd cna_test_directx2_logical_transform)
    cna_register_renderer_test(NAME DirectX2_LogicalTransform COMMAND ${_directx2_logical_transform_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # NOTE: DirectX1_No3D has no DIRECTX2 equivalent here by design (plans/plan_dx2.md task instructions) -- DIRECTX2's
    # 3D layer is real (not a permanent throw like DIRECTX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_directx2_test(cna_test_directx2_device3d_smoke directx2_device3d_smoke_test.cpp)
    cna_directx2_ctest_command(_directx2_device3d_smoke_cmd cna_test_directx2_device3d_smoke)
    cna_register_renderer_test(NAME DirectX2_Device3DSmoke COMMAND ${_directx2_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O5 (DX2-40..DX2-42): DirectX2VertexBufferRenderer/DirectX2IndexBufferRenderer CTest.
    cna_directx2_test(cna_test_directx2_vertex_index_buffer directx2_vertex_index_buffer_test.cpp)
    cna_directx2_ctest_command(_directx2_vertex_index_buffer_cmd cna_test_directx2_vertex_index_buffer)
    cna_register_renderer_test(NAME DirectX2_VertexIndexBuffer COMMAND ${_directx2_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_directx2_test(cna_test_directx2_colored_primitives directx2_colored_primitives_test.cpp)
    cna_directx2_ctest_command(_directx2_colored_primitives_cmd cna_test_directx2_colored_primitives)
    cna_register_renderer_test(NAME DirectX2_ColoredPrimitives COMMAND ${_directx2_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_directx2_test(cna_test_directx2_indexed_primitives directx2_indexed_primitives_test.cpp)
    cna_directx2_ctest_command(_directx2_indexed_primitives_cmd cna_test_directx2_indexed_primitives)
    cna_register_renderer_test(NAME DirectX2_IndexedPrimitives COMMAND ${_directx2_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_directx2_test(cna_test_directx2_ztest directx2_ztest_test.cpp)
    cna_directx2_ctest_command(_directx2_ztest_cmd cna_test_directx2_ztest)
    cna_register_renderer_test(NAME DirectX2_ZTest COMMAND ${_directx2_ztest_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via D3DRENDERSTATE_TEXTUREHANDLE CTest.
    cna_directx2_test(cna_test_directx2_texture3d directx2_texture3d_test.cpp)
    cna_directx2_ctest_command(_directx2_texture3d_cmd cna_test_directx2_texture3d)
    cna_register_renderer_test(NAME DirectX2_Texture3D COMMAND ${_directx2_texture3d_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_directx2_test(cna_test_directx2_clipping directx2_clipping_test.cpp)
    cna_directx2_ctest_command(_directx2_clipping_cmd cna_test_directx2_clipping)
    cna_register_renderer_test(NAME DirectX2_Clipping COMMAND ${_directx2_clipping_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsRenderer entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_directx2_test(cna_test_directx2_remaining_defaults directx2_remaining_defaults_test.cpp)
    cna_directx2_ctest_command(_directx2_remaining_defaults_cmd cna_test_directx2_remaining_defaults)
    cna_register_renderer_test(NAME DirectX2_RemainingDefaults COMMAND ${_directx2_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_directx2_test(cna_test_directx2_lighting directx2_lighting_test.cpp)
    cna_directx2_ctest_command(_directx2_lighting_cmd cna_test_directx2_lighting)
    cna_register_renderer_test(NAME DirectX2_Lighting COMMAND ${_directx2_lighting_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")

    # Phase O9 (DX2-95, DX2-97): WireFrame/AnisotropicFiltering re-verification -- WireFrame now
    # real (SupportsCapability flipped true), AnisotropicFiltering empirically confirmed absent.
    cna_directx2_test(cna_test_directx2_wireframe_aniso directx2_wireframe_aniso_test.cpp)
    cna_directx2_ctest_command(_directx2_wireframe_aniso_cmd cna_test_directx2_wireframe_aniso)
    cna_register_renderer_test(NAME DirectX2_WireframeAniso COMMAND ${_directx2_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DIRECTX2")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
