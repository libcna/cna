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

    # TinyGL compiles OpenMP regions into the static archive. Find the C component in this parent
    # directory before adding upstream: imported targets are directory-scoped in CMake, so doing the
    # lookup only after FetchContent_MakeAvailable() can emit a false negative even though the child
    # directory already found and linked OpenMP. The renderer's documented toolchain contract makes
    # this a configure-time requirement rather than a warning followed by a possible link failure.
    enable_language(C)
    find_package(OpenMP REQUIRED COMPONENTS C)
    if(NOT TARGET OpenMP::OpenMP_C)
        message(FATAL_ERROR "CNA: FindOpenMP succeeded but OpenMP::OpenMP_C is unavailable.")
    endif()

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

    # TINYGL-0 (tinygl-spike/README.md, "Building and running"): glopCopyTexImage2D and
    # glopDrawPixels are compiled with OpenMP pragmas
    # (TGL_FEATURE_MULTITHREADED_COPY_TEXIMAGE_2D / TGL_FEATURE_MULTITHREADED_DRAWPIXELS in
    # zfeatures.h), so the static archive carries unresolved GOMP_* references even though this
    # renderer never calls either entry point. Upstream checks the incorrectly-cased legacy-style
    # OPENMP_C_FOUND name rather than FindOpenMP's OpenMP_C_FOUND, so link the required target here.
    target_link_libraries(tinygl-static PUBLIC OpenMP::OpenMP_C)

    # Upstream compiles with -march=native when not cross-compiling, so the archive is tuned for
    # the build host. Stated rather than overridden: it is upstream's own choice, and CNA builds
    # TinyGL from source per machine.
    message(STATUS "CNA: TinyGL pinned at ${CNA_TINYGL_GIT_TAG}")
endfunction()
