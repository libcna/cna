# --- FNA3D (3D graphics library for FNA) and its MojoShader submodule ---
#
# FNA3D-1: FNA3D is fetched at configure time at a pinned
# commit, in the same spirit as the WEBGPU integration. FNA3D is a real multi-file C library (not a
# single-header drop-in), so this actually builds a static archive; it carries MojoShader as a git
# submodule, which the fetch must recurse into because FNA3D's own CMakeLists.txt compiles
# MojoShader's translation units directly.
#
# The pin is CNA's fork https://github.com/libcna/FNA3D (upstream https://github.com/FNA-XNA/FNA3D).
# Its `cna` branch is upstream FNA3D plus CNA's FNA3D fixes, one commit each, and its MojoShader
# submodule points at the fork https://github.com/libcna/mojoshader (upstream
# https://github.com/icculus/mojoshader), whose `cna` branch carries CNA's Effect parser,
# shader-validation and shader-profile fixes the same way. Those fixes were once a series of patches
# applied here at configure time; they are history in the forks now, so the fetched or supplied
# checkout is used exactly as it is and nothing here writes to it. docs/fna3d-renderer.md says how
# the forks follow upstream.
#
# FNA3D's only dependency is SDL 3.2.0 or newer -- exactly the SDL3 CNA already vendors -- so the
# fetched project resolves SDL3::SDL3 from CNA's own already-configured imported target
# (cna_configure_vendored_sdl() runs from the root CMakeLists.txt before renderer selection). No
# second SDL build, no system SDL requirement.
#
# Offline / air-gapped builds: pass -DFETCHCONTENT_SOURCE_DIR_FNA3D=/path/to/FNA3D to point at an
# existing checkout (CMake's own built-in per-dependency override, so no CNA-specific variable is
# needed): a clone of the fork at CNA_FNA3D_GIT_TAG with its MojoShader submodule initialized
# (git submodule update --init). A checkout at another revision is reported, not refused -- that is
# how a fork change is tried before the pin moves. A checkout FetchContent populated itself is held
# to the pin instead: cmake/Fna3dSourceCheck.cmake compares it on every configure.

include_guard(GLOBAL)

# libcna/FNA3D `cna`: FNA3D 26.08 (32401479a3ab) with CNA's four FNA3D commits, merged with release
# 26.10 (62a41708e975), then the fixes for the 2026-10-26 dependency audit and its review
# (a78cbed..10f650b9d0f6); the MojoShader submodule at libcna/mojoshader 03b686fe1a33 (MojoShader
# 6333f74 with CNA's 133 commits, merged with upstream main ad5dff8, the revision 26.10 pins, then
# that audit's and review's fixes and the fork's regression tests in tests/cna). Bump
# deliberately: FNA3D's driver list and FNA3D_* ABI move between releases, and
# modules/renderers/fna3d/effects/ holds stock-effect binaries whose MojoShader parse path must keep
# matching the pinned library.
#
# A build tree keeps the defaults of the CNA revision that first configured it; CNA's own defaults
# follow this pin, as one pair, and a developer's choice is never replaced. Until the forks the
# defaults were upstream FNA3D at 3240147, which this file then patched -- fetched as it is now,
# that revision would build without CNA's fixes. cmake/Fna3dSourcePin.cmake has the rules.
include("${CMAKE_CURRENT_LIST_DIR}/Fna3dSourcePin.cmake")
cna_fna3d_select_source_pin(
    DEFAULT_REPOSITORY "https://github.com/libcna/FNA3D.git"
    DEFAULT_TAG "10f650b9d0f6bb0e6f3f9378833fbd807dfe8cde")

# Every configure compares the checkout with the pin, read-only: FNA3D's HEAD, the MojoShader
# commit it records against MojoShader's own HEAD, and local changes in either. A
# FETCHCONTENT_SOURCE_DIR_FNA3D checkout is reported; a FetchContent-populated one is refused unless
# this option says otherwise (2026-10-26 dependency audit D-03/D-04).
include("${CMAKE_CURRENT_LIST_DIR}/Fna3dSourceCheck.cmake")
option(CNA_FNA3D_ALLOW_SOURCE_DRIFT
    "Build a FetchContent-populated FNA3D checkout that is not at CNA_FNA3D_GIT_TAG, has a MojoShader other than the one it records, or has local changes, with a warning instead of an error"
    OFF)

