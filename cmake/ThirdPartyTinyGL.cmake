# --- TinyGL (C-Chads/tinygl, CPU fixed-function OpenGL 1.x subset,
#     https://github.com/C-Chads/tinygl) ---
#
# Unlike PortableGL (a single STB-style header, see cmake/ThirdPartyPortableGL.cmake) TinyGL is a
# real multi-file C library with its own CMake build, so this fetches the upstream project and
# consumes its own `tinygl-static` target rather than only putting a directory on the include path.
#
# Pinned at commit 36a7987e7bebfda19615ea33341b1cc0ff9c3b13 (2023-11-04, the C-Chads fork's own
# HEAD when the TINYGL renderer landed), verified via `git ls-remote https://github.com/C-Chads/tinygl`.
# TinyGL is zlib-style licensed, but with one clause plain zlib does not have: an acknowledgment in
# the product AND its documentation is REQUIRED. THIRD_PARTY_NOTICES.md carries that acknowledgment;
# see the upstream LICENSE file for the complete text.
#
# Offline / air-gapped builds: pass -DFETCHCONTENT_SOURCE_DIR_TINYGL=/path/to/tinygl to point at an
# existing checkout. That is CMake's own built-in per-dependency override, so no CNA-specific
# variable is needed.

set(CNA_TINYGL_GIT_REPOSITORY "https://github.com/C-Chads/tinygl.git"
    CACHE STRING "Upstream TinyGL repository fetched for the TINYGL graphics renderer")
set(CNA_TINYGL_GIT_TAG "36a7987e7bebfda19615ea33341b1cc0ff9c3b13"
    CACHE STRING "Pinned TinyGL commit used by the TINYGL graphics renderer")

function(cna_configure_tinygl)
    include(FetchContent)

    # OpenMP is an optional acceleration. Every upstream pragma is guarded by _OPENMP, so a compiler
    # without OpenMP produces a complete single-threaded archive with no GOMP/omp references. Find
    # the C component in this parent directory before adding upstream because imported targets are
    # directory-scoped; when it exists we enable the accelerated regions explicitly below.
    enable_language(C)
    find_package(OpenMP QUIET COMPONENTS C)

    # Upstream builds a shared library and its demo corpus by default; CNA links the static archive
    # only. The demos pull in SDL and X11, neither of which this renderer needs -- it never opens a
    # window (see docs/tinygl-renderer.md).
    set(TINYGL_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(TINYGL_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(TINYGL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        tinygl
        GIT_REPOSITORY "${CNA_TINYGL_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_TINYGL_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(tinygl)

    if(NOT TARGET tinygl-static)
        message(FATAL_ERROR
            "CNA: fetched TinyGL at ${tinygl_SOURCE_DIR} but its tinygl-static target is missing -- "
            "the pin CNA_TINYGL_GIT_TAG=${CNA_TINYGL_GIT_TAG} may not be a TinyGL checkout.")
    endif()
    if(NOT EXISTS "${tinygl_SOURCE_DIR}/include/GL/gl.h")
        message(FATAL_ERROR
            "CNA: fetched TinyGL at ${tinygl_SOURCE_DIR} but include/GL/gl.h is missing.")
    endif()

    # Upstream checks the incorrectly-cased legacy-style OPENMP_C_FOUND name rather than CMake's
    # OpenMP_C_FOUND. Attach the target ourselves when available; its compile option defines
    # _OPENMP and its link interface resolves the resulting runtime references. With no target the
    # guarded pragmas compile out and no runtime dependency is created.
    #
    # MSVC is excluded deliberately. Upstream's clip.c and vertex.c use `#pragma omp simd`, an
    # OpenMP 4.0 construct; MSVC's default /openmp implements 2.0 and rejects it with C7660,
    # asking for /openmp:experimental. Taking an acceleration that is optional by design (see
    # TINYGL-21) in exchange for a dependency on an experimental compiler switch is a bad trade,
    # so Windows/MSVC builds take the same complete single-threaded path that TINYGL-21 already
    # tests: 14/14 suites pass with no OpenMP, and the archive carries no OpenMP references.
    if(TARGET OpenMP::OpenMP_C AND NOT MSVC)
        target_link_libraries(tinygl-static PUBLIC OpenMP::OpenMP_C)
        message(STATUS "CNA: TinyGL OpenMP acceleration enabled")
    elseif(MSVC)
        message(STATUS "CNA: TinyGL OpenMP skipped on MSVC (omp simd needs OpenMP 4.0); "
                       "using the complete single-threaded path")
    else()
        message(STATUS "CNA: TinyGL OpenMP unavailable; using the complete single-threaded path")
    endif()

    # Upstream compiles with -march=native when not cross-compiling, so the archive is tuned for
    # the build host. Stated rather than overridden: it is upstream's own choice, and CNA builds
    # TinyGL from source per machine.
    message(STATUS "CNA: TinyGL pinned at ${CNA_TINYGL_GIT_TAG}")
endfunction()
