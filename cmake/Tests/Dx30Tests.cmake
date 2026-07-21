if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "DX30")
    enable_testing()

    macro(cna_dx30_test target src)
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

    # plan_dx30.md design decision 8: when cross-compiled from Linux (DX30 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-dx30.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring Dx1Tests.cmake's own
    # cna_dx1_ctest_command exactly.
    macro(cna_dx30_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dx30.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plan_dx30.md design decision 9: a real, automated proof this backend never quietly reaches
    # for the proven-broken execute-buffer Direct3D path instead of the working
    # IDirect3DDevice2::DrawPrimitive one -- pure text check via
    # scripts/check-dx30-execute-buffer-discipline.sh, no compiled binary, no Wine needed, runs
    # identically whether cross-compiling or not. Registered FIRST, mirroring Dx1Tests.cmake's own
    # Dx1_V1OnlyDiscipline placement.
    add_test(NAME Dx30_ExecuteBufferDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-dx30-execute-buffer-discipline.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Dx30_ExecuteBufferDiscipline PROPERTIES LABELS "DX30")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_dx30_test(cna_test_dx30_smoke examples/dx30_smoke_test.cpp)
    cna_dx30_ctest_command(_dx30_smoke_cmd cna_test_dx30_smoke)
    cna_register_backend_test(NAME Dx30_Smoke COMMAND ${_dx30_smoke_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target backend CTest.
    cna_dx30_test(cna_test_dx30_texture_rendertarget examples/dx30_texture_rendertarget_test.cpp)
    cna_dx30_ctest_command(_dx30_texture_rendertarget_cmd cna_test_dx30_texture_rendertarget)
    cna_register_backend_test(NAME Dx30_TextureRenderTarget COMMAND ${_dx30_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_dx30_test(cna_test_dx30_spritebatch examples/dx30_spritebatch_test.cpp)
    cna_dx30_ctest_command(_dx30_spritebatch_cmd cna_test_dx30_spritebatch)
    cna_register_backend_test(NAME Dx30_SpriteBatch COMMAND ${_dx30_spritebatch_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_dx30_test(cna_test_dx30_blend examples/dx30_blend_test.cpp)
    cna_dx30_ctest_command(_dx30_blend_cmd cna_test_dx30_blend)
    cna_register_backend_test(NAME Dx30_Blend COMMAND ${_dx30_blend_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_dx30_test(cna_test_dx30_sampling examples/dx30_sampling_test.cpp)
    cna_dx30_ctest_command(_dx30_sampling_cmd cna_test_dx30_sampling)
    cna_register_backend_test(NAME Dx30_AddressMode COMMAND ${_dx30_sampling_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_dx30_test(cna_test_dx30_spritefont examples/dx30_spritefont_test.cpp)
    cna_dx30_ctest_command(_dx30_spritefont_cmd cna_test_dx30_spritefont)
    cna_register_backend_test(NAME Dx30_SpriteFont COMMAND ${_dx30_spritefont_cmd}
        TIMEOUT 60 LABELS "DX30")

    # CNA::GraphicsCapability: DX30 is fully 3D-capable from day one (a verbatim port of DX2's own
    # post-Phase-O9 state -- no "3D lands later" scope gap the way DX2's own Phase O1/O2 history
    # had) -- SupportsCapability() reports which capabilities are genuinely unavailable at this
    # DirectX era vs. real.
    cna_dx30_test(cna_test_dx30_graphics_capability examples/dx30_graphics_capability_test.cpp)
    cna_dx30_ctest_command(_dx30_graphics_capability_cmd cna_test_dx30_graphics_capability)
    cna_register_backend_test(NAME Dx30_GraphicsCapability COMMAND ${_dx30_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_dx30_test(cna_test_dx30_logical_transform examples/dx30_logical_transform_test.cpp)
    cna_dx30_ctest_command(_dx30_logical_transform_cmd cna_test_dx30_logical_transform)
    cna_register_backend_test(NAME Dx30_LogicalTransform COMMAND ${_dx30_logical_transform_cmd}
        TIMEOUT 60 LABELS "DX30")

    # NOTE: Dx1_No3D has no DX30 equivalent here either, same reasoning as DX2 (plan_dx2.md task instructions) -- DX30's
    # 3D layer is real (not a permanent throw like DX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_dx30_test(cna_test_dx30_device3d_smoke examples/dx30_device3d_smoke_test.cpp)
    cna_dx30_ctest_command(_dx30_device3d_smoke_cmd cna_test_dx30_device3d_smoke)
    cna_register_backend_test(NAME Dx30_Device3DSmoke COMMAND ${_dx30_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O5 (DX2-40..DX2-42): Dx2VertexBufferBackend/Dx2IndexBufferBackend CTest.
    cna_dx30_test(cna_test_dx30_vertex_index_buffer examples/dx30_vertex_index_buffer_test.cpp)
    cna_dx30_ctest_command(_dx30_vertex_index_buffer_cmd cna_test_dx30_vertex_index_buffer)
    cna_register_backend_test(NAME Dx30_VertexIndexBuffer COMMAND ${_dx30_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_dx30_test(cna_test_dx30_colored_primitives examples/dx30_colored_primitives_test.cpp)
    cna_dx30_ctest_command(_dx30_colored_primitives_cmd cna_test_dx30_colored_primitives)
    cna_register_backend_test(NAME Dx30_ColoredPrimitives COMMAND ${_dx30_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_dx30_test(cna_test_dx30_indexed_primitives examples/dx30_indexed_primitives_test.cpp)
    cna_dx30_ctest_command(_dx30_indexed_primitives_cmd cna_test_dx30_indexed_primitives)
    cna_register_backend_test(NAME Dx30_IndexedPrimitives COMMAND ${_dx30_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_dx30_test(cna_test_dx30_ztest examples/dx30_ztest_test.cpp)
    cna_dx30_ctest_command(_dx30_ztest_cmd cna_test_dx30_ztest)
    cna_register_backend_test(NAME Dx30_ZTest COMMAND ${_dx30_ztest_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via D3DRENDERSTATE_TEXTUREHANDLE CTest.
    cna_dx30_test(cna_test_dx30_texture3d examples/dx30_texture3d_test.cpp)
    cna_dx30_ctest_command(_dx30_texture3d_cmd cna_test_dx30_texture3d)
    cna_register_backend_test(NAME Dx30_Texture3D COMMAND ${_dx30_texture3d_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_dx30_test(cna_test_dx30_clipping examples/dx30_clipping_test.cpp)
    cna_dx30_ctest_command(_dx30_clipping_cmd cna_test_dx30_clipping)
    cna_register_backend_test(NAME Dx30_Clipping COMMAND ${_dx30_clipping_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsBackend entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_dx30_test(cna_test_dx30_remaining_defaults examples/dx30_remaining_defaults_test.cpp)
    cna_dx30_ctest_command(_dx30_remaining_defaults_cmd cna_test_dx30_remaining_defaults)
    cna_register_backend_test(NAME Dx30_RemainingDefaults COMMAND ${_dx30_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_dx30_test(cna_test_dx30_lighting examples/dx30_lighting_test.cpp)
    cna_dx30_ctest_command(_dx30_lighting_cmd cna_test_dx30_lighting)
    cna_register_backend_test(NAME Dx30_Lighting COMMAND ${_dx30_lighting_cmd}
        TIMEOUT 60 LABELS "DX30")

    # Phase O9 (DX2-95, DX2-97): WireFrame/AnisotropicFiltering re-verification -- WireFrame now
    # real (SupportsCapability flipped true), AnisotropicFiltering empirically confirmed absent.
    cna_dx30_test(cna_test_dx30_wireframe_aniso examples/dx30_wireframe_aniso_test.cpp)
    cna_dx30_ctest_command(_dx30_wireframe_aniso_cmd cna_test_dx30_wireframe_aniso)
    cna_register_backend_test(NAME Dx30_WireframeAniso COMMAND ${_dx30_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DX30")
endif()
