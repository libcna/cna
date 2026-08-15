include_guard(GLOBAL)

# Blend2D (https://github.com/blend2d/blend2d) integration for the BLEND2D graphics renderer.
#
# Blend2D is a 2D vector graphics engine released under the Zlib license, powered at runtime by
# an AsmJit-generated (also Zlib) JIT pipeline compiler. CNA uses it as a genuine 2D-only CPU
# rendering route: a Blend2D BLImage/BLContext backbuffer handed to the platform's narrow surface
# presenter -- the same CPU-raster/presenter shape established by the SKIA renderer
# (cmake/ThirdPartySkia.cmake), but built from source via
# FetchContent (like BGFX/LLGL/Wicked) rather than consumed as a build-outside-the-tree artifact,
# since Blend2D (unlike Skia) is a small, portable, natively CMake-buildable C++ library.
#
# Acquisition follows the pinned-tag FetchContent shape (cmake/ThirdPartyLLGL.cmake), with an
# explicit local-source escape hatch for reproducible/offline builds (CNA_BLEND2D_ROOT /
# CNA_ASMJIT_ROOT), mirroring CNA_WEBGPU_ROOT/CNA_WICKED_ROOT.

set(CNA_BLEND2D_GIT_TAG "def0d1238c3e5d0983bb848e5676049d829e435b" CACHE STRING
    "Blend2D git commit to fetch (pinned; v0.21.2, 2025-11-02; do not track a moving branch)")
set(CNA_BLEND2D_GIT_REPOSITORY "https://github.com/blend2d/blend2d.git" CACHE STRING
    "Blend2D git repository URL")
set(CNA_BLEND2D_ROOT "" CACHE PATH
    "Path to an existing Blend2D source checkout; when set, no download is performed")

# Blend2D's JIT pipeline compiler depends on AsmJit; upstream does not pin an exact AsmJit
# revision (BLEND2D_EXTERNAL_ASMJIT=OFF just expects a recent checkout next to/inside the source
# tree via ASMJIT_DIR), so CNA pins one explicitly for a reproducible build -- the same date as
# the Blend2D pin above (both projects are developed together upstream; "Updated to work with
# latest AsmJit" is a recurring Blend2D commit message).
set(CNA_ASMJIT_GIT_TAG "b56f4176cb9b0c0501da659ac54d4c5877862c7b" CACHE STRING
    "AsmJit git commit to fetch (pinned; 2025-11-02, same day as the Blend2D pin)")
set(CNA_ASMJIT_GIT_REPOSITORY "https://github.com/asmjit/asmjit.git" CACHE STRING
    "AsmJit git repository URL")
set(CNA_ASMJIT_ROOT "" CACHE PATH
    "Path to an existing AsmJit source checkout; when set, no download is performed")

# Configures Blend2D (and its AsmJit dependency) and publishes the blend2d::blend2d target every
# consumer links (blend2d/CMakeLists.txt unconditionally declares
# `add_library(blend2d::blend2d ALIAS blend2d)`, so no extra alias wiring is needed here).
function(cna_configure_blend2d)
    if(TARGET blend2d::blend2d)
        return()
    endif()

    include(FetchContent)

    if(CNA_ASMJIT_ROOT)
        if(NOT EXISTS "${CNA_ASMJIT_ROOT}/CMakeLists.txt")
            message(FATAL_ERROR
                "CNA Blend2D: CNA_ASMJIT_ROOT='${CNA_ASMJIT_ROOT}' does not contain a "
                "CMakeLists.txt -- it must point at an AsmJit source checkout "
                "(git clone ${CNA_ASMJIT_GIT_REPOSITORY}).")
        endif()
        message(STATUS "CNA Blend2D: using local AsmJit source at ${CNA_ASMJIT_ROOT}")
        set(_cna_asmjit_dir "${CNA_ASMJIT_ROOT}")
    else()
        message(STATUS "CNA Blend2D: fetching AsmJit ${CNA_ASMJIT_GIT_TAG} from ${CNA_ASMJIT_GIT_REPOSITORY}")
        FetchContent_Declare(
            cna_asmjit
            GIT_REPOSITORY "${CNA_ASMJIT_GIT_REPOSITORY}"
            GIT_TAG "${CNA_ASMJIT_GIT_TAG}"
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE)
        # Populate only -- AsmJit is embedded by Blend2D's own add_subdirectory(${ASMJIT_DIR})
        # below (ASMJIT_EMBED semantics), not added directly by CNA.
        FetchContent_Populate(cna_asmjit)
        set(_cna_asmjit_dir "${cna_asmjit_SOURCE_DIR}")
    endif()

    # Blend2D's own CMakeLists.txt (BLEND2D_EXTERNAL_ASMJIT=OFF path) consumes AsmJit through this
    # exact cache variable, add_subdirectory()-ing it itself -- CNA never adds AsmJit as its own
    # subdirectory/target.
    set(ASMJIT_DIR "${_cna_asmjit_dir}" CACHE PATH "" FORCE)
    set(ASMJIT_STATIC TRUE CACHE BOOL "" FORCE)

    set(BLEND2D_STATIC TRUE CACHE BOOL "" FORCE)
    set(BLEND2D_TEST OFF CACHE BOOL "" FORCE)
    set(BLEND2D_NO_INSTALL ON CACHE BOOL "" FORCE)

    if(CNA_BLEND2D_ROOT)
        if(NOT EXISTS "${CNA_BLEND2D_ROOT}/CMakeLists.txt")
            message(FATAL_ERROR
                "CNA Blend2D: CNA_BLEND2D_ROOT='${CNA_BLEND2D_ROOT}' does not contain a "
                "CMakeLists.txt -- it must point at a Blend2D source checkout "
                "(git clone ${CNA_BLEND2D_GIT_REPOSITORY}).")
        endif()
        message(STATUS "CNA Blend2D: using local Blend2D source at ${CNA_BLEND2D_ROOT}")
        add_subdirectory("${CNA_BLEND2D_ROOT}" "${CMAKE_BINARY_DIR}/_deps/blend2d-build")
    else()
        message(STATUS "CNA Blend2D: fetching Blend2D ${CNA_BLEND2D_GIT_TAG} from ${CNA_BLEND2D_GIT_REPOSITORY}")
        FetchContent_Declare(
            cna_blend2d
            GIT_REPOSITORY "${CNA_BLEND2D_GIT_REPOSITORY}"
            GIT_TAG "${CNA_BLEND2D_GIT_TAG}"
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE)
        FetchContent_MakeAvailable(cna_blend2d)
    endif()

    if(NOT TARGET blend2d::blend2d)
        message(FATAL_ERROR
            "CNA Blend2D: the blend2d::blend2d target was not created by the resolved Blend2D "
            "source tree -- the checkout does not match this integration's expectations.")
    endif()

    message(STATUS "CNA Blend2D: configured (AsmJit JIT pipelines enabled)")
endfunction()
