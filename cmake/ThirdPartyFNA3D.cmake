# --- FNA3D (3D graphics library for FNA, https://github.com/FNA-XNA/FNA3D) ---
#
# plans/plan_fna3d.md FNA3D-1 / design decision 1: FNA3D is fetched at configure time from the upstream
# repository at a pinned tag, in the same spirit as the SOKOL/BGFX/WEBGPU integrations. FNA3D is a
# real multi-file C library (not a single-header drop-in), so unlike sokol this actually builds a
# static archive; it carries MojoShader as a git submodule, which the fetch must recurse into
# because FNA3D's own CMakeLists.txt compiles MojoShader's translation units directly. CNA carries
# one narrow parser-robustness patch for that exact submodule revision; the fetch/configure path
# applies it automatically and idempotently.
#
# FNA3D's only dependency is SDL 3.2.0 or newer -- exactly the SDL3 CNA already vendors -- so the
# fetched project resolves SDL3::SDL3 from CNA's own already-configured imported target
# (cna_configure_vendored_sdl() runs from the root CMakeLists.txt before renderer selection). No
# second SDL build, no system SDL requirement.
#
# Offline / air-gapped builds: pass -DFETCHCONTENT_SOURCE_DIR_FNA3D=/path/to/FNA3D to point at an
# existing checkout (CMake's own built-in per-dependency override, so no CNA-specific variable is
# needed). That checkout must have its MojoShader submodule initialized.

include_guard(GLOBAL)

set(CNA_FNA3D_GIT_REPOSITORY "https://github.com/FNA-XNA/FNA3D.git"
    CACHE STRING "Upstream FNA3D repository fetched for the FNA3D graphics renderer")
# Release tag 26.08 (FNA3D's own calendar versioning), commit 3240147. Bump deliberately: FNA3D's
# driver list and FNA3D_* ABI move between releases, and modules/renderers/fna3d/effects/ holds
# stock-effect binaries whose MojoShader parse path must keep matching the pinned library.
set(CNA_FNA3D_GIT_TAG "3240147"
    CACHE STRING "Pinned FNA3D revision used by the FNA3D graphics renderer")

