# directx7 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX7Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_dx7.md: real DirectX 7 renderer -- new IDirectDraw7/IDirect3D7 interfaces, viewport object
# removed, texture binding simplified; same Windows-only/MinGW-cross-compile/Wine-wrapper shape.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX7")
    enable_testing()

    macro(cna_directx7_test target src)
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

    # plans/plan_dx7.md design decision 15: when cross-compiled from Linux (DIRECTX7 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-directx7.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring DirectX1Tests.cmake's own
    # cna_directx1_ctest_command exactly.
    macro(cna_directx7_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-directx7.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plans/plan_dx7.md design decision 16: a real, automated proof this renderer never quietly reaches
    # for the proven-broken execute-buffer Direct3D path, the old D3DVT_*-enum vertex-type
    # submission, any pre-v7 DirectDraw/Direct3D interface, the removed viewport object, or the old
    # texture-handle mechanism -- pure text check via scripts/check-directx7-execute-buffer-discipline.sh,
    # no compiled binary, no Wine needed, runs identically whether cross-compiling or not.
    # Registered FIRST, mirroring DirectX1Tests.cmake's own DirectX1_V1OnlyDiscipline placement.
    add_test(NAME DirectX7_ExecuteBufferDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-directx7-execute-buffer-discipline.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(DirectX7_ExecuteBufferDiscipline PROPERTIES LABELS "DIRECTX7")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_directx7_test(cna_test_directx7_smoke directx7_smoke_test.cpp)
    cna_directx7_ctest_command(_directx7_smoke_cmd cna_test_directx7_smoke)
    cna_register_renderer_test(NAME DirectX7_Smoke COMMAND ${_directx7_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target renderer CTest.
    cna_directx7_test(cna_test_directx7_texture_rendertarget directx7_texture_rendertarget_test.cpp)
    cna_directx7_ctest_command(_directx7_texture_rendertarget_cmd cna_test_directx7_texture_rendertarget)
    cna_register_renderer_test(NAME DirectX7_TextureRenderTarget COMMAND ${_directx7_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_directx7_test(cna_test_directx7_spritebatch directx7_spritebatch_test.cpp)
    cna_directx7_ctest_command(_directx7_spritebatch_cmd cna_test_directx7_spritebatch)
    cna_register_renderer_test(NAME DirectX7_SpriteBatch COMMAND ${_directx7_spritebatch_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_directx7_test(cna_test_directx7_blend directx7_blend_test.cpp)
    cna_directx7_ctest_command(_directx7_blend_cmd cna_test_directx7_blend)
    cna_register_renderer_test(NAME DirectX7_Blend COMMAND ${_directx7_blend_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_directx7_test(cna_test_directx7_sampling directx7_sampling_test.cpp)
    cna_directx7_ctest_command(_directx7_sampling_cmd cna_test_directx7_sampling)
    cna_register_renderer_test(NAME DirectX7_AddressMode COMMAND ${_directx7_sampling_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_directx7_test(cna_test_directx7_spritefont directx7_spritefont_test.cpp)
    cna_directx7_ctest_command(_directx7_spritefont_cmd cna_test_directx7_spritefont)
    cna_register_renderer_test(NAME DirectX7_SpriteFont COMMAND ${_directx7_spritefont_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # CNA::GraphicsCapability: DIRECTX7 is fully 3D-capable from day one (a port of DIRECTX3's own, itself a port of DIRECTX2's
    # post-Phase-O9 state -- no "3D lands later" scope gap the way DIRECTX2's own Phase O1/O2 history
    # had) -- SupportsCapability() reports which capabilities are genuinely unavailable at this
    # DirectX era vs. real.
    cna_directx7_test(cna_test_directx7_graphics_capability directx7_graphics_capability_test.cpp)
    cna_directx7_ctest_command(_directx7_graphics_capability_cmd cna_test_directx7_graphics_capability)
    cna_register_renderer_test(NAME DirectX7_GraphicsCapability COMMAND ${_directx7_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_directx7_test(cna_test_directx7_logical_transform directx7_logical_transform_test.cpp)
    cna_directx7_ctest_command(_directx7_logical_transform_cmd cna_test_directx7_logical_transform)
    cna_register_renderer_test(NAME DirectX7_LogicalTransform COMMAND ${_directx7_logical_transform_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # NOTE: DirectX1_No3D has no DIRECTX7 equivalent here either, same reasoning as DIRECTX2/DIRECTX3 -- DIRECTX7's
    # 3D layer is real (not a permanent throw like DIRECTX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_directx7_test(cna_test_directx7_device3d_smoke directx7_device3d_smoke_test.cpp)
    cna_directx7_ctest_command(_directx7_device3d_smoke_cmd cna_test_directx7_device3d_smoke)
    cna_register_renderer_test(NAME DirectX7_Device3DSmoke COMMAND ${_directx7_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O5 (DX2-40..DX2-42): DirectX2VertexBufferRenderer/DirectX2IndexBufferRenderer CTest.
    cna_directx7_test(cna_test_directx7_vertex_index_buffer directx7_vertex_index_buffer_test.cpp)
    cna_directx7_ctest_command(_directx7_vertex_index_buffer_cmd cna_test_directx7_vertex_index_buffer)
    cna_register_renderer_test(NAME DirectX7_VertexIndexBuffer COMMAND ${_directx7_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_directx7_test(cna_test_directx7_colored_primitives directx7_colored_primitives_test.cpp)
    cna_directx7_ctest_command(_directx7_colored_primitives_cmd cna_test_directx7_colored_primitives)
    cna_register_renderer_test(NAME DirectX7_ColoredPrimitives COMMAND ${_directx7_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_directx7_test(cna_test_directx7_indexed_primitives directx7_indexed_primitives_test.cpp)
    cna_directx7_ctest_command(_directx7_indexed_primitives_cmd cna_test_directx7_indexed_primitives)
    cna_register_renderer_test(NAME DirectX7_IndexedPrimitives COMMAND ${_directx7_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_directx7_test(cna_test_directx7_ztest directx7_ztest_test.cpp)
    cna_directx7_ctest_command(_directx7_ztest_cmd cna_test_directx7_ztest)
    cna_register_renderer_test(NAME DirectX7_ZTest COMMAND ${_directx7_ztest_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via D3DRENDERSTATE_TEXTUREHANDLE CTest.
    cna_directx7_test(cna_test_directx7_texture3d directx7_texture3d_test.cpp)
    cna_directx7_ctest_command(_directx7_texture3d_cmd cna_test_directx7_texture3d)
    cna_register_renderer_test(NAME DirectX7_Texture3D COMMAND ${_directx7_texture3d_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_directx7_test(cna_test_directx7_clipping directx7_clipping_test.cpp)
    cna_directx7_ctest_command(_directx7_clipping_cmd cna_test_directx7_clipping)
    cna_register_renderer_test(NAME DirectX7_Clipping COMMAND ${_directx7_clipping_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsRenderer entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_directx7_test(cna_test_directx7_remaining_defaults directx7_remaining_defaults_test.cpp)
    cna_directx7_ctest_command(_directx7_remaining_defaults_cmd cna_test_directx7_remaining_defaults)
    cna_register_renderer_test(NAME DirectX7_RemainingDefaults COMMAND ${_directx7_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_directx7_test(cna_test_directx7_lighting directx7_lighting_test.cpp)
    cna_directx7_ctest_command(_directx7_lighting_cmd cna_test_directx7_lighting)
    cna_register_renderer_test(NAME DirectX7_Lighting COMMAND ${_directx7_lighting_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase O9 (DX2-95, DX2-97): WireFrame/AnisotropicFiltering re-verification -- WireFrame now
    # real (SupportsCapability flipped true), AnisotropicFiltering empirically confirmed absent.
    cna_directx7_test(cna_test_directx7_wireframe_aniso directx7_wireframe_aniso_test.cpp)
    cna_directx7_ctest_command(_directx7_wireframe_aniso_cmd cna_test_directx7_wireframe_aniso)
    cna_register_renderer_test(NAME DirectX7_WireframeAniso COMMAND ${_directx7_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")

    # Phase S6 (plans/plan_dx7.md): real stencil buffer write-then-test through the full XNA public API
    # (GraphicsDevice.DepthStencilState), proving stencil (unchanged from DIRECTX6) survives the removed
    # viewport object -- mirrors the DX7-0 spike's own Test E/F shape (write REPLACE on half the
    # target, then test EQUAL against the full target and confirm the untouched half is correctly
    # rejected).
    cna_directx7_test(cna_test_directx7_stencil directx7_stencil_test.cpp)
    cna_directx7_ctest_command(_directx7_stencil_cmd cna_test_directx7_stencil)
    cna_register_renderer_test(NAME DirectX7_Stencil COMMAND ${_directx7_stencil_cmd}
        TIMEOUT 60 LABELS "DIRECTX7")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
