if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "DX8")
    enable_testing()

    macro(cna_dx8_test target src)
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

    # plan_dx8.md design decision 15: when cross-compiled from Linux (DX8 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-dx8.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring Dx1Tests.cmake's own
    # cna_dx1_ctest_command exactly.
    macro(cna_dx8_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dx8.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # plan_dx8.md design decision 16: a real, automated proof this backend never quietly reaches
    # for any DirectDraw interface at all (DX8 has no DirectDraw concept -- "DirectDraw+Direct3D
    # merged"), the removed D3DTLVERTEX macro/D3DVT_* old vertex-type enum, or the legacy
    # D3DRENDERSTATE_* render-state naming (DX8 uses only the modern D3DRS_* names) -- pure text
    # check via scripts/check-dx8-legacy-interface-discipline.sh, no compiled binary, no Wine
    # needed, runs identically whether cross-compiling or not. Registered FIRST, mirroring
    # Dx1Tests.cmake's own Dx1_V1OnlyDiscipline placement.
    add_test(NAME Dx8_LegacyInterfaceDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-dx8-legacy-interface-discipline.sh" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Dx8_LegacyInterfaceDiscipline PROPERTIES LABELS "DX8")

    # Phase O2 (DX2-10, 2D layer ported from DX1-10..DX1-18): device/window bring-up +
    # Clear/Present/readback foundation CTest.
    cna_dx8_test(cna_test_dx8_smoke examples/dx8_smoke_test.cpp)
    cna_dx8_ctest_command(_dx8_smoke_cmd cna_test_dx8_smoke)
    cna_register_backend_test(NAME Dx8_Smoke COMMAND ${_dx8_smoke_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O2 (DX2-11, 2D layer ported from DX1-20..DX1-28): texture/render-target backend CTest.
    cna_dx8_test(cna_test_dx8_texture_rendertarget examples/dx8_texture_rendertarget_test.cpp)
    cna_dx8_ctest_command(_dx8_texture_rendertarget_cmd cna_test_dx8_texture_rendertarget)
    cna_register_backend_test(NAME Dx8_TextureRenderTarget COMMAND ${_dx8_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O2 (DX2-12, 2D layer ported from DX1-30..DX1-39): CPU compositor / SpriteBatch draw
    # path CTest.
    cna_dx8_test(cna_test_dx8_spritebatch examples/dx8_spritebatch_test.cpp)
    cna_dx8_ctest_command(_dx8_spritebatch_cmd cna_test_dx8_spritebatch)
    cna_register_backend_test(NAME Dx8_SpriteBatch COMMAND ${_dx8_spritebatch_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O2 (DX2-12, 2D layer ported from DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_dx8_test(cna_test_dx8_blend examples/dx8_blend_test.cpp)
    cna_dx8_ctest_command(_dx8_blend_cmd cna_test_dx8_blend)
    cna_register_backend_test(NAME Dx8_Blend COMMAND ${_dx8_blend_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter + TextureAddressMode
    # sampling CTest.
    cna_dx8_test(cna_test_dx8_sampling examples/dx8_sampling_test.cpp)
    cna_dx8_ctest_command(_dx8_sampling_cmd cna_test_dx8_sampling)
    cna_register_backend_test(NAME Dx8_AddressMode COMMAND ${_dx8_sampling_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O2 (DX2-13, 2D layer ported from DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_dx8_test(cna_test_dx8_spritefont examples/dx8_spritefont_test.cpp)
    cna_dx8_ctest_command(_dx8_spritefont_cmd cna_test_dx8_spritefont)
    cna_register_backend_test(NAME Dx8_SpriteFont COMMAND ${_dx8_spritefont_cmd}
        TIMEOUT 60 LABELS "DX8")

    # CNA::GraphicsCapability: DX8 is fully 3D-capable from day one (a port of DX3's own, itself a port of DX2's
    # post-Phase-O9 state -- no "3D lands later" scope gap the way DX2's own Phase O1/O2 history
    # had) -- SupportsCapability() reports which capabilities are genuinely unavailable at this
    # DirectX era vs. real.
    cna_dx8_test(cna_test_dx8_graphics_capability examples/dx8_graphics_capability_test.cpp)
    cna_dx8_ctest_command(_dx8_graphics_capability_cmd cna_test_dx8_graphics_capability)
    cna_register_backend_test(NAME Dx8_GraphicsCapability COMMAND ${_dx8_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O2 (DX2-14, 2D layer ported from DX1-68): logical/window coordinate transform CTest.
    cna_dx8_test(cna_test_dx8_logical_transform examples/dx8_logical_transform_test.cpp)
    cna_dx8_ctest_command(_dx8_logical_transform_cmd cna_test_dx8_logical_transform)
    cna_register_backend_test(NAME Dx8_LogicalTransform COMMAND ${_dx8_logical_transform_cmd}
        TIMEOUT 60 LABELS "DX8")

    # NOTE: Dx1_No3D has no DX8 equivalent here either, same reasoning as DX2/DX3 -- DX8's
    # 3D layer is real (not a permanent throw like DX1's), so its own 3D CTest suite grows across
    # Phase O3/O4/O5 instead of a single "throws" test that would need replacing almost immediately.

    # Phase O3 (DX2-20..DX2-26): real Direct3D v2 device bring-up CTest -- device/viewport/Z-buffer
    # construction and the newly-real ClearColorAndDepth/ClearDepth/ClearColorDepthAndStencil entry
    # points. The 3D DRAW path (VertexBuffer/DrawColoredPrimitives) is still Phase O4/O5 -- this
    # test's own Check D confirms CreateVertexBuffer still throws, rather than over-claiming.
    cna_dx8_test(cna_test_dx8_device3d_smoke examples/dx8_device3d_smoke_test.cpp)
    cna_dx8_ctest_command(_dx8_device3d_smoke_cmd cna_test_dx8_device3d_smoke)
    cna_register_backend_test(NAME Dx8_Device3DSmoke COMMAND ${_dx8_device3d_smoke_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O5 (DX2-40..DX2-42): Dx2VertexBufferBackend/Dx2IndexBufferBackend CTest.
    cna_dx8_test(cna_test_dx8_vertex_index_buffer examples/dx8_vertex_index_buffer_test.cpp)
    cna_dx8_ctest_command(_dx8_vertex_index_buffer_cmd cna_test_dx8_vertex_index_buffer)
    cna_register_backend_test(NAME Dx8_VertexIndexBuffer COMMAND ${_dx8_vertex_index_buffer_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O4 (DX2-30..DX2-35): real CPU transform/clip -> D3DTLVERTEX -> DrawIndexedPrimitive
    # pipeline CTest -- pixel-verified triangle rendering through the real Direct3D v2 device.
    cna_dx8_test(cna_test_dx8_colored_primitives examples/dx8_colored_primitives_test.cpp)
    cna_dx8_ctest_command(_dx8_colored_primitives_cmd cna_test_dx8_colored_primitives)
    cna_register_backend_test(NAME Dx8_ColoredPrimitives COMMAND ${_dx8_colored_primitives_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O4 (DX2-33, DX2-36): DrawIndexedPrimitives (16-bit and 32-bit indices) CTest.
    cna_dx8_test(cna_test_dx8_indexed_primitives examples/dx8_indexed_primitives_test.cpp)
    cna_dx8_ctest_command(_dx8_indexed_primitives_cmd cna_test_dx8_indexed_primitives)
    cna_register_backend_test(NAME Dx8_IndexedPrimitives COMMAND ${_dx8_indexed_primitives_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O4 (DX2-37): real depth-test occlusion CTest, order-independent.
    cna_dx8_test(cna_test_dx8_ztest examples/dx8_ztest_test.cpp)
    cna_dx8_ctest_command(_dx8_ztest_cmd cna_test_dx8_ztest)
    cna_register_backend_test(NAME Dx8_ZTest COMMAND ${_dx8_ztest_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O4 (DX2-34, DX2-38): real texture0 sampling via SetTexture+SetTextureStageState CTest.
    cna_dx8_test(cna_test_dx8_texture3d examples/dx8_texture3d_test.cpp)
    cna_dx8_ctest_command(_dx8_texture3d_cmd cna_test_dx8_texture3d)
    cna_register_backend_test(NAME Dx8_Texture3D COMMAND ${_dx8_texture3d_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O4 (DX2-30, DX2-39): near-plane clipping CTest.
    cna_dx8_test(cna_test_dx8_clipping examples/dx8_clipping_test.cpp)
    cna_dx8_ctest_command(_dx8_clipping_cmd cna_test_dx8_clipping)
    cna_register_backend_test(NAME Dx8_Clipping COMMAND ${_dx8_clipping_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O7 (DX2-60..DX2-66): remaining IGraphicsBackend entry points genuinely unavailable at
    # this DirectX era -- occlusion query, volume/cube textures, custom effects, instancing.
    cna_dx8_test(cna_test_dx8_remaining_defaults examples/dx8_remaining_defaults_test.cpp)
    cna_dx8_ctest_command(_dx8_remaining_defaults_cmd cna_test_dx8_remaining_defaults)
    cna_register_backend_test(NAME Dx8_RemainingDefaults COMMAND ${_dx8_remaining_defaults_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect lighting (ambient + directional
    # Lambertian/Blinn-Phong specular) for the normal-bearing vertex layouts, design decision 13.
    cna_dx8_test(cna_test_dx8_lighting examples/dx8_lighting_test.cpp)
    cna_dx8_ctest_command(_dx8_lighting_cmd cna_test_dx8_lighting)
    cna_register_backend_test(NAME Dx8_Lighting COMMAND ${_dx8_lighting_cmd}
        TIMEOUT 60 LABELS "DX8")

    # WireFrame/AnisotropicFiltering re-verification -- both report true on this backend (a real
    # DXVK-backed GPU, unlike DX2..DX7's own software rasterizer where anisotropic filtering was
    # empirically confirmed absent).
    cna_dx8_test(cna_test_dx8_wireframe_aniso examples/dx8_wireframe_aniso_test.cpp)
    cna_dx8_ctest_command(_dx8_wireframe_aniso_cmd cna_test_dx8_wireframe_aniso)
    cna_register_backend_test(NAME Dx8_WireframeAniso COMMAND ${_dx8_wireframe_aniso_cmd}
        TIMEOUT 60 LABELS "DX8")

    # Phase S6 (plan_dx8.md): real stencil buffer write-then-test through the full XNA public API
    # (GraphicsDevice.DepthStencilState), proving stencil (unchanged from DX6) survives the removed
    # viewport object -- mirrors the DX8-0 spike's own Test E/F shape (write REPLACE on half the
    # target, then test EQUAL against the full target and confirm the untouched half is correctly
    # rejected).
    cna_dx8_test(cna_test_dx8_stencil examples/dx8_stencil_test.cpp)
    cna_dx8_ctest_command(_dx8_stencil_cmd cna_test_dx8_stencil)
    cna_register_backend_test(NAME Dx8_Stencil COMMAND ${_dx8_stencil_cmd}
        TIMEOUT 60 LABELS "DX8")
endif()
