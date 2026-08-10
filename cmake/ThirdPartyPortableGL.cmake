# --- PortableGL (single-header CPU software OpenGL 3.x-ish implementation,
#     https://github.com/rswinkle/PortableGL) ---
#
# Mirrors cmake/ThirdPartySokol.cmake's own pattern for a single-header upstream C library: the
# header is fetched at configure time from the upstream repository at a pinned tag/commit, in the
# same spirit as sokol's FetchContent block. There is nothing to build -- PortableGL is a single
# STB-style header -- so this only needs to put its directory on the include path; the one
# translation unit that instantiates `#define PORTABLEGL_IMPLEMENTATION` is CNA's own
# modules/renderers/portablegl/src/PortableGLImpl.cpp.
#
# Pinned at upstream tag 0.100.0 (commit 63a55db75ab07619797a93ff9bf3909355d27950), verified via
# `git ls-remote --tags https://github.com/rswinkle/PortableGL.git`. MIT licensed (see the
# header's own license block).
#
# Offline / air-gapped builds: pass -DFETCHCONTENT_SOURCE_DIR_PORTABLEGL=/path/to/PortableGL to
# point at an existing checkout. That is CMake's own built-in per-dependency override, so no
# CNA-specific variable is needed.

set(CNA_PORTABLEGL_GIT_REPOSITORY "https://github.com/rswinkle/PortableGL.git"
    CACHE STRING "Upstream PortableGL repository fetched for the PORTABLEGL graphics renderer")
set(CNA_PORTABLEGL_GIT_TAG "63a55db75ab07619797a93ff9bf3909355d27950"
    CACHE STRING "Pinned PortableGL commit (tag 0.100.0) used by the PORTABLEGL graphics renderer")

function(cna_configure_portablegl)
    include(FetchContent)

    FetchContent_Declare(
        portablegl
        GIT_REPOSITORY "${CNA_PORTABLEGL_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_PORTABLEGL_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(portablegl)

    if(NOT EXISTS "${portablegl_SOURCE_DIR}/portablegl.h")
        message(FATAL_ERROR
            "CNA: fetched PortableGL at ${portablegl_SOURCE_DIR} but portablegl.h is missing -- "
            "the pin CNA_PORTABLEGL_GIT_TAG=${CNA_PORTABLEGL_GIT_TAG} may not be a PortableGL "
            "checkout.")
    endif()

    add_library(cna_portablegl_headers INTERFACE)
    target_include_directories(cna_portablegl_headers INTERFACE "${portablegl_SOURCE_DIR}")

    message(STATUS "CNA: PortableGL pinned at ${CNA_PORTABLEGL_GIT_TAG} (tag 0.100.0)")
endfunction()
