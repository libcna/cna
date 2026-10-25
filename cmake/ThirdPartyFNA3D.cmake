# --- FNA3D (3D graphics library for FNA) and its MojoShader submodule ---
#
# plans/plan_fna3d.md FNA3D-1 / design decision 1: FNA3D is fetched at configure time at a pinned
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
# how a fork change is tried before the pin moves.

include_guard(GLOBAL)

# libcna/FNA3D `cna`: FNA3D 26.08 (32401479a3ab), CNA's four FNA3D commits, and the MojoShader
# submodule at libcna/mojoshader e6d80c19b3f6 (MojoShader 6333f74 and CNA's 133 commits). Bump
# deliberately: FNA3D's driver list and FNA3D_* ABI move between releases, and
# modules/renderers/fna3d/effects/ holds stock-effect binaries whose MojoShader parse path must keep
# matching the pinned library.
set(_cna_fna3d_default_repository "https://github.com/libcna/FNA3D.git")
set(_cna_fna3d_default_tag "f4bef618d2c2e59866b02896b52efa25815c829b")
# A build tree keeps the defaults of the CNA revision that first configured it. A cached value that
# is one of CNA's own former defaults follows the current one; any other value is a developer's
# override and is kept. Until the forks the defaults were upstream FNA3D at 3240147, which this file
# then patched -- fetched as it is now, that revision would build without CNA's fixes.
foreach(_cna_fna3d_setting IN ITEMS REPOSITORY TAG)
    string(TOLOWER "${_cna_fna3d_setting}" _cna_fna3d_setting_lower)
    set(_cna_fna3d_former_defaults "${_CNA_FNA3D_DEFAULT_GIT_${_cna_fna3d_setting}}")
    if(_cna_fna3d_setting STREQUAL "REPOSITORY")
        list(APPEND _cna_fna3d_former_defaults "https://github.com/FNA-XNA/FNA3D.git")
    else()
        list(APPEND _cna_fna3d_former_defaults "3240147")
    endif()
    get_property(_cna_fna3d_setting_type CACHE CNA_FNA3D_GIT_${_cna_fna3d_setting} PROPERTY TYPE)
    if(_cna_fna3d_setting_type STREQUAL "STRING"
       AND CNA_FNA3D_GIT_${_cna_fna3d_setting} IN_LIST _cna_fna3d_former_defaults
       AND NOT CNA_FNA3D_GIT_${_cna_fna3d_setting} STREQUAL _cna_fna3d_default_${_cna_fna3d_setting_lower})
        message(STATUS "CNA: CNA_FNA3D_GIT_${_cna_fna3d_setting} follows the new default "
            "(${CNA_FNA3D_GIT_${_cna_fna3d_setting}} -> ${_cna_fna3d_default_${_cna_fna3d_setting_lower}})")
        set_property(CACHE CNA_FNA3D_GIT_${_cna_fna3d_setting} PROPERTY VALUE
            "${_cna_fna3d_default_${_cna_fna3d_setting_lower}}")
    endif()
    set(_CNA_FNA3D_DEFAULT_GIT_${_cna_fna3d_setting} "${_cna_fna3d_default_${_cna_fna3d_setting_lower}}"
        CACHE INTERNAL "CNA's own default for CNA_FNA3D_GIT_${_cna_fna3d_setting}")
endforeach()
set(CNA_FNA3D_GIT_REPOSITORY "${_cna_fna3d_default_repository}"
    CACHE STRING "FNA3D repository fetched for the FNA3D graphics renderer (CNA's fork)")
set(CNA_FNA3D_GIT_TAG "${_cna_fna3d_default_tag}"
    CACHE STRING "Pinned FNA3D commit used by the FNA3D graphics renderer")

# A FETCHCONTENT_SOURCE_DIR_FNA3D checkout is built as it is. Say so when it is not the pinned
# commit, or has local changes in FNA3D or its MojoShader submodule, because no step here brings
# it to the pin any more. Read-only: a shared checkout may not be writable.
function(_cna_fna3d_report_source_override source_dir)
    find_package(Git QUIET)
    if(NOT GIT_EXECUTABLE OR NOT EXISTS "${source_dir}/.git")
        message(STATUS "CNA: FNA3D source ${source_dir} is not a git checkout; built as it is")
        return()
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
        OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --verify --quiet
                "${CNA_FNA3D_GIT_TAG}^{commit}"
        OUTPUT_VARIABLE _pin OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" --no-optional-locks -C "${source_dir}" status --porcelain
        OUTPUT_VARIABLE _changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_head STREQUAL "" OR NOT _head STREQUAL _pin)
        message(WARNING
            "CNA: FETCHCONTENT_SOURCE_DIR_FNA3D=${source_dir} is at '${_head}', not the pinned "
            "CNA_FNA3D_GIT_TAG=${CNA_FNA3D_GIT_TAG} of ${CNA_FNA3D_GIT_REPOSITORY}. It is built as "
            "it is; CNA no longer patches FNA3D or MojoShader at configure time.")
    endif()
    if(NOT _changes STREQUAL "")
        string(REPLACE "\n" "; " _changes "${_changes}")
        message(WARNING
            "CNA: FETCHCONTENT_SOURCE_DIR_FNA3D=${source_dir} has local changes (${_changes}). "
            "They are built as they are.")
    endif()
endfunction()

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
    # variable back afterwards -- FNA3D's own cmake_minimum_required(3.10) puts CMP0077 in OLD
    # mode, where option() overrides a plain normal variable, so the cache entry is the only
    # reliable lever here.
    set(_cna_fna3d_saved_shared "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fna3d)
    set(BUILD_SHARED_LIBS "${_cna_fna3d_saved_shared}" CACHE BOOL "" FORCE)

    if(FETCHCONTENT_SOURCE_DIR_FNA3D)
        _cna_fna3d_report_source_override("${fna3d_SOURCE_DIR}")
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
            "-DFETCHCONTENT_SOURCE_DIR_FNA3D, run: git submodule update --init.")
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

# plans/plan_sdlgpu.md SDLGPU-93: use CNA's statically linked, process-initialized
# SDL_shadercross rather than MojoShader's optional shared-library probe. This is a separate helper
# because the same MojoShader target also serves renderers that do not use SDL_gpu or ShaderCross.
function(cna_enable_mojoshader_sdl_gpu_shadercross)
    if(NOT TARGET mojoshader OR NOT TARGET cna_mojoshader)
        message(FATAL_ERROR
            "CNA: the SDL_GPU ShaderCross bridge requires cna_configure_mojoshader() first")
    endif()
    if(NOT TARGET cna_sdl_shadercross)
        message(FATAL_ERROR
            "CNA: the SDL_GPU MojoShader bridge requires the pinned SDL_shadercross target")
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
