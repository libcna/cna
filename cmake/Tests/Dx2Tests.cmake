if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DX2")
    enable_testing()

    macro(cna_dx2_test target src)
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

    # plan_dx2.md design decision 11: when cross-compiled from Linux (DX2 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-dx2.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring Dx1Tests.cmake's own
    # cna_dx1_ctest_command exactly.
    macro(cna_dx2_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dx2.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plan_dx2.md design decision 12: a real, automated proof this renderer never quietly reaches
    # for the proven-broken execute-buffer Direct3D path instead of the working
    # IDirect3DDevice2::DrawPrimitive one -- pure text check via
    # scripts/check-dx2-execute-buffer-discipline.sh, no compiled binary, no Wine needed, runs
    # identically whether cross-compiling or not. Registered FIRST, mirroring Dx1Tests.cmake's own
    # Dx1_V1OnlyDiscipline placement.
    add_test(NAME Dx2_ExecuteBufferDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-dx2-execute-buffer-discipline.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Dx2_ExecuteBufferDiscipline PROPERTIES LABELS "DX2")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_dx2_test(cna_test_dx2_smoke examples/dx2_smoke_test.cpp)
    cna_dx2_ctest_command(_dx2_smoke_cmd cna_test_dx2_smoke)
    cna_register_renderer_test(NAME Dx2_Smoke COMMAND ${_dx2_smoke_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target renderer CTest.
    cna_dx2_test(cna_test_dx2_texture_rendertarget examples/dx2_texture_rendertarget_test.cpp)
    cna_dx2_ctest_command(_dx2_texture_rendertarget_cmd cna_test_dx2_texture_rendertarget)
    cna_register_renderer_test(NAME Dx2_TextureRenderTarget COMMAND ${_dx2_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_dx2_test(cna_test_dx2_spritebatch examples/dx2_spritebatch_test.cpp)
    cna_dx2_ctest_command(_dx2_spritebatch_cmd cna_test_dx2_spritebatch)
    cna_register_renderer_test(NAME Dx2_SpriteBatch COMMAND ${_dx2_spritebatch_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_dx2_test(cna_test_dx2_blend examples/dx2_blend_test.cpp)
    cna_dx2_ctest_command(_dx2_blend_cmd cna_test_dx2_blend)
    cna_register_renderer_test(NAME Dx2_Blend COMMAND ${_dx2_blend_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_dx2_test(cna_test_dx2_sampling examples/dx2_sampling_test.cpp)
    cna_dx2_ctest_command(_dx2_sampling_cmd cna_test_dx2_sampling)
    cna_register_renderer_test(NAME Dx2_AddressMode COMMAND ${_dx2_sampling_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_dx2_test(cna_test_dx2_spritefont examples/dx2_spritefont_test.cpp)
    cna_dx2_ctest_command(_dx2_spritefont_cmd cna_test_dx2_spritefont)
    cna_register_renderer_test(NAME Dx2_SpriteFont COMMAND ${_dx2_spritefont_cmd}
        TIMEOUT 60 LABELS "DX2")

    # CNA::GraphicsCapability: DX2 is 2D-only for now (Phase O1/O2 scope -- 3D lands in a later
    # phase, unlike DX1's permanent 2D-only boundary) -- SupportsCapability() reports which
    # capabilities are not yet supported.
    cna_dx2_test(cna_test_dx2_graphics_capability examples/dx2_graphics_capability_test.cpp)
    cna_dx2_ctest_command(_dx2_graphics_capability_cmd cna_test_dx2_graphics_capability)
    cna_register_renderer_test(NAME Dx2_GraphicsCapability COMMAND ${_dx2_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_dx2_test(cna_test_dx2_logical_transform examples/dx2_logical_transform_test.cpp)
    cna_dx2_ctest_command(_dx2_logical_transform_cmd cna_test_dx2_logical_transform)
    cna_register_renderer_test(NAME Dx2_LogicalTransform COMMAND ${_dx2_logical_transform_cmd}
        TIMEOUT 60 LABELS "DX2")

    # NOTE: Dx1_No3D has no DX2 equivalent here by design (plan_dx2.md task instructions) -- DX2's
    # 3D layer is real (not a permanent throw like DX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_dx2_test(cna_test_dx2_device3d_smoke examples/dx2_device3d_smoke_test.cpp)
    cna_dx2_ctest_command(_dx2_device3d_smoke_cmd cna_test_dx2_device3d_smoke)
    cna_register_renderer_test(NAME Dx2_Device3DSmoke COMMAND ${_dx2_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O5 (DX2-40..DX2-42): Dx2VertexBufferRenderer/Dx2IndexBufferRenderer CTest.
    cna_dx2_test(cna_test_dx2_vertex_index_buffer examples/dx2_vertex_index_buffer_test.cpp)
    cna_dx2_ctest_command(_dx2_vertex_index_buffer_cmd cna_test_dx2_vertex_index_buffer)
    cna_register_renderer_test(NAME Dx2_VertexIndexBuffer COMMAND ${_dx2_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_dx2_test(cna_test_dx2_colored_primitives examples/dx2_colored_primitives_test.cpp)
    cna_dx2_ctest_command(_dx2_colored_primitives_cmd cna_test_dx2_colored_primitives)
    cna_register_renderer_test(NAME Dx2_ColoredPrimitives COMMAND ${_dx2_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_dx2_test(cna_test_dx2_indexed_primitives examples/dx2_indexed_primitives_test.cpp)
    cna_dx2_ctest_command(_dx2_indexed_primitives_cmd cna_test_dx2_indexed_primitives)
    cna_register_renderer_test(NAME Dx2_IndexedPrimitives COMMAND ${_dx2_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_dx2_test(cna_test_dx2_ztest examples/dx2_ztest_test.cpp)
    cna_dx2_ctest_command(_dx2_ztest_cmd cna_test_dx2_ztest)
    cna_register_renderer_test(NAME Dx2_ZTest COMMAND ${_dx2_ztest_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via D3DRENDERSTATE_TEXTUREHANDLE CTest.
    cna_dx2_test(cna_test_dx2_texture3d examples/dx2_texture3d_test.cpp)
    cna_dx2_ctest_command(_dx2_texture3d_cmd cna_test_dx2_texture3d)
    cna_register_renderer_test(NAME Dx2_Texture3D COMMAND ${_dx2_texture3d_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_dx2_test(cna_test_dx2_clipping examples/dx2_clipping_test.cpp)
    cna_dx2_ctest_command(_dx2_clipping_cmd cna_test_dx2_clipping)
    cna_register_renderer_test(NAME Dx2_Clipping COMMAND ${_dx2_clipping_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsRenderer entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_dx2_test(cna_test_dx2_remaining_defaults examples/dx2_remaining_defaults_test.cpp)
    cna_dx2_ctest_command(_dx2_remaining_defaults_cmd cna_test_dx2_remaining_defaults)
    cna_register_renderer_test(NAME Dx2_RemainingDefaults COMMAND ${_dx2_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_dx2_test(cna_test_dx2_lighting examples/dx2_lighting_test.cpp)
    cna_dx2_ctest_command(_dx2_lighting_cmd cna_test_dx2_lighting)
    cna_register_renderer_test(NAME Dx2_Lighting COMMAND ${_dx2_lighting_cmd}
        TIMEOUT 60 LABELS "DX2")

    # Phase O9 (DX2-95, DX2-97): WireFrame/AnisotropicFiltering re-verification -- WireFrame now
    # real (SupportsCapability flipped true), AnisotropicFiltering empirically confirmed absent.
    cna_dx2_test(cna_test_dx2_wireframe_aniso examples/dx2_wireframe_aniso_test.cpp)
    cna_dx2_ctest_command(_dx2_wireframe_aniso_cmd cna_test_dx2_wireframe_aniso)
    cna_register_renderer_test(NAME Dx2_WireframeAniso COMMAND ${_dx2_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DX2")
endif()
