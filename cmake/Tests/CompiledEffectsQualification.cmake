# SPDX-License-Identifier: MS-PL
# GAP-13: script-only declarations test; named policy/corpus entries select the active renderer.
if(NOT EMSCRIPTEN)
    foreach(_gap13_identity IN LISTS CNA_RENDERER_IDENTITIES)
        set(_gap13_option "")
        if(_gap13_identity MATCHES "^(OPENGL33|OPENGLES3|WEBGL2)$")
            set(_gap13_option CNA_EASYGL_COMPILED_EFFECTS)
        elseif(_gap13_identity MATCHES "^(SDLGPU|WEBGPU|SOFTWARE|DIRECTX9)$")
            set(_gap13_option "CNA_${_gap13_identity}_COMPILED_EFFECTS")
        endif()
        set(_gap13_expected 0)
        if(_gap13_identity STREQUAL "FNA3D")
            set(_gap13_expected 1)
        elseif(_gap13_option)
            if(${_gap13_option})
                set(_gap13_expected 1)
            endif()
        endif()
        set(_gap13_filter "Gap13CompiledEffectPolicyTest.*")
        if(_gap13_identity MATCHES "^(SDLGPU|WEBGPU)$")
            string(APPEND _gap13_filter ":Gap13CompiledEffectSpirvTest.*")
        endif()
        if(_gap13_identity STREQUAL "DIRECTX9")
            string(APPEND _gap13_filter ":Gap13CompiledEffectNativeRefusalTest.*")
        endif()
        # Keep launcher/executable separate: the test helper expands semicolons in COMMAND.
        set(_gap13_launcher "")
        if(MINGW)
            set(_gap13_launcher "${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk9.sh")
        endif()
        set(_gap13_policy_count 2)
        if(_gap13_identity MATCHES "^(SDLGPU|WEBGPU|DIRECTX9)$")
            set(_gap13_policy_count 3)
        endif()
        cna_register_renderer_test(NAME Gap13_CompiledPolicy_${_gap13_identity}
            COMMAND ${CMAKE_COMMAND} "-DCNA_EXECUTABLE=$<TARGET_FILE:CnaTests>"
                "-DCNA_LAUNCHER=${_gap13_launcher}"
                "-DCNA_FILTER=${_gap13_filter}" "-DCNA_EXPECTED_CASES=${_gap13_policy_count}"
                "-DCNA_REPORT=${CMAKE_BINARY_DIR}/gap13-policy-${_gap13_identity}.xml"
                "-DCNA_WINE=${MINGW}"
                -P "${CMAKE_SOURCE_DIR}/cmake/Tests/CompiledEffectsRunCase.cmake"
            LABELS "compiled-effects;qualification" TIMEOUT 180
            ENVIRONMENT "CNA_GRAPHICS_RENDERER=${_gap13_identity};CNA_GAP13_EXPECT_COMPILED_EFFECTS=${_gap13_expected};SDL_AUDIODRIVER=dummy")
        if(_gap13_expected AND NOT _gap13_identity STREQUAL "FNA3D")
            cna_register_renderer_test(NAME Gap13_CompiledCorpus_${_gap13_identity}
                COMMAND ${CMAKE_COMMAND} "-DCNA_EXECUTABLE=$<TARGET_FILE:CnaTests>"
                "-DCNA_LAUNCHER=${_gap13_launcher}"
                    "-DCNA_FILTER=Gap13CompiledEffectCorpusTest.*" "-DCNA_EXPECTED_CASES=44"
                    "-DCNA_REPORT=${CMAKE_BINARY_DIR}/gap13-corpus-${_gap13_identity}.xml"
                    "-DCNA_WINE=${MINGW}"
                    -P "${CMAKE_SOURCE_DIR}/cmake/Tests/CompiledEffectsRunCase.cmake"
                LABELS "compiled-effects;qualification" TIMEOUT 600
                ENVIRONMENT "CNA_GRAPHICS_RENDERER=${_gap13_identity};CNA_GAP13_REQUIRE_CORPUS=1;SDL_AUDIODRIVER=dummy")
        endif()
    endforeach()
endif()