# FX-061/FX-062/FX-063/FX-065: fetches the pin and publishes `cna_mojoshader` alone --
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

    # MojoShader itself has two standard-library routes -- SDL's (MOJOSHADER_USE_SDL_STDLIB, which
    # FNA3D's build selects) and the C library's.
    # A CNA_ENABLE_SDL=OFF configuration has no SDL3 target by design, so there the archive is built
    # on the C library route, without its SDL_GPU adapter, instead of refusing every compiled-effect
    # backend outright. FNA3D itself still needs SDL3; its renderer is refused by name in such a
    # configuration (cmake/SdlAvailability.cmake).
    set(_cna_mojoshader_without_sdl FALSE)
    if(NOT TARGET SDL3::SDL3)
        if(CNA_ENABLE_SDL)
            message(FATAL_ERROR
                "CNA: MojoShader and FNA3D need SDL3::SDL3 before cna_configure_mojoshader() "
                "runs -- both depend solely on SDL 3.2.0 or newer and resolve it from this "
                "target.")
        endif()
        set(_cna_mojoshader_without_sdl TRUE)
    endif()

    # GIT_TAG is a full commit, so once the checkout is at it the update step returns without
    # contacting the remote; when the pin moves it fetches and checks out the new commit and its
    # submodule.
    FetchContent_Declare(
        fna3d
        GIT_REPOSITORY "${CNA_FNA3D_GIT_REPOSITORY}"
        GIT_TAG        "${CNA_FNA3D_GIT_TAG}"
        GIT_SHALLOW    FALSE
        GIT_PROGRESS   TRUE
        GIT_SUBMODULES_RECURSE TRUE
        EXCLUDE_FROM_ALL
    )

    # FNA3D defaults to a shared library. CNA links every renderer's dependencies statically into
    # the one executable, so force a static archive for the duration of the fetch and put the
    # variable back afterwards exactly as it was (2026-10-26 dependency audit C-07). Both levers are
    # needed: FNA3D's own cmake_minimum_required(3.10) puts CMP0077 in OLD mode, where its option()
    # replaces a plain normal variable with a new ON cache entry, or takes its own default over an
    # untyped one -- so a typed cache entry has to exist; and under CMP0126 NEW a caller's normal
    # BUILD_SHARED_LIBS=ON hides that entry -- so a normal variable is set in this function's scope,
    # which FNA3D's directory inherits and the caller never sees.
    if(DEFINED CACHE{BUILD_SHARED_LIBS})
        set(_cna_fna3d_shared_cached TRUE)
        set(_cna_fna3d_shared_value "$CACHE{BUILD_SHARED_LIBS}")
        get_property(_cna_fna3d_shared_type CACHE BUILD_SHARED_LIBS PROPERTY TYPE)
        get_property(_cna_fna3d_shared_help CACHE BUILD_SHARED_LIBS PROPERTY HELPSTRING)
    else()
        set(_cna_fna3d_shared_cached FALSE)
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(fna3d)
    if(_cna_fna3d_shared_cached)
        set_property(CACHE BUILD_SHARED_LIBS PROPERTY VALUE "${_cna_fna3d_shared_value}")
        set_property(CACHE BUILD_SHARED_LIBS PROPERTY TYPE "${_cna_fna3d_shared_type}")
        set_property(CACHE BUILD_SHARED_LIBS PROPERTY HELPSTRING "${_cna_fna3d_shared_help}")
    else()
        unset(BUILD_SHARED_LIBS CACHE)
    endif()
    unset(BUILD_SHARED_LIBS)

    # FetchContent_Declare(EXCLUDE_FROM_ALL) reaches add_subdirectory() only from CMake 3.28, and
    # not at all when a dependency provider populates FNA3D; CNA supports 3.20. Exclude the
    # directory itself, which every supported CMake honours, so a configuration that wants only
    # MojoShader never builds FNA3D as part of `all` -- the FNA3D renderer still gets it as a link
    # dependency of cna_fna3d (2026-10-26 dependency audit C-06).
    if(TARGET FNA3D)
        get_target_property(_cna_fna3d_directory FNA3D SOURCE_DIR)
        get_target_property(_cna_fna3d_imported FNA3D IMPORTED)
        if(_cna_fna3d_directory AND NOT _cna_fna3d_imported)
            set_property(DIRECTORY "${_cna_fna3d_directory}" PROPERTY EXCLUDE_FROM_ALL TRUE)
        endif()
    endif()

    find_package(Git QUIET)
    if(FETCHCONTENT_SOURCE_DIR_FNA3D)
        set(_cna_fna3d_source_mode OVERRIDE)
    else()
        set(_cna_fna3d_source_mode PINNED)
    endif()
    cna_fna3d_verify_source(
        SOURCE_DIR  "${fna3d_SOURCE_DIR}"
        TAG         "${CNA_FNA3D_GIT_TAG}"
        REPOSITORY  "${CNA_FNA3D_GIT_REPOSITORY}"
        MODE        ${_cna_fna3d_source_mode}
        ALLOW_DRIFT "${CNA_FNA3D_ALLOW_SOURCE_DRIFT}"
        GIT         "${GIT_EXECUTABLE}")

    if(NOT TARGET FNA3D)
        message(FATAL_ERROR
            "CNA: fetched FNA3D at ${fna3d_SOURCE_DIR} but no FNA3D target was defined -- the pin "
            "CNA_FNA3D_GIT_TAG=${CNA_FNA3D_GIT_TAG} may not be an FNA3D checkout.")
    endif()

    if(_cna_mojoshader_without_sdl AND TARGET mojoshader)
        # FNA3D's CMakeLists selects SDL's standard library for everything in its directory
        # (add_definitions), and compiles the SDL_GPU adapter unconditionally. Both are taken back
        # for this configuration only; every other MojoShader switch FNA3D sets (effect support,
        # depth clipping, render-target flip, XNA 4 vertex textures, disabled profiles) stays
        # exactly as FNA3D's own build defines it.
        get_property(_cna_fna3d_definitions DIRECTORY "${fna3d_SOURCE_DIR}"
                     PROPERTY COMPILE_DEFINITIONS)
        list(REMOVE_ITEM _cna_fna3d_definitions MOJOSHADER_USE_SDL_STDLIB USE_SDL3)
        set_property(DIRECTORY "${fna3d_SOURCE_DIR}"
                     PROPERTY COMPILE_DEFINITIONS "${_cna_fna3d_definitions}")
        get_target_property(_cna_mojoshader_sources mojoshader SOURCES)
        list(FILTER _cna_mojoshader_sources EXCLUDE REGEX "mojoshader_sdlgpu\\.c$")
        set_property(TARGET mojoshader PROPERTY SOURCES "${_cna_mojoshader_sources}")
        message(STATUS
            "CNA: MojoShader is built on the C standard library, without its SDL_GPU adapter "
            "(CNA_ENABLE_SDL=OFF)")
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

    # FX-061/FX-062/FX-063/FX-065: the Effect Framework parser is not FNA3D's. FNA3D
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
    #                                so the header CNA sees is the header the archive was built from
    #                                (absent, like the archive's, in a CNA_ENABLE_SDL=OFF build).
    if(TARGET mojoshader)
        # FNA3D compiles MojoShader's "bytecode" profile out. The DIRECTX9 renderer feeds an
        # effect's Direct3D 9 shader tokens to the device unchanged and asks MojoShader only for
        # their reflection, which that profile provides for every shader model the tokens can hold
        # (the HLSL profile stops at ps_1_x instructions it never learnt to re-emit, TEXCRD among
        # them).
        get_property(_cna_fna3d_definitions DIRECTORY "${fna3d_SOURCE_DIR}"
                     PROPERTY COMPILE_DEFINITIONS)
        list(REMOVE_ITEM _cna_fna3d_definitions SUPPORT_PROFILE_BYTECODE=0)
        set_property(DIRECTORY "${fna3d_SOURCE_DIR}"
                     PROPERTY COMPILE_DEFINITIONS "${_cna_fna3d_definitions}")
        target_sources(mojoshader PRIVATE
            "${fna3d_SOURCE_DIR}/MojoShader/profiles/mojoshader_profile_bytecode.c")
    endif()

    add_library(cna_mojoshader INTERFACE)
    if(TARGET mojoshader)
        target_link_libraries(cna_mojoshader INTERFACE mojoshader)
    else()
        # Older FNA3D revisions compiled MojoShader's translation units straight into FNA3D. Fall
        # back to that rather than failing: the symbols are still reachable, just not separable.
        target_link_libraries(cna_mojoshader INTERFACE FNA3D)
    endif()
    target_include_directories(cna_mojoshader SYSTEM INTERFACE
        "$<BUILD_INTERFACE:${fna3d_SOURCE_DIR}/MojoShader>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cna/dependencies/mojoshader>")
    target_compile_definitions(cna_mojoshader INTERFACE
        MOJOSHADER_EFFECT_SUPPORT
        MOJOSHADER_NO_VERSION_INCLUDE)
    if(NOT _cna_mojoshader_without_sdl)
        target_compile_definitions(cna_mojoshader INTERFACE
            MOJOSHADER_USE_SDL_STDLIB
            USE_SDL3)
    endif()

    message(STATUS "CNA: MojoShader pinned with FNA3D ${CNA_FNA3D_GIT_TAG} (${fna3d_SOURCE_DIR})")
endfunction()

# SDLGPU-93: use CNA's statically linked, process-initialized
# SDL_shadercross rather than MojoShader's optional shared-library probe. This is a separate helper
# because the same MojoShader target also serves renderers that do not use SDL_gpu or ShaderCross.
function(cna_enable_mojoshader_sdl_gpu_shadercross)
    if(NOT TARGET mojoshader OR NOT TARGET cna_mojoshader)
        message(FATAL_ERROR
            "CNA: the MojoShader SDL_GPU ShaderCross bridge requires cna_configure_mojoshader() first")
    endif()
    if(NOT TARGET cna_sdl_shadercross)
        message(FATAL_ERROR
            "CNA: the MojoShader SDL_GPU bridge requires the pinned SDL_shadercross target")
    endif()

    target_compile_definitions(mojoshader PRIVATE
        CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC)
    target_link_libraries(mojoshader PRIVATE cna_sdl_shadercross)
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
        "$<BUILD_INTERFACE:${_cna_fna3d_source_dir}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cna/dependencies/fna3d>")

    message(STATUS "CNA: FNA3D pinned at ${CNA_FNA3D_GIT_TAG} (${_cna_fna3d_source_dir})")
endfunction()
