# --- sokol (single-header GPU abstraction, https://github.com/floooh/sokol) ---
#
# plans/plan_sokol.md SOKOL-2 / design decision 3: sokol_gfx.h is fetched at configure time from the
# upstream repository at a pinned commit, in the same spirit as the BGFX renderer's own
# FetchContent block in RendererSelection.cmake. Unlike bgfx there is nothing to build -- sokol is
# a set of STB-style single-file headers, so this only needs to put a directory on the include
# path; the one translation unit that instantiates the implementation is CNA's own
# modules/renderers/sokol/src/SokolImpl.cpp.
#
# sokol publishes no version tags (its only tags mark historical API breaks), so the pin below is
# a plain upstream commit SHA. Bump it deliberately: sokol's C API changes without deprecation
# periods, and the checked-in shaders/sokol_shaders.hpp must be regenerated with a matching
# sokol-shdc whenever it moves.
#
# Offline / air-gapped builds: pass -DFETCHCONTENT_SOURCE_DIR_SOKOL=/path/to/sokol to point at an
# existing checkout. That is CMake's own built-in per-dependency override, so no CNA-specific
# variable is needed.

set(CNA_SOKOL_GIT_REPOSITORY "https://github.com/floooh/sokol.git"
    CACHE STRING "Upstream sokol repository fetched for the SOKOL graphics renderer")
set(CNA_SOKOL_GIT_TAG "27b49604b19be8cee0dcc6b2bbfe803dd9517585"
    CACHE STRING "Pinned sokol commit used by the SOKOL graphics renderer")

# Maps CNA_SOKOL_API onto the SOKOL_* compile definition sokol_gfx.h dispatches on, plus the
# system libraries that API needs. Only GLCORE is verified on this project's Linux dev machine
# (design decision 2) -- the other values exist so the renderer's own code stays API-agnostic and
# a platform owner can enable one without editing C++.
function(cna_configure_sokol)
    include(FetchContent)

    FetchContent_Declare(
        sokol
        GIT_REPOSITORY "${CNA_SOKOL_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_SOKOL_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(sokol)

    if(NOT EXISTS "${sokol_SOURCE_DIR}/sokol_gfx.h")
        message(FATAL_ERROR
            "CNA: fetched sokol at ${sokol_SOURCE_DIR} but sokol_gfx.h is missing -- the pin "
            "CNA_SOKOL_GIT_TAG=${CNA_SOKOL_GIT_TAG} may not be a sokol checkout.")
    endif()

    add_library(cna_sokol_headers INTERFACE)
    target_include_directories(cna_sokol_headers INTERFACE "${sokol_SOURCE_DIR}")
    target_compile_definitions(cna_sokol_headers INTERFACE "${CNA_SOKOL_API_DEFINE}")

    if(CNA_SOKOL_API STREQUAL "GLCORE" OR CNA_SOKOL_API STREQUAL "GLES3")
        # sokol_gfx.h's Linux/GL path includes <GL/gl.h> with GL_GLEXT_PROTOTYPES and calls the
        # core entry points directly, so the GL library must be linked (there is no built-in
        # loader outside Windows). find_package(OpenGL) also validates that the GL development
        # headers are actually installed, turning a missing mesa-common-dev into a clear
        # configure-time error instead of a confusing "GL/gl.h: No such file" later.
        find_package(OpenGL REQUIRED)
        target_link_libraries(cna_sokol_headers INTERFACE OpenGL::GL)
    endif()

    message(STATUS "CNA: sokol pinned at ${CNA_SOKOL_GIT_TAG} (${CNA_SOKOL_API_DEFINE})")
endfunction()
