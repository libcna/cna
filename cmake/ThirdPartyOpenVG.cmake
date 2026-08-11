include_guard(GLOBAL)

# ShivaVG (https://github.com/ileben/ShivaVG) -- an LGPL-2.1 ANSI C implementation of the Khronos
# OpenVG 1.1 vector graphics API, built on fixed-function/immediate-mode desktop OpenGL. This is the
# genuine upstream the OPENVG CNA renderer talks to:
#
#     CNA -> OpenVgRenderer -> ShivaVG (real vgCreatePath/vgDrawPath/vgCreateImage/... OpenVG
#            entry points) -> OpenGL (via a context OpenVgRenderer creates itself with SDL)
#
# ShivaVG ships no CMake of its own -- autotools/configure only, and its last (and only) upstream
# commit is from 2010 -- so this fetches the source at a pinned commit and compiles the library's 11
# core .c files directly instead of running its autoconf toolchain. Two small compatibility gaps
# separate a from-2010 codebase from a 2020s host toolchain/headers; both are bridged with
# compiler-only flags/generated files below, no upstream source is patched.

set(CNA_OPENVG_GIT_REPOSITORY "https://github.com/ileben/ShivaVG.git" CACHE STRING
    "Upstream OpenVG implementation (ShivaVG) repository fetched for the OPENVG graphics renderer")
# ShivaVG publishes no tags/releases; this is its one and only upstream commit (2010-11-20,
# "Initial commit with subdivision cache") -- there is no newer revision to track.
set(CNA_OPENVG_GIT_TAG "6122ccb3c4b86f69a326f1a65b0f86bc79f69c50" CACHE STRING
    "Pinned ShivaVG commit used by the OPENVG graphics renderer")

# ShivaVG is ANSI C, but cna_configure_openvg() (below) runs from RendererSelection.cmake --
# called before add_subdirectory(modules), which is what first pulls in a C target
# (third_party/enet) today. The top-level `project(CNA LANGUAGES CXX)` never enables C, so without
# this the generator (Ninja) reaches its Generate step with no C compile/archive rules configured
# at all ("Error required internal CMake variable not set... CMAKE_C_COMPILE_OBJECT") -- found and
# verified empirically. Deliberately called here at this file's own top level (include()'d from
# directory scope), NOT inside cna_configure_openvg() itself: enable_language() called from inside
# a function() does not propagate the generator's internal per-language rule variables to the
# rest of the configure run (a real, reproducible CMake scoping gap, verified standalone) --
# calling it from a macro/top-level/include()'d-file scope is required. enable_language() is
# idempotent and safe to call again later (e.g. by ENet's own project(enet)).
enable_language(C)

