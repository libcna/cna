# DirectX 11 renderer example/test registration. Renderer-neutral parity
# fixtures are owned by cmake/DirectXParityTests.cmake (DX-242).

# Example/test executables historically live at the build root; scripts, docs,
# and golden-image working-directory conventions rely on this location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
    enable_testing()

    macro(cna_directx11_test target src)
        add_executable(${target} ${src})
        target_link_libraries(${target} PRIVATE CNA SDL3::SDL3)
        cna_link_sharp_runtime(${target} PRIVATE)
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

    macro(cna_directx11_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # GLTF-386: accept both version spellings emitted by genuine DXVK builds.
    cna_register_renderer_test(NAME DirectX11_DxvkGate
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test-run-wine-dxvk-gate.sh
        TIMEOUT 10 LABELS "DIRECTX11")

    cna_directx11_test(cna_test_directx11_smoke directx11_smoke_test.cpp)
    cna_directx11_ctest_command(_directx11_smoke_cmd cna_test_directx11_smoke)
    cna_register_renderer_test(NAME DirectX11_Smoke COMMAND ${_directx11_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX11")

    include(${CMAKE_SOURCE_DIR}/cmake/DirectXParityTests.cmake)
    cna_register_d3d_parity_tests()
endif()

cna_apply_skip_convention()
