# =====================================================================================
# SDL2 dependency for CNA_PLATFORM=SDL2
#
# SDL2 is intentionally separate from the existing SDL3 audio/runtime dependency.  A
# platform build can therefore exercise the real SDL2 API even while another CNA module
# still uses SDL3 internally.  No SDL2 target is exported from cna_platform.
# =====================================================================================

include_guard(GLOBAL)
include(FetchContent)

set(CNA_SDL2_ROOT "" CACHE PATH
    "Optional path to an SDL2 source tree (uses pinned FetchContent when empty)")
set(CNA_SDL2_GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git" CACHE STRING
    "SDL2 upstream repository used when CNA_SDL2_ROOT is empty")
set(CNA_SDL2_GIT_TAG "fa24d868ac2f8fd558e4e914c9863411245db8fd" CACHE STRING
    "Pinned SDL 2.30.11 revision used by CNA_PLATFORM=SDL2")

function(cna_configure_sdl2)
    if(TARGET SDL2::SDL2)
        return()
    endif()

    if(CNA_SDL2_ROOT)
        if(NOT EXISTS "${CNA_SDL2_ROOT}/CMakeLists.txt")
            message(FATAL_ERROR
                "CNA: CNA_SDL2_ROOT does not contain SDL2's CMakeLists.txt: ${CNA_SDL2_ROOT}")
        endif()
        set(_cna_sdl2_source "${CNA_SDL2_ROOT}")
    else()
        FetchContent_Declare(cna_sdl2
            GIT_REPOSITORY "${CNA_SDL2_GIT_REPOSITORY}"
            GIT_TAG "${CNA_SDL2_GIT_TAG}"
            GIT_SHALLOW TRUE)
        FetchContent_GetProperties(cna_sdl2)
        if(NOT cna_sdl2_POPULATED)
            FetchContent_Populate(cna_sdl2)
        endif()
        set(_cna_sdl2_source "${cna_sdl2_SOURCE_DIR}")
    endif()

    # SDL2's tests/examples do not exercise CNA and only make every platform configure slower.
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    add_subdirectory("${_cna_sdl2_source}" "${CMAKE_BINARY_DIR}/_deps/cna-sdl2-build" EXCLUDE_FROM_ALL)

    if(NOT TARGET SDL2::SDL2)
        message(FATAL_ERROR "CNA: SDL2 configuration did not provide SDL2::SDL2")
    endif()
endfunction()
