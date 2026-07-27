if(CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    add_executable(cna_test_canvas_smoke examples/canvas_smoke_test.cpp)
    target_link_libraries(cna_test_canvas_smoke PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # CNA::GraphicsCapability: Canvas is 2D-only -- SupportsCapability() reports which
    # capabilities are genuinely absent (ThreeD and everything that depends on it). Twin of
    # cna_test_sdl_graphics_capability/cna_test_dx3_graphics_capability.
    add_executable(cna_test_canvas_graphics_capability examples/canvas_graphics_capability_test.cpp)
    target_link_libraries(cna_test_canvas_graphics_capability PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. Built (not ctest-registered, like
    # every Canvas test: this backend produces a wasm module that needs a browser), so the readback
    # contract at least stays compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_texture2d_getdata_contract examples/texture2d_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_texture2d_getdata_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)

    # REMED-GFX-130: the TextureCube/Texture3D half of the same finding. Built (not ctest-registered,
    # like every Canvas test) so this backend's deterministic cube/volume rejection stays
    # compile-verified on the Emscripten toolchain.
    add_executable(cna_test_canvas_cube_volume_getdata_contract
                   examples/texturecube_texture3d_getdata_contract_test.cpp)
    target_link_libraries(cna_test_canvas_cube_volume_getdata_contract PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
endif()