# plans/plan_fx.md FX-061/FX-062/FX-063/FX-065: fetches the pin and publishes `cna_mojoshader` alone --
# the Effect Framework parser plus the OpenGL, SDL_GPU and D3D11 adapters, with no FNA3D linked.
# A renderer other than FNA3D calls this directly, so it can read a compiled effect without
# depending on a second graphics API it does not use. `cna_configure_fna3d()` builds on top of it.
#
# The pin is FNA3D's repository because MojoShader ships as its submodule and has no release of its
# own that carries these adapters. FNA3D's own targets are declared EXCLUDE_FROM_ALL, so a
# configuration that wants only MojoShader does not compile FNA3D as a side effect; the FNA3D
# renderer still gets it, pulled in as a link dependency of `cna_fna3d`.
function(cna_configure_mojoshader)
    if(TARGET cna_mojoshader)
        return()
    endif()

    include(FetchContent)

    set(_cna_fna3d_mojoshader_patch
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-effect-parser-robustness.patch"
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-fragment-precision.patch"
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-vertex-color-clamp.patch"
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-glsl-texcrd.patch"
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-legacy-texcoord-input.patch"
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-unmatched-fragment-input.patch"
        "${CMAKE_CURRENT_LIST_DIR}/patches/mojoshader-6333f74-ilp32-float-literal.patch")
    set(_cna_fna3d_mojoshader_patch_script
        "${CMAKE_CURRENT_LIST_DIR}/patches/apply-fna3d-mojoshader-patch.cmake")

    if(NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR
            "CNA: MojoShader and FNA3D need SDL3::SDL3 before cna_configure_mojoshader() runs -- "
            "both depend solely on SDL 3.2.0 or newer and resolve it from this target.")
    endif()

    FetchContent_Declare(
        fna3d
        GIT_REPOSITORY "${CNA_FNA3D_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_FNA3D_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
        GIT_SUBMODULES_RECURSE TRUE
        EXCLUDE_FROM_ALL
        # Without this the update step re-checks out the pinned ref on EVERY configure, which
        # reverts the patch series below and makes the patch step rewrite -- and MojoShader
        # recompile -- every single time. The ref is pinned, so there is nothing to update to; the
        # explicit re-apply after MakeAvailable is what keeps the sources correct regardless.
        UPDATE_DISCONNECTED TRUE
        PATCH_COMMAND  "${CMAKE_COMMAND}"
                       "-DCNA_FNA3D_MOJOSHADER_PATCH_FILE=${_cna_fna3d_mojoshader_patch}"
                       -P "${_cna_fna3d_mojoshader_patch_script}"
    )

    # FNA3D defaults to a shared library. CNA links every renderer's dependencies statically into
    # the one executable, so force a static archive for the duration of the fetch and put the
    # variable back afterwards -- FNA3D's own cmake_minimum_required(3.10) puts CMP0077 in OLD
    # mode, where option() overrides a plain normal variable, so the cache entry is the only
    # reliable lever here.
    set(_cna_fna3d_saved_shared "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fna3d)
    set(BUILD_SHARED_LIBS "${_cna_fna3d_saved_shared}" CACHE BOOL "" FORCE)

    # A FETCHCONTENT_SOURCE_DIR_FNA3D override bypasses FetchContent's download/update/patch
    # steps. Re-run the idempotent script explicitly so an offline/local checkout receives the
    # same required fixes before any configured FNA3D target is compiled.
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DCNA_FNA3D_MOJOSHADER_PATCH_FILE=${_cna_fna3d_mojoshader_patch}"
                -P "${_cna_fna3d_mojoshader_patch_script}"
        WORKING_DIRECTORY "${fna3d_SOURCE_DIR}"
        RESULT_VARIABLE _cna_fna3d_mojoshader_patch_result)
    if(NOT _cna_fna3d_mojoshader_patch_result EQUAL 0)
        message(FATAL_ERROR
            "CNA: failed to ensure the required MojoShader patches are applied in "
            "${fna3d_SOURCE_DIR}.")
    endif()

    if(NOT TARGET FNA3D)
        message(FATAL_ERROR
            "CNA: fetched FNA3D at ${fna3d_SOURCE_DIR} but no FNA3D target was defined -- the pin "
            "CNA_FNA3D_GIT_TAG=${CNA_FNA3D_GIT_TAG} may not be an FNA3D checkout.")
    endif()
    if(NOT EXISTS "${fna3d_SOURCE_DIR}/MojoShader/mojoshader.h")
        message(FATAL_ERROR
            "CNA: fetched FNA3D at ${fna3d_SOURCE_DIR} but MojoShader/mojoshader.h is missing -- "
            "the MojoShader submodule was not initialized. For an offline checkout supplied via "
            "-DFETCHCONTENT_SOURCE_DIR_FNA3D, run: git submodule update --init --recursive.")
    endif()

    # FNA3D compiles with -Wall -pedantic and a C99 dialect of its own; nothing there is CNA's to
    # police, and the SDL3 GPU forward declarations MojoShader repeats trip -Wpedantic on every
    # translation unit. Keep the fetched project's warnings out of CNA's own build log without
    # weakening any CNA warning setting.
    foreach(_fna3d_target FNA3D mojoshader)
        if(TARGET ${_fna3d_target})
            get_target_property(_fna3d_includes ${_fna3d_target} INTERFACE_INCLUDE_DIRECTORIES)
            if(_fna3d_includes)
                set_target_properties(${_fna3d_target} PROPERTIES
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_fna3d_includes}")
            endif()
            set_property(TARGET ${_fna3d_target} APPEND PROPERTY
                COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:C>:-w>)
        endif()
    endforeach()

    # plans/plan_fx.md FX-061/FX-062/FX-063/FX-065: the Effect Framework parser is not FNA3D's. FNA3D
    # merely happens to be how CNA obtains it, because MojoShader ships as FNA3D's submodule and
    # FNA3D's own CMakeLists.txt already builds it as a separate `mojoshader` archive carrying the
    # OpenGL, SDL_GPU and D3D11 adapters. Publishing it as its own interface target is what lets a
    # renderer other than FNA3D parse a compiled effect without linking a second graphics API it
    # does not use. `cna_fna3d` then adds FNA3D itself on top.
    #
    # MOJOSHADER_EFFECT_SUPPORT   -- without it mojoshader.h hides every Effect Framework struct and
    #                                FNA3D_CreateEffect's out-parameter stays an incomplete type.
    # MOJOSHADER_NO_VERSION_INCLUDE -- mojoshader_version.h is generated by MojoShader's own build
    #                                and is not present in the submodule checkout.
    # MOJOSHADER_USE_SDL_STDLIB   -- matches the switch FNA3D's own build compiles MojoShader with,
    #                                so the header CNA sees is the header the archive was built from.
    add_library(cna_mojoshader INTERFACE)
    if(TARGET mojoshader)
        target_link_libraries(cna_mojoshader INTERFACE mojoshader)
    else()
        # Older FNA3D revisions compiled MojoShader's translation units straight into FNA3D. Fall
        # back to that rather than failing: the symbols are still reachable, just not separable.
        target_link_libraries(cna_mojoshader INTERFACE FNA3D)
    endif()
    target_include_directories(cna_mojoshader SYSTEM INTERFACE
        "${fna3d_SOURCE_DIR}/MojoShader")
    target_compile_definitions(cna_mojoshader INTERFACE
        MOJOSHADER_EFFECT_SUPPORT
        MOJOSHADER_NO_VERSION_INCLUDE
        MOJOSHADER_USE_SDL_STDLIB
        USE_SDL3)

    message(STATUS "CNA: MojoShader pinned with FNA3D ${CNA_FNA3D_GIT_TAG} (${fna3d_SOURCE_DIR})")
endfunction()

# Publishes `cna_fna3d`: FNA3D itself on top of the MojoShader target above.
function(cna_configure_fna3d)
    cna_configure_mojoshader()
    if(TARGET cna_fna3d)
        return()
    endif()

    FetchContent_GetProperties(fna3d SOURCE_DIR _cna_fna3d_source_dir)
    if(NOT TARGET FNA3D)
        message(FATAL_ERROR
            "CNA: no FNA3D target was defined -- the pin CNA_FNA3D_GIT_TAG=${CNA_FNA3D_GIT_TAG} "
            "may not be an FNA3D checkout.")
    endif()

    add_library(cna_fna3d INTERFACE)
    target_link_libraries(cna_fna3d INTERFACE FNA3D cna_mojoshader)
    target_include_directories(cna_fna3d SYSTEM INTERFACE
        "${_cna_fna3d_source_dir}/include")

    message(STATUS "CNA: FNA3D pinned at ${CNA_FNA3D_GIT_TAG} (${_cna_fna3d_source_dir})")
endfunction()
