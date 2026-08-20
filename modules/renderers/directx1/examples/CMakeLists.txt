# directx1 renderer example/test registration (module-local; this file is the
# former cmake/Tests/DirectX1Tests.cmake, moved when examples moved into their owning
# physical modules).
#
# From the root CMakeLists.txt include site:
# plans/plan_dx1.md: DIRECTX1 (real DirectDraw v1, no ../free-direct) CTest registration, mirroring D3D9's
# own cna_directx9_test/cna_directx9_ctest_command shape (Windows-only, MinGW cross-compile + Wine
# wrapper) rather than DIRECTX3's native-Linux shape, since DIRECTX1 genuinely needs real ddraw.h.

# Example/test executables historically live at the build root (scripts, docs and
# the golden-image working-directory conventions rely on it); keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Shared example-support headers ("common/PixelTestGame.hpp", ...) are owned by
# the graphics module's examples directory.
include_directories(${CNA_GRAPHICS_EXAMPLES_DIR})

if(CNA_BUILD_TESTS AND CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX1")
    enable_testing()

    macro(cna_directx1_test target src)
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

    # plans/plan_dx1.md design decision 11: when cross-compiled from Linux (DIRECTX1 is Windows-only, design
    # decision 1), CTest itself still runs on the Linux host, so the test COMMAND wraps the real
    # .exe with scripts/run-wine-directx1.sh -- a real, native Windows/MSVC configure needs no Wine
    # wrapper at all and runs the .exe directly, mirroring DirectX9Tests.cmake's own
    # cna_directx9_ctest_command exactly.
    macro(cna_directx1_ctest_command out_var target_name)
        if(CMAKE_CROSSCOMPILING)
            set(${out_var} ${CMAKE_SOURCE_DIR}/scripts/run-wine-directx1.sh $<TARGET_FILE:${target_name}>)
        else()
            set(${out_var} $<TARGET_FILE:${target_name}>)
        endif()
    endmacro()

    # DX1-1 (plans/plan_dx1.md section 1): a real, automated proof this renderer never silently drifts
    # into DIRECTX3+ (or Direct3D) territory -- greps its own source for any IDirectDraw2+/
    # IDirectDrawSurface2+/DDSURFACEDESC2 symbol or any d3d.h/IDirect3D token (comments that merely
    # document the discipline are excluded). Pure text check via scripts/check-directx1-v1-only.sh, no
    # compiled binary, no Wine needed -- runs identically whether cross-compiling or not.
    add_test(NAME DirectX1_V1OnlyDiscipline COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/check-directx1-v1-only.sh" "${CMAKE_SOURCE_DIR}")
    # Historical root-scope registration CWD, preserved (see cna_register_renderer_test).
    set_tests_properties(DirectX1_V1OnlyDiscipline PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(DirectX1_V1OnlyDiscipline PROPERTIES LABELS "DIRECTX1")

    # Phase O2 (DX1-1..DX1-18): device/window bring-up + Clear/Present/readback foundation.
    cna_directx1_test(cna_test_directx1_smoke directx1_smoke_test.cpp)
    cna_directx1_ctest_command(_directx1_smoke_cmd cna_test_directx1_smoke)
    cna_register_renderer_test(NAME DirectX1_Smoke COMMAND ${_directx1_smoke_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O3 (DX1-20..DX1-28): texture/render-target renderer CTest.
    cna_directx1_test(cna_test_directx1_texture_rendertarget directx1_texture_rendertarget_test.cpp)
    cna_directx1_ctest_command(_directx1_texture_rendertarget_cmd cna_test_directx1_texture_rendertarget)
    cna_register_renderer_test(NAME DirectX1_TextureRenderTarget COMMAND ${_directx1_texture_rendertarget_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O4 (DX1-30..DX1-39): CPU compositor / SpriteBatch draw path CTest.
    cna_directx1_test(cna_test_directx1_spritebatch directx1_spritebatch_test.cpp)
    cna_directx1_ctest_command(_directx1_spritebatch_cmd cna_test_directx1_spritebatch)
    cna_register_renderer_test(NAME DirectX1_SpriteBatch COMMAND ${_directx1_spritebatch_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O5 (DX1-40..DX1-44): blend-mode compositing math CTest.
    cna_directx1_test(cna_test_directx1_blend directx1_blend_test.cpp)
    cna_directx1_ctest_command(_directx1_blend_cmd cna_test_directx1_blend)
    cna_register_renderer_test(NAME DirectX1_Blend COMMAND ${_directx1_blend_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O5 (DX1-45/DX1-46): TextureFilter + TextureAddressMode sampling CTest.
    cna_directx1_test(cna_test_directx1_sampling directx1_sampling_test.cpp)
    cna_directx1_ctest_command(_directx1_sampling_cmd cna_test_directx1_sampling)
    cna_register_renderer_test(NAME DirectX1_AddressMode COMMAND ${_directx1_sampling_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O6 (DX1-50..DX1-54): SpriteFont / DrawString CTest.
    cna_directx1_test(cna_test_directx1_spritefont directx1_spritefont_test.cpp)
    cna_directx1_ctest_command(_directx1_spritefont_cmd cna_test_directx1_spritefont)
    cna_register_renderer_test(NAME DirectX1_SpriteFont COMMAND ${_directx1_spritefont_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O7 (DX1-60..DX1-67, DX1-69): ThrowNo3D wiring / remaining-defaults CTest.
    cna_directx1_test(cna_test_directx1_no3d directx1_no3d_test.cpp)
    cna_directx1_ctest_command(_directx1_no3d_cmd cna_test_directx1_no3d)
    cna_register_renderer_test(NAME DirectX1_No3D COMMAND ${_directx1_no3d_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # CNA::GraphicsCapability: DIRECTX1 is 2D-only -- SupportsCapability() reports which capabilities
    # are genuinely absent (ThreeD and everything that depends on it).
    cna_directx1_test(cna_test_directx1_graphics_capability directx1_graphics_capability_test.cpp)
    cna_directx1_ctest_command(_directx1_graphics_capability_cmd cna_test_directx1_graphics_capability)
    cna_register_renderer_test(NAME DirectX1_GraphicsCapability COMMAND ${_directx1_graphics_capability_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")

    # Phase O7 (DX1-68): logical/window coordinate transform CTest.
    cna_directx1_test(cna_test_directx1_logical_transform directx1_logical_transform_test.cpp)
    cna_directx1_ctest_command(_directx1_logical_transform_cmd cna_test_directx1_logical_transform)
    cna_register_renderer_test(NAME DirectX1_LogicalTransform COMMAND ${_directx1_logical_transform_cmd}
        TIMEOUT 60 LABELS "DIRECTX1")
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
