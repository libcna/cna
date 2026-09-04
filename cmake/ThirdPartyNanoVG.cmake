# --- NanoVG (https://github.com/memononen/nanovg) ---
#
# NanoVG ships no CMake of its own. Its core (`nanovg.c`, plus the vendored `fontstash.h`/
# `stb_image.h` it `#include`s with their own `..._IMPLEMENTATION` macros) is backend-agnostic --
# it never touches OpenGL directly -- so it is compiled once here into a small static library,
# mirroring cmake/ThirdPartyOpenVG.cmake's own "fetch at a pinned commit, compile the handful of
# core .c files directly" shape for a from-source upstream with no build system of its own.
#
# The GL2 RENDER BACKEND (`nanovg_gl.h`, selected by `#define NANOVG_GL2_IMPLEMENTATION`) is
# deliberately NOT compiled here: it calls `gl*` entry points directly by name rather than loading
# them itself (unlike a real loader such as GLAD), so CNA's own
# modules/renderers/nanovg/src/NanoVgGl.cpp `#include`s it after declaring the function-pointer
# shims those calls resolve to -- see that file's own header comment and nanovg-spike/README.md
# for the existence-gate proof this mechanism actually works.
#
# Pinned at the HEAD commit as of 2026-08-19 (NanoVG publishes no tags/releases beyond ancient
# ones; this is the latest commit on its default branch), verified via
# `git ls-remote https://github.com/memononen/nanovg.git HEAD`. zlib licensed (see the
# repository's own LICENSE.txt).
#
# Offline / air-gapped builds: pass -DFETCHCONTENT_SOURCE_DIR_NANOVG=/path/to/nanovg to point at an
# existing checkout. That is CMake's own built-in per-dependency override, so no CNA-specific
# variable is needed.

set(CNA_NANOVG_GIT_REPOSITORY "https://github.com/memononen/nanovg.git"
    CACHE STRING "Upstream NanoVG repository fetched for the NANOVG graphics renderer")
set(CNA_NANOVG_GIT_TAG "ce3bf745eb2d2dbc14a50bf2446783f691ac4353"
    CACHE STRING "Pinned NanoVG commit used by the NANOVG graphics renderer")

# nanovg.c is plain, backend-agnostic C -- cna_configure_nanovg() (below) runs from
# RendererSelection.cmake, called before add_subdirectory(modules), which is what first pulls in
# a C target (third_party/enet) today. The top-level `project(CNA LANGUAGES CXX)` never enables C,
# so without this the generator (Ninja) reaches its Generate step with no C compile/archive rules
# configured at all ("Error required internal CMake variable not set... CMAKE_C_COMPILE_OBJECT") --
# found and verified empirically, same gap cmake/ThirdPartyOpenVG.cmake documents for ShivaVG.
# Deliberately called here at this file's own top level (include()'d from directory scope), NOT
# inside cna_configure_nanovg() itself: enable_language() called from inside a function() does not
# propagate the generator's internal per-language rule variables to the rest of the configure run.
# enable_language() is idempotent and safe to call again later (e.g. by ENet's own project(enet)).
enable_language(C)

function(cna_configure_nanovg)
    if(TARGET cna_thirdparty_nanovg)
        return()
    endif()

    include(FetchContent)

    FetchContent_Declare(
        nanovg
        GIT_REPOSITORY "${CNA_NANOVG_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_NANOVG_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(nanovg)

    if(NOT EXISTS "${nanovg_SOURCE_DIR}/src/nanovg.c")
        message(FATAL_ERROR
            "CNA: fetched NanoVG at ${nanovg_SOURCE_DIR} but src/nanovg.c is missing -- the pin "
            "CNA_NANOVG_GIT_TAG=${CNA_NANOVG_GIT_TAG} may not be a NanoVG checkout.")
    endif()

    # Compiled as C (font/path tessellation only -- no GL calls) to sidestep any C-vs-C++ friction
    # in its own vendored fontstash.h/stb_image.h, exactly like ShivaVG's own core .c files.
    add_library(cna_thirdparty_nanovg STATIC "${nanovg_SOURCE_DIR}/src/nanovg.c")
    target_include_directories(cna_thirdparty_nanovg PUBLIC "${nanovg_SOURCE_DIR}/src")
    set_target_properties(cna_thirdparty_nanovg PROPERTIES
        C_STANDARD 99
        POSITION_INDEPENDENT_CODE ON
    )

    find_package(OpenGL REQUIRED)
    target_link_libraries(cna_thirdparty_nanovg PUBLIC OpenGL::GL)

    message(STATUS "CNA: NanoVG pinned at ${CNA_NANOVG_GIT_TAG}")
endfunction()
