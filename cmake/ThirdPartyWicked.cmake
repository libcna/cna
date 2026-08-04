# Wicked Engine integration for the CNA WICKED graphics backend (plan_wicked.md).
#
# CNA consumes exactly one layer of Wicked Engine: its render hardware interface,
# `wi::graphics::GraphicsDevice` (plus `wi::shadercompiler`). None of the engine layers above it
# (wi::renderer, wi::scene, wi::Application, wi::input, physics, Lua) are used -- CNA owns the XNA
# runtime and only needs a device abstraction underneath it (plan_wicked.md design decision 1).
#
# The preferred input is a local Wicked Engine checkout:
#   cmake -DCNA_GRAPHICS_BACKEND=WICKED -DCNA_WICKED_ROOT=/path/to/WickedEngine ...
#
# When CNA_WICKED_ROOT is empty and CNA_WICKED_AUTO_FETCH is ON, the pinned revision is cloned via
# FetchContent. Auto-fetch defaults to OFF (unlike cmake/ThirdPartyWebGPU.cmake's small binary
# release download) because the Wicked Engine repository carries its `Content/` sample assets and a
# full clone is measured in gigabytes -- silently triggering that from a plain configure would be a
# surprise, so it must be asked for by name.
#
# Both paths require the SDL3 platform patch below; see cna_wicked_check_sdl3_support().

include_guard(GLOBAL)

set(CNA_WICKED_COMMIT "27c0df160d738925474a2181d3f88bfd59edaefe"
    CACHE STRING "Pinned Wicked Engine revision used by CNA")
set(CNA_WICKED_ROOT "" CACHE PATH "Root of a local Wicked Engine checkout")
option(CNA_WICKED_AUTO_FETCH "Clone the pinned Wicked Engine revision when CNA_WICKED_ROOT is empty" OFF)
option(CNA_WICKED_APPLY_SDL3_PATCH "Apply cmake/patches/wicked-sdl3-platform.patch to the resolved Wicked Engine checkout when it is not already SDL3-aware" ON)

set(CNA_WICKED_SDL3_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/wicked-sdl3-platform.patch"
    CACHE FILEPATH "SDL3 platform patch applied to Wicked Engine")

# Wicked Engine's Unix platform layer is SDL2-only upstream: wiPlatform.h calls SDL2 window
# functions unconditionally under PLATFORM_LINUX, and GraphicsDevice_Vulkan::CreateSwapChain has a
# hard `#error PLATFORM NOT SUPPORTED` when neither _WIN32 nor SDL2 is defined. CNA is SDL3-only
# and hands the backend an SDL3 window, and SDL2 and SDL3 cannot be loaded into one process (they
# export the same symbol names with different ABIs), so the patch adds a parallel SDL3 branch
# everywhere the SDL2 one exists (plan_wicked.md design decision 2).
function(cna_wicked_check_sdl3_support _root)
    file(READ "${_root}/WickedEngine/wiPlatform.h" _platform_header)
    if(_platform_header MATCHES "SDL3")
        return()
    endif()

    if(NOT CNA_WICKED_APPLY_SDL3_PATCH)
        message(FATAL_ERROR
            "CNA Wicked: the Wicked Engine checkout at '${_root}' has no SDL3 platform support and "
            "CNA_WICKED_APPLY_SDL3_PATCH=OFF. Apply ${CNA_WICKED_SDL3_PATCH} manually "
            "(git -C ${_root} apply <patch>) or re-enable the option.")
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(FATAL_ERROR
            "CNA Wicked: git is required to apply ${CNA_WICKED_SDL3_PATCH} to '${_root}'.")
    endif()

    message(STATUS "CNA Wicked: applying SDL3 platform patch to ${_root}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${CNA_WICKED_SDL3_PATCH}"
        WORKING_DIRECTORY "${_root}"
        RESULT_VARIABLE _patch_result
        ERROR_VARIABLE _patch_error)
    if(NOT _patch_result EQUAL 0)
        message(FATAL_ERROR
            "CNA Wicked: failed to apply ${CNA_WICKED_SDL3_PATCH} to '${_root}':\n${_patch_error}\n"
            "The patch is authored against Wicked Engine ${CNA_WICKED_COMMIT}; a different revision "
            "may need it rebasing.")
    endif()
endfunction()