# Configures the cna_thirdparty_shivavg static library every OPENVG renderer source links against.
function(cna_configure_openvg)
    if(TARGET cna_thirdparty_shivavg)
        return()
    endif()

    include(FetchContent)

    # P1-9: VGContext_dtor (src/shContext.c) never freed the paths/paints/images resource arrays'
    # own backing storage, nor c->defaultPaint's owned instops/stops arrays and 1D gradient GL
    # texture -- upstream only ever freed the individual path/paint/image objects inside those
    # arrays, not the arrays (or defaultPaint) themselves. A fixed 64-byte/5-allocation-plus-one-
    # GL-texture leak on every vgCreateContextSH/vgDestroyContextSH (OpenVgRenderer construct/
    # destroy) cycle, confirmed via LeakSanitizer and fixed here via a checked-in patch applied to
    # the fetched source -- not by hand-editing generated _deps content. See
    # cmake/patches/shivavg-context-dtor-leak.patch and docs/openvg-renderer.md.
    FetchContent_Declare(
        shivavg
        GIT_REPOSITORY "${CNA_OPENVG_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_OPENVG_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
        PATCH_COMMAND  "${CMAKE_COMMAND}"
                       "-DCNA_SHIVAVG_PATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/shivavg-context-dtor-leak.patch"
                       -P "${CMAKE_CURRENT_LIST_DIR}/patches/apply-shivavg-patch.cmake"
    )
    FetchContent_MakeAvailable(shivavg)

    if(NOT EXISTS "${shivavg_SOURCE_DIR}/src/shContext.c")
        message(FATAL_ERROR
            "CNA: fetched ShivaVG at ${shivavg_SOURCE_DIR} but src/shContext.c is missing -- the pin "
            "CNA_OPENVG_GIT_TAG=${CNA_OPENVG_GIT_TAG} may not be a ShivaVG checkout.")
    endif()

    # Gap 1: ShivaVG's src/shDefs.h hand-defines int8_t/uint8_t/int16_t/... unless HAVE_CONFIG_H is
    # set and its own "../config.h" (normally autoconf-generated) says HAVE_INTTYPES_H. Modern
    # <stdint.h> -- pulled in transitively by the system GL headers this same file includes a few
    # lines later -- already provides these, so the hand-rolled `#define uint8_t unsigned char`
    # etc. collide with it ("two or more data types in declaration specifiers"). Writing the one
    # line of config.h ShivaVG's build actually needs, at the exact relative path its headers
    # already `#include "../config.h"` from, is equivalent to what its configure script would have
    # generated -- no upstream file is modified.
    if(NOT EXISTS "${shivavg_SOURCE_DIR}/config.h")
        file(WRITE "${shivavg_SOURCE_DIR}/config.h" "#define HAVE_INTTYPES_H 1\n")
    endif()

    # Gap 2: shDefs.h includes <GL/gl.h>/<GL/glu.h>/<GL/glx.h> directly with GL_GLEXT_LEGACY
    # defined (deliberately skipping glext.h, ShivaVG's own compatibility choice for old GL
    # headers). A current mesa GL/glxext.h (pulled in transitively by glx.h) unconditionally
    # references the GLintptr/GLsizeiptr typedefs that glext.h would normally have supplied first,
    # so skipping it leaves them undefined ("unknown type name 'GLintptr'"). This one-line shim
    # defines just those two -- matching mesa's own `typedef ptrdiff_t GLintptr` -- and is
    # -included ahead of every ShivaVG translation unit; found and verified against this
    # environment's mesa headers (see openvg-spike/README.md).
    set(_cna_shivavg_glintptr_shim "${CMAKE_CURRENT_BINARY_DIR}/shivavg_glintptr_shim.h")
    file(WRITE "${_cna_shivavg_glintptr_shim}"
"#ifndef CNA_SHIVAVG_GLINTPTR_SHIM
#define CNA_SHIVAVG_GLINTPTR_SHIM
#include <stddef.h>
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
#endif
")

    file(GLOB _cna_shivavg_sources CONFIGURE_DEPENDS "${shivavg_SOURCE_DIR}/src/*.c")
    add_library(cna_thirdparty_shivavg STATIC ${_cna_shivavg_sources})
    # ShivaVG's own sources `#include "openvg.h"`/"vgu.h" (unqualified) -- the public API headers
    # live one level down at include/vg, so that directory (not include/) is what goes on the
    # PUBLIC path CNA's own OpenVgRenderer consumes as `#include "openvg.h"` too.
    target_include_directories(cna_thirdparty_shivavg PUBLIC "${shivavg_SOURCE_DIR}/include/vg")
    target_include_directories(cna_thirdparty_shivavg PRIVATE "${shivavg_SOURCE_DIR}" "${shivavg_SOURCE_DIR}/src")
    target_compile_definitions(cna_thirdparty_shivavg PRIVATE HAVE_CONFIG_H)
    # P1-8: forced-include is spelled differently per compiler driver -- GCC/Clang take it as two
    # tokens ("-include", "path"); MSVC (cl.exe, used even under CMake's Ninja/NMake generators on
    # Windows, not just the Visual Studio generator) needs the single-token "/FI\"path\"" form.
    # This project's own gap-2 shim only NEEDS to reach ShivaVG's translation units on the
    # platforms that actually hit gap 2 (Linux GLX headers referencing GLintptr/GLsizeiptr before
    # glext.h would have supplied them -- see the comment above); applying the wrong flag spelling
    # on MSVC would otherwise silently fail the whole configure with an "unknown argument" error.
    if(MSVC)
        target_compile_options(cna_thirdparty_shivavg PRIVATE "/FI${_cna_shivavg_glintptr_shim}")
    else()
        target_compile_options(cna_thirdparty_shivavg PRIVATE "-include" "${_cna_shivavg_glintptr_shim}")
    endif()
    set_target_properties(cna_thirdparty_shivavg PROPERTIES
        C_STANDARD 99
        POSITION_INDEPENDENT_CODE ON
    )

    # shImage.c calls gluScaleImage, shContext.c calls gluOrtho2D -- ShivaVG genuinely links GLU,
    # not just GL. Deliberately NOT `find_package(OpenGL REQUIRED COMPONENTS OpenGL GLU)`: CMake
    # 3.28's FindOpenGL.cmake has a real case-sensitivity bug in its COMPONENTS/HANDLE_COMPONENTS
    # path -- it sets the legacy `OPENGL_GLU_FOUND` but find_package_handle_standard_args checks
    # `OpenGL_GLU_FOUND` (matching CMAKE_FIND_PACKAGE_NAME's case) when COMPONENTS is used, so
    # `Could NOT find OpenGL (missing: GLU)` fires even with libGLU genuinely installed (verified
    # empirically against this project's own toolchain). A plain `find_package(OpenGL REQUIRED)`
    # takes the non-COMPONENTS path, which sets `OPENGL_GLU_FOUND`/creates `OpenGL::GLU` correctly
    # whenever GLU is actually present, and this project's own gate below turns an actually-missing
    # GLU into a clear configure-time error either way.
    find_package(OpenGL REQUIRED)
    if(NOT TARGET OpenGL::GLU)
        message(FATAL_ERROR
            "CNA: OPENVG renderer needs GLU (ShivaVG's shImage.c/shContext.c call "
            "gluScaleImage/gluOrtho2D) -- install libglu1-mesa-dev (Linux) or the equivalent GLU "
            "development package for your platform.")
    endif()
    target_link_libraries(cna_thirdparty_shivavg PUBLIC OpenGL::GL OpenGL::GLU)

    message(STATUS "CNA: ShivaVG (OpenVG) pinned at ${CNA_OPENVG_GIT_TAG}")
endfunction()
