# SKIA-159: a separately pinned Ganesh/OpenGL Skia artifact, additive to and independent of the
# validated raster artifact in ThirdPartySkia.cmake. This file defines CNA::SkiaGanesh but nothing
# in the ordinary CNA_GRAPHICS_BACKEND=SKIA selection links it yet -- construction-time mode
# selection is SKIA-160's job (see docs/skia-surface-mode-adr.md's six reopening requirements).
# Today this target exists only so a standalone probe (build-probe/) can prove the artifact and
# link line are genuinely correct; see docs/skia-ganesh-artifact.md.
#
# Skia uses GN, not CMake, and brings a substantial revision-locked dependency graph; a normal CNA
# configure must stay offline and deterministic, exactly like the raster artifact. See
# docs/skia-ganesh-artifact.md for the exact pinned revision and GN build command.

# Declared defensively (rather than assumed already-declared by ThirdPartySkia.cmake): this file
# must work standalone whenever only CNA_SKIA_GANESH_BUILD_DIR is set, regardless of which
# CNA_GRAPHICS_BACKEND is actually selected. A repeated CACHE declaration of the same variable
# name is a CMake no-op that preserves any already-set value.
set(CNA_SKIA_ROOT "" CACHE PATH
    "Path to the source checkout of the pinned Skia revision (required for CNA_GRAPHICS_BACKEND=SKIA \
or for CNA_SKIA_GANESH_BUILD_DIR)")
set(CNA_SKIA_GANESH_BUILD_DIR "" CACHE PATH
    "Path to the GN output directory containing the matching Ganesh/OpenGL libskia.a (optional; \
only required by targets that explicitly request CNA::SkiaGanesh)")

function(cna_configure_skia_ganesh)
    if(TARGET CNA::SkiaGanesh)
        return()
    endif()

    if(NOT CNA_SKIA_ROOT OR NOT EXISTS "${CNA_SKIA_ROOT}/include/core/SkSurface.h")
        message(FATAL_ERROR
            "CNA: SkiaGanesh requires -DCNA_SKIA_ROOT=<Skia source checkout> (the same checkout "
            "the raster artifact uses). See docs/skia-ganesh-artifact.md for the pinned revision.")
    endif()

    if(NOT EXISTS "${CNA_SKIA_ROOT}/include/gpu/ganesh/gl/GrGLDirectContext.h")
        message(FATAL_ERROR
            "CNA: -DCNA_SKIA_ROOT points at a Skia checkout with no Ganesh/GL public headers. "
            "This must be the same pinned revision as the raster artifact; Ganesh headers are "
            "part of every checkout of that revision regardless of GN build args.")
    endif()

    if(NOT CNA_SKIA_GANESH_BUILD_DIR OR NOT EXISTS "${CNA_SKIA_GANESH_BUILD_DIR}/libskia.a")
        message(FATAL_ERROR
            "CNA: SkiaGanesh requires -DCNA_SKIA_GANESH_BUILD_DIR=<GN output directory built with "
            "skia_use_gl=true skia_enable_ganesh=true, containing libskia.a>. This must be a "
            "different output directory than -DCNA_SKIA_BUILD_DIR (the raster artifact); the two "
            "are mutually exclusive GN configurations of the same source checkout and cannot share "
            "one output directory. See docs/skia-ganesh-artifact.md for the exact GN command.")
    endif()

    if("${CNA_SKIA_GANESH_BUILD_DIR}" STREQUAL "${CNA_SKIA_BUILD_DIR}")
        message(FATAL_ERROR
            "CNA: -DCNA_SKIA_GANESH_BUILD_DIR must not be the same path as -DCNA_SKIA_BUILD_DIR. "
            "The raster artifact (skia_use_gl=false skia_enable_ganesh=false) and the Ganesh "
            "artifact (skia_use_gl=true skia_enable_ganesh=true) are incompatible GN outputs of "
            "the same source checkout; each needs its own output directory.")
    endif()

    # Same six mutually dependent archives as the raster artifact (skia_enable_ganesh=true compiles
    # Ganesh/GL sources into libskia.a itself; it does not introduce a seventh archive). Verified
    # against the actual GN output directory listing when this artifact was first built -- see
    # docs/skia-ganesh-artifact.md.
    set(_cna_skia_ganesh_archives
        "${CNA_SKIA_GANESH_BUILD_DIR}/libskia.a"
        "${CNA_SKIA_GANESH_BUILD_DIR}/libskcms.a"
        "${CNA_SKIA_GANESH_BUILD_DIR}/liballocator_base.a"
        "${CNA_SKIA_GANESH_BUILD_DIR}/liballocator_core.a"
        "${CNA_SKIA_GANESH_BUILD_DIR}/liballocator_shim.a"
        "${CNA_SKIA_GANESH_BUILD_DIR}/libraw_ptr.a"
    )
    foreach(_cna_skia_ganesh_archive IN LISTS _cna_skia_ganesh_archives)
        if(NOT EXISTS "${_cna_skia_ganesh_archive}")
            message(FATAL_ERROR
                "CNA: SkiaGanesh build directory is incomplete; missing ${_cna_skia_ganesh_archive}. "
                "Rebuild the Ganesh target described in docs/skia-ganesh-artifact.md.")
        endif()
    endforeach()

    string(JOIN "," _cna_skia_ganesh_link_group_members ${_cna_skia_ganesh_archives})

    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)

    add_library(cna_third_party_skia_ganesh INTERFACE)
    add_library(CNA::SkiaGanesh ALIAS cna_third_party_skia_ganesh)
    target_include_directories(cna_third_party_skia_ganesh INTERFACE "${CNA_SKIA_ROOT}")
    target_link_libraries(cna_third_party_skia_ganesh INTERFACE
        "$<LINK_GROUP:RESCAN,${_cna_skia_ganesh_link_group_members}>"
        Threads::Threads
        OpenGL::GL
        "${CMAKE_DL_LIBS}"
    )
endfunction()