function(cna_configure_wicked)
    if(TARGET WickedEngine)
        return()
    endif()

    set(_root "${CNA_WICKED_ROOT}")

    if(NOT _root)
        if(NOT CNA_WICKED_AUTO_FETCH)
            message(FATAL_ERROR
                "CNA Wicked: CNA_WICKED_ROOT is empty and CNA_WICKED_AUTO_FETCH=OFF. Clone Wicked "
                "Engine once and point CNA at it:\n"
                "  git clone https://github.com/turanszkij/WickedEngine.git\n"
                "  git -C WickedEngine checkout ${CNA_WICKED_COMMIT}\n"
                "  cmake -DCNA_GRAPHICS_BACKEND=WICKED -DCNA_WICKED_ROOT=<path> ...\n"
                "or configure with -DCNA_WICKED_AUTO_FETCH=ON to let CMake clone it (several GB).")
        endif()

        include(FetchContent)
        FetchContent_Declare(
            wicked_engine
            GIT_REPOSITORY https://github.com/turanszkij/WickedEngine.git
            GIT_TAG ${CNA_WICKED_COMMIT}
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE
        )
        # Populate only -- the Wicked subdirectory is added below, after its own options are set.
        FetchContent_Populate(wicked_engine)
        set(_root "${wicked_engine_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${_root}/WickedEngine/wiGraphicsDevice.h")
        message(FATAL_ERROR
            "CNA Wicked: '${_root}' does not look like a Wicked Engine checkout "
            "(WickedEngine/wiGraphicsDevice.h is missing).")
    endif()

    if(NOT WIN32)
        cna_wicked_check_sdl3_support("${_root}")
        set(WICKED_USE_SDL3 ON CACHE BOOL "" FORCE)
    endif()

    # CNA needs the library only. Every sample/editor/template target is switched off: they build
    # the full engine application layer (SDL2 event loop, ImGui, editor content) that CNA replaces
    # with its own XNA Game loop, and several of them are not even present in a sparse checkout.
    set(WICKED_EDITOR OFF CACHE BOOL "" FORCE)
    set(WICKED_TESTS OFF CACHE BOOL "" FORCE)
    set(WICKED_IMGUI_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(WICKED_LINUX_TEMPLATE OFF CACHE BOOL "" FORCE)
    set(WICKED_WINDOWS_TEMPLATE OFF CACHE BOOL "" FORCE)
    # CNA compiles its own shaders through wi::shadercompiler at runtime (plan_wicked.md design
    # decision 4) and never reads Wicked's own shader dump, so the embedded-shader variant -- which
    # additionally requires running offlineshadercompiler over the whole engine shader set at build
    # time -- is deliberately not used.
    set(WICKED_EMBED_SHADERS OFF CACHE BOOL "" FORCE)
    set(WICKED_ENABLE_SYMLINKS OFF CACHE BOOL "" FORCE)

    add_subdirectory("${_root}" "${CMAKE_BINARY_DIR}/_deps/wicked-build" EXCLUDE_FROM_ALL)

    if(NOT TARGET WickedEngine)
        message(FATAL_ERROR "CNA Wicked: the WickedEngine target was not created by '${_root}'.")
    endif()

    set(CNA_WICKED_RESOLVED_ROOT "${_root}" CACHE INTERNAL "Resolved Wicked Engine root")
    # WickedEngine loads libdxcompiler.so/dxcompiler.dll at runtime (wi::shadercompiler), so the
    # shared object has to sit next to the executable that uses it.
    if(WIN32)
        set(_dxc "${_root}/WickedEngine/dxcompiler.dll")
    elseif(APPLE)
        set(_dxc "${_root}/WickedEngine/libdxcompiler.dylib")
    else()
        set(_dxc "${_root}/WickedEngine/libdxcompiler.so")
    endif()
    if(EXISTS "${_dxc}")
        set(CNA_WICKED_DXCOMPILER "${_dxc}" CACHE INTERNAL "Wicked Engine dxcompiler runtime library")
    else()
        message(WARNING
            "CNA Wicked: ${_dxc} was not found in the checkout -- runtime shader compilation "
            "(wi::shadercompiler) will fail until it is available next to the executable.")
    endif()

    message(STATUS "CNA Wicked: using Wicked Engine from ${_root}")
endfunction()
