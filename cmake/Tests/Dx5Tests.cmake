if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "DX5")
    enable_testing()

    macro(cna_dx5_test target src)
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

    # plan_dx5.md design decision 11: when cross-compiled from Linux (DX5 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-dx5.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring Dx1Tests.cmake's own
    # cna_dx1_ctest_command exactly.
    macro(cna_dx5_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dx5.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plan_dx5.md design decision 12: a real, automated proof this backend never quietly reaches
    # for the proven-broken execute-buffer Direct3D path (or the old D3DVT_*-enum vertex-type
    # submission) instead of the working IDirect3DDevice3::DrawPrimitive + D3DFVF_TLVERTEX one --
    # pure text check via scripts/check-dx5-execute-buffer-discipline.sh, no compiled binary, no
    # Wine needed, runs identically whether cross-compiling or not. Registered FIRST, mirroring
    # Dx1Tests.cmake's own Dx1_V1OnlyDiscipline placement.
    add_test(NAME Dx5_ExecuteBufferDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-dx5-execute-buffer-discipline.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Dx5_ExecuteBufferDiscipline PROPERTIES LABELS "DX5")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_dx5_test(cna_test_dx5_smoke examples/dx5_smoke_test.cpp)
    cna_dx5_ctest_command(_dx5_smoke_cmd cna_test_dx5_smoke)
    cna_register_backend_test(NAME Dx5_Smoke COMMAND ${_dx5_smoke_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target backend CTest.
    cna_dx5_test(cna_test_dx5_texture_rendertarget examples/dx5_texture_rendertarget_test.cpp)
    cna_dx5_ctest_command(_dx5_texture_rendertarget_cmd cna_test_dx5_texture_rendertarget)
    cna_register_backend_test(NAME Dx5_TextureRenderTarget COMMAND ${_dx5_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_dx5_test(cna_test_dx5_spritebatch examples/dx5_spritebatch_test.cpp)
    cna_dx5_ctest_command(_dx5_spritebatch_cmd cna_test_dx5_spritebatch)
    cna_register_backend_test(NAME Dx5_SpriteBatch COMMAND ${_dx5_spritebatch_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_dx5_test(cna_test_dx5_blend examples/dx5_blend_test.cpp)
    cna_dx5_ctest_command(_dx5_blend_cmd cna_test_dx5_blend)
    cna_register_backend_test(NAME Dx5_Blend COMMAND ${_dx5_blend_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_dx5_test(cna_test_dx5_sampling examples/dx5_sampling_test.cpp)
    cna_dx5_ctest_command(_dx5_sampling_cmd cna_test_dx5_sampling)
    cna_register_backend_test(NAME Dx5_AddressMode COMMAND ${_dx5_sampling_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_dx5_test(cna_test_dx5_spritefont examples/dx5_spritefont_test.cpp)
    cna_dx5_ctest_command(_dx5_spritefont_cmd cna_test_dx5_spritefont)
    cna_register_backend_test(NAME Dx5_SpriteFont COMMAND ${_dx5_spritefont_cmd}
        TIMEOUT 60 LABELS "DX5")

    # CNA::GraphicsCapability: DX5 is fully 3D-capable from day one (a port of DX30's own, itself a port of DX2's
    # post-Phase-O9 state -- no "3D lands later" scope gap the way DX2's own Phase O1/O2 history
    # had) -- SupportsCapability() reports which capabilities are genuinely unavailable at this
    # DirectX era vs. real.
    cna_dx5_test(cna_test_dx5_graphics_capability examples/dx5_graphics_capability_test.cpp)
    cna_dx5_ctest_command(_dx5_graphics_capability_cmd cna_test_dx5_graphics_capability)
    cna_register_backend_test(NAME Dx5_GraphicsCapability COMMAND ${_dx5_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_dx5_test(cna_test_dx5_logical_transform examples/dx5_logical_transform_test.cpp)
    cna_dx5_ctest_command(_dx5_logical_transform_cmd cna_test_dx5_logical_transform)
    cna_register_backend_test(NAME Dx5_LogicalTransform COMMAND ${_dx5_logical_transform_cmd}
        TIMEOUT 60 LABELS "DX5")

    # NOTE: Dx1_No3D has no DX5 equivalent here either, same reasoning as DX2/DX30 -- DX5's
    # 3D layer is real (not a permanent throw like DX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_dx5_test(cna_test_dx5_device3d_smoke examples/dx5_device3d_smoke_test.cpp)
    cna_dx5_ctest_command(_dx5_device3d_smoke_cmd cna_test_dx5_device3d_smoke)
    cna_register_backend_test(NAME Dx5_Device3DSmoke COMMAND ${_dx5_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O5 (DX2-40..DX2-42): Dx2VertexBufferBackend/Dx2IndexBufferBackend CTest.
    cna_dx5_test(cna_test_dx5_vertex_index_buffer examples/dx5_vertex_index_buffer_test.cpp)
    cna_dx5_ctest_command(_dx5_vertex_index_buffer_cmd cna_test_dx5_vertex_index_buffer)
    cna_register_backend_test(NAME Dx5_VertexIndexBuffer COMMAND ${_dx5_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_dx5_test(cna_test_dx5_colored_primitives examples/dx5_colored_primitives_test.cpp)
    cna_dx5_ctest_command(_dx5_colored_primitives_cmd cna_test_dx5_colored_primitives)
    cna_register_backend_test(NAME Dx5_ColoredPrimitives COMMAND ${_dx5_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_dx5_test(cna_test_dx5_indexed_primitives examples/dx5_indexed_primitives_test.cpp)
    cna_dx5_ctest_command(_dx5_indexed_primitives_cmd cna_test_dx5_indexed_primitives)
    cna_register_backend_test(NAME Dx5_IndexedPrimitives COMMAND ${_dx5_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_dx5_test(cna_test_dx5_ztest examples/dx5_ztest_test.cpp)
    cna_dx5_ctest_command(_dx5_ztest_cmd cna_test_dx5_ztest)
    cna_register_backend_test(NAME Dx5_ZTest COMMAND ${_dx5_ztest_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via D3DRENDERSTATE_TEXTUREHANDLE CTest.
    cna_dx5_test(cna_test_dx5_texture3d examples/dx5_texture3d_test.cpp)
    cna_dx5_ctest_command(_dx5_texture3d_cmd cna_test_dx5_texture3d)
    cna_register_backend_test(NAME Dx5_Texture3D COMMAND ${_dx5_texture3d_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_dx5_test(cna_test_dx5_clipping examples/dx5_clipping_test.cpp)
    cna_dx5_ctest_command(_dx5_clipping_cmd cna_test_dx5_clipping)
    cna_register_backend_test(NAME Dx5_Clipping COMMAND ${_dx5_clipping_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsBackend entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_dx5_test(cna_test_dx5_remaining_defaults examples/dx5_remaining_defaults_test.cpp)
    cna_dx5_ctest_command(_dx5_remaining_defaults_cmd cna_test_dx5_remaining_defaults)
    cna_register_backend_test(NAME Dx5_RemainingDefaults COMMAND ${_dx5_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_dx5_test(cna_test_dx5_lighting examples/dx5_lighting_test.cpp)
    cna_dx5_ctest_command(_dx5_lighting_cmd cna_test_dx5_lighting)
    cna_register_backend_test(NAME Dx5_Lighting COMMAND ${_dx5_lighting_cmd}
        TIMEOUT 60 LABELS "DX5")

    # Phase O9 (DX2-95, DX2-97): WireFrame/AnisotropicFiltering re-verification -- WireFrame now
    # real (SupportsCapability flipped true), AnisotropicFiltering empirically confirmed absent.
    cna_dx5_test(cna_test_dx5_wireframe_aniso examples/dx5_wireframe_aniso_test.cpp)
    cna_dx5_ctest_command(_dx5_wireframe_aniso_cmd cna_test_dx5_wireframe_aniso)
    cna_register_backend_test(NAME Dx5_WireframeAniso COMMAND ${_dx5_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DX5")
endif()
