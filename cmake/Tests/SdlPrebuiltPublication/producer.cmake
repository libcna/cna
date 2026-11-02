# SPDX-License-Identifier: MS-PL
# Exercise the unmodified production functions from a minimal parent project.
set(CMAKE_GENERATOR "${CNA_GENERATOR}")
set(CMAKE_GENERATOR_PLATFORM "")
set(CMAKE_GENERATOR_TOOLSET "")
set(CMAKE_GENERATOR_INSTANCE "")
set(CNA_SDL_PREBUILT_ROOT "${CNA_WORK_DIR}/prebuilt" CACHE PATH "" FORCE)
set(CNA_USE_SYSTEM_SDL OFF CACHE BOOL "" FORCE)
set(CNA_VENDORED_SOURCE_CHECK OFF CACHE STRING "" FORCE)
set(CNA_MAX_VENDORED_BUILD_JOBS 1 CACHE STRING "" FORCE)
set(EMSCRIPTEN TRUE) # Avoid host video-driver discovery during include.
include("${CNA_SOURCE_DIR}/cmake/ThirdPartySDL.cmake")
set(CNA_SDL3_PATCHES "")
# The fake payload uses the Unix layout on every host; no target binaries are executed.
foreach(_platform IN ITEMS EMSCRIPTEN ANDROID WIN32 APPLE MSVC CNA_APPLE_IOS)
    set(${_platform} FALSE)
endforeach()
if(CNA_PRODUCER STREQUAL "lock-check")
    function(_expect_locked)
        file(LOCK "${CNA_SDL_PREBUILT_ROOT}/.cna-prebuilt.lock" GUARD FUNCTION
            TIMEOUT 0 RESULT_VARIABLE _lock_result)
        if(_lock_result EQUAL 0)
            message(FATAL_ERROR "Producer did not retain the root lock during installation")
        endif()
    endfunction()
    _expect_locked()
elseif(CNA_PRODUCER STREQUAL "check-mixer-absent")
    _cna_sdl_set_layout("${CNA_SDL_PREBUILT_ROOT}")
    # With no stamp, CHECK must report absent regardless of the incoming arguments.
    _cna_ensure_sdl_dep(CHECK DIR "${CNA_SDL_PREBUILT_ROOT}" NAME SDL3_mixer
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_mixer" LIBRARY "${_sdl_mixer_lib}"
        STATE_VAR _state MANIFEST_VAR _manifest)
    if(NOT _state STREQUAL "absent")
        message(FATAL_ERROR "Interrupted mixer was classified ${_state}, expected absent")
    endif()
elseif(CNA_PRODUCER STREQUAL "check")
    _cna_sdl_set_layout("${CNA_SDL_PREBUILT_ROOT}")
    _cna_ensure_sdl_dep(CHECK DIR "${CNA_SDL_PREBUILT_ROOT}" NAME SDL3
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL" LIBRARY "${_sdl3_lib}"
        CMAKE_ARGS -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        STATE_VAR _state MANIFEST_VAR _manifest)
    file(WRITE "${CNA_WORK_DIR}/check.txt" "${_state}")
else()
    file(WRITE "${CNA_WORK_DIR}/${CNA_PRODUCER}.started" "started\n")
    cna_configure_vendored_sdl()
    file(WRITE "${CNA_WORK_DIR}/${CNA_PRODUCER}.selected" "${CNA_SDL_PREBUILT_DIR}")
    if(NOT SDL3_DIR STREQUAL "${CNA_SDL_PREBUILT_DIR}/install/lib/cmake/SDL3"
            OR NOT SDL3_mixer_DIR STREQUAL "${CNA_SDL_PREBUILT_DIR}/install/lib/cmake/SDL3_mixer")
        message(FATAL_ERROR "Package consumers did not follow the selected install")
    endif()
endif()
