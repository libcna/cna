# Skia is intentionally an externally prepared dependency rather than a CMake FetchContent
# download. Upstream Skia uses GN and brings a substantial, revision-locked dependency graph; a
# normal CNA configure must stay offline and deterministic. See docs/skia-renderer.md for the
# exact pinned revision and the minimal raster build command.

set(CNA_SKIA_ROOT "" CACHE PATH
    "Path to the source checkout of the pinned Skia revision (required for CNA_GRAPHICS_RENDERER=SKIA)")
set(CNA_SKIA_BUILD_DIR "" CACHE PATH
    "Path to the GN output directory containing the matching raster libskia.a")

function(cna_configure_skia)
    if(TARGET CNA::Skia)
        return()
    endif()

    if(NOT CNA_SKIA_ROOT OR NOT EXISTS "${CNA_SKIA_ROOT}/include/core/SkSurface.h")
        message(FATAL_ERROR
            "CNA: SKIA requires -DCNA_SKIA_ROOT=<Skia source checkout>. "
            "See docs/skia-renderer.md for the pinned revision and build command.")
    endif()

    if(NOT CNA_SKIA_BUILD_DIR OR NOT EXISTS "${CNA_SKIA_BUILD_DIR}/libskia.a")
        message(FATAL_ERROR
            "CNA: SKIA requires -DCNA_SKIA_BUILD_DIR=<matching GN output directory containing libskia.a>. "
            "See docs/skia-renderer.md for the exact raster build command.")
    endif()

    # The minimal raster GN build currently produces six mutually dependent archives. Keep them
    # in a linker group: changing their order would otherwise make static consumers fail with
    # unresolved PartitionAlloc/SkSL symbols even though all archive files are present.
    set(_cna_skia_archives
        "${CNA_SKIA_BUILD_DIR}/libskia.a"
        "${CNA_SKIA_BUILD_DIR}/libskcms.a"
        "${CNA_SKIA_BUILD_DIR}/liballocator_base.a"
        "${CNA_SKIA_BUILD_DIR}/liballocator_core.a"
        "${CNA_SKIA_BUILD_DIR}/liballocator_shim.a"
        "${CNA_SKIA_BUILD_DIR}/libraw_ptr.a"
    )
    foreach(_cna_skia_archive IN LISTS _cna_skia_archives)
        if(NOT EXISTS "${_cna_skia_archive}")
            message(FATAL_ERROR
                "CNA: SKIA build directory is incomplete; missing ${_cna_skia_archive}. "
                "Rebuild the raster target described in docs/skia-renderer.md.")
        endif()
    endforeach()

    # Do not add --start-group/--end-group as independent link items. CMake may reorder them
    # away from their archives while flattening an INTERFACE dependency into an executable.
    # LINK_GROUP preserves the relationship through all consuming targets.
    string(JOIN "," _cna_skia_link_group_members ${_cna_skia_archives})

    find_package(Threads REQUIRED)

    add_library(cna_third_party_skia INTERFACE)
    add_library(CNA::Skia ALIAS cna_third_party_skia)
    target_include_directories(cna_third_party_skia INTERFACE "${CNA_SKIA_ROOT}")
    target_link_libraries(cna_third_party_skia INTERFACE
        "$<LINK_GROUP:RESCAN,${_cna_skia_link_group_members}>"
        Threads::Threads
        "${CMAKE_DL_LIBS}"
    )
endfunction()
