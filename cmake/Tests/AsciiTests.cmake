if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32
   AND CNA_GRAPHICS_BACKEND STREQUAL "ASCII")

    enable_testing()

    macro(cna_ascii_test target src)
        add_executable(${target} ${src})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(${target} PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(${target} PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(${target} PRIVATE SDL3::SDL3main)
        endif()
    endmacro()

    # Phase G2 (ASCII-10/11): hand-authored glyph-density font atlas smoke test.
    cna_ascii_test(cna_test_ascii_fontatlas examples/ascii_fontatlas_test.cpp)
    cna_register_backend_test(NAME Ascii_FontAtlas COMMAND cna_test_ascii_fontatlas
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G3 (ASCII-20/21): offscreen gameTarget_ + SetRenderTarget(nullptr) redirect.
    cna_ascii_test(cna_test_ascii_offscreentarget examples/ascii_offscreentarget_test.cpp)
    cna_register_backend_test(NAME Ascii_OffscreenTarget COMMAND cna_test_ascii_offscreentarget
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G4 (ASCII-30..33): quantizer -- pure function, no GraphicsDevice/window needed.
    cna_ascii_test(cna_test_ascii_quantizer examples/ascii_quantizer_test.cpp)
    cna_register_backend_test(NAME Ascii_Quantizer COMMAND cna_test_ascii_quantizer
        TIMEOUT 30)

    # Phase G5 (ASCII-40/41): Present() draws the quantized grid into the real window.
    cna_ascii_test(cna_test_ascii_present examples/ascii_present_test.cpp)
    cna_register_backend_test(NAME Ascii_Present COMMAND cna_test_ascii_present
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G6 (ASCII-50/51): Mouse/Keyboard/GamePad need zero new code against a real window.
    cna_ascii_test(cna_test_ascii_input examples/ascii_input_test.cpp)
    cna_register_backend_test(NAME Ascii_Input COMMAND cna_test_ascii_input
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # Phase G7 (ASCII-60): confirm ThrowNo3D reuse via the wrapped SdlGraphicsBackend.
    cna_ascii_test(cna_test_ascii_throwno3d examples/ascii_throwno3d_test.cpp)
    cna_register_backend_test(NAME Ascii_ThrowNo3D COMMAND cna_test_ascii_throwno3d
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")

    # REMED-GFX-127: every public Texture2D::GetData call must return the resource's real content or
    # reject the request deterministically -- never fabricate one. This backend's render targets are
    # the wrapped SdlGraphicsBackend's own SDL_TEXTUREACCESS_TARGET textures, so it inherits that
    # backend's readback capability; the check belongs here too because the wrapper is what a game
    # actually runs against.
    cna_ascii_test(cna_test_ascii_texture2d_getdata_contract
                   examples/texture2d_getdata_contract_test.cpp)
    cna_register_backend_test(NAME Ascii_Texture2D_GetDataContract COMMAND cna_test_ascii_texture2d_getdata_contract
        TIMEOUT 30 ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
endif()
