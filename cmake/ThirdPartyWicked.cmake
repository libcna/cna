# Wicked Engine integration for the CNA WICKED graphics renderer (plan_wicked.md).
#
# CNA consumes exactly one layer of Wicked Engine: its render hardware interface,
# `wi::graphics::GraphicsDevice` (plus `wi::shadercompiler`). None of the engine layers above it
# (wi::renderer, wi::scene, wi::Application, wi::input, physics, Lua) are used -- CNA owns the XNA
# runtime and only needs a device abstraction underneath it (plan_wicked.md design decision 1).
#
# The preferred input is a local Wicked Engine checkout:
#   cmake -DCNA_GRAPHICS_RENDERER=WICKED -DCNA_WICKED_ROOT=/path/to/WickedEngine ...
#
# When CNA_WICKED_ROOT is empty and CNA_WICKED_AUTO_FETCH is ON, the pinned revision is cloned via
# FetchContent. Auto-fetch defaults to OFF (unlike cmake/ThirdPartyWebGPU.cmake's small binary
# release download) because the Wicked Engine repository carries its `Content/` sample assets and a
# full clone is measured in gigabytes -- silently triggering that from a plain configure would be a
# surprise, so it must be asked for by name.
#
# Both paths require the CNA native-window patch below; see cna_wicked_check_platform_support().

include_guard(GLOBAL)

set(CNA_WICKED_COMMIT "27c0df160d738925474a2181d3f88bfd59edaefe"
    CACHE STRING "Pinned Wicked Engine revision used by CNA")
set(CNA_WICKED_ROOT "" CACHE PATH "Root of a local Wicked Engine checkout")
option(CNA_WICKED_AUTO_FETCH "Clone the pinned Wicked Engine revision when CNA_WICKED_ROOT is empty" OFF)
option(CNA_WICKED_APPLY_PLATFORM_PATCH "Apply CNA's native-window platform patch to the resolved Wicked Engine checkout" ON)
option(CNA_WICKED_APPLY_TEARDOWN_PATCH "Apply cmake/patches/wicked-device-teardown.patch to the resolved Wicked Engine checkout when its Vulkan device still leaks at destruction" ON)
option(CNA_WICKED_APPLY_STAGING_FOOTPRINT_PATCH "Apply cmake/patches/wicked-staging-footprint.patch to the resolved Wicked Engine checkout when its Vulkan UPLOAD/READBACK staging buffers are still allocated smaller than their mapped-layout footprint" ON)

set(CNA_WICKED_PLATFORM_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/wicked-cna-platform.patch"
    CACHE FILEPATH "Native-window platform patch applied to Wicked Engine")
set(CNA_WICKED_LEGACY_SDL3_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/wicked-sdl3-platform-legacy.patch"
    CACHE FILEPATH "Legacy patch reversed when upgrading an already-patched checkout")
set(CNA_WICKED_TEARDOWN_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/wicked-device-teardown.patch"
    CACHE FILEPATH "Vulkan device teardown patch applied to Wicked Engine")
set(CNA_WICKED_STAGING_FOOTPRINT_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/wicked-staging-footprint.patch"
    CACHE FILEPATH "Vulkan staging-buffer footprint patch applied to Wicked Engine")

# Wicked Engine's Unix platform layer is SDL2-only upstream. CNA supplies an immutable native
# window snapshot instead; the patch adds that narrow X11/Vulkan bridge and selects Wicked's
# existing no-backend audio stubs, avoiding both the SDL2 and FAudio dependencies.
function(cna_wicked_check_platform_support _root)
    file(READ "${_root}/WickedEngine/wiPlatform.h" _platform_header)
    if(_platform_header MATCHES "WICKED_CNA_PLATFORM")
        return()
    endif()

    if(NOT CNA_WICKED_APPLY_PLATFORM_PATCH)
        message(FATAL_ERROR
            "CNA Wicked: the checkout at '${_root}' has no CNA native-window support and "
            "CNA_WICKED_APPLY_PLATFORM_PATCH=OFF. Apply ${CNA_WICKED_PLATFORM_PATCH} manually "
            "(git -C ${_root} apply <patch>) or re-enable the option.")
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(FATAL_ERROR
            "CNA Wicked: git is required to apply ${CNA_WICKED_PLATFORM_PATCH} to '${_root}'.")
    endif()

    # Older CNA configurations permanently applied the former SDL3 bridge to the external
    # checkout. Return that exact patch first so the new patch always targets the pinned upstream
    # tree instead of depending on which CNA version configured it last.
    if(_platform_header MATCHES "SDL3")
        message(STATUS "CNA Wicked: removing legacy SDL3 platform patch from ${_root}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --reverse --whitespace=nowarn
                    "${CNA_WICKED_LEGACY_SDL3_PATCH}"
            WORKING_DIRECTORY "${_root}"
            RESULT_VARIABLE _legacy_result
            ERROR_VARIABLE _legacy_error)
        if(NOT _legacy_result EQUAL 0)
            message(FATAL_ERROR
                "CNA Wicked: failed to reverse ${CNA_WICKED_LEGACY_SDL3_PATCH}:\n${_legacy_error}")
        endif()
    endif()

    message(STATUS "CNA Wicked: applying native-window platform patch to ${_root}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${CNA_WICKED_PLATFORM_PATCH}"
        WORKING_DIRECTORY "${_root}"
        RESULT_VARIABLE _patch_result
        ERROR_VARIABLE _patch_error)
    if(NOT _patch_result EQUAL 0)
        message(FATAL_ERROR
            "CNA Wicked: failed to apply ${CNA_WICKED_PLATFORM_PATCH} to '${_root}':\n${_patch_error}\n"
            "The patch is authored against Wicked Engine ${CNA_WICKED_COMMIT}; a different revision "
            "may need it rebasing.")
    endif()
endfunction()

# Wicked Engine's GraphicsDevice_Vulkan destructor at the pinned revision leaks at teardown
# (plan_wicked.md WICKED-78): the three null images created beside nullBuffer are never destroyed,
# so VMA's "Some allocations were not freed" assertion aborts the process whenever the allocator is
# actually torn down; and the pooled CommandList_Vulkan objects are never freed, so once any
# command list has touched its per-frame linear allocator, the retained GPUBuffer keeps the whole
# allocation handler -- VmaAllocator, VkDevice and VkInstance -- alive forever, which both leaks
# the device and hides the assertion. The patch releases both in the destructor.
function(cna_wicked_check_device_teardown_fix _root)
    file(READ "${_root}/WickedEngine/wiGraphicsDevice_Vulkan.cpp" _device_source)
    if(_device_source MATCHES "cmd_allocator\\.free")
        return()
    endif()

    if(NOT CNA_WICKED_APPLY_TEARDOWN_PATCH)
        message(FATAL_ERROR
            "CNA Wicked: the Wicked Engine checkout at '${_root}' still leaks its Vulkan device at "
            "teardown (WICKED-78) and CNA_WICKED_APPLY_TEARDOWN_PATCH=OFF. Apply "
            "${CNA_WICKED_TEARDOWN_PATCH} manually (git -C ${_root} apply <patch>) or re-enable "
            "the option.")
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(FATAL_ERROR
            "CNA Wicked: git is required to apply ${CNA_WICKED_TEARDOWN_PATCH} to '${_root}'.")
    endif()

    message(STATUS "CNA Wicked: applying Vulkan device teardown patch to ${_root}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${CNA_WICKED_TEARDOWN_PATCH}"
        WORKING_DIRECTORY "${_root}"
        RESULT_VARIABLE _patch_result
        ERROR_VARIABLE _patch_error)
    if(NOT _patch_result EQUAL 0)
        message(FATAL_ERROR
            "CNA Wicked: failed to apply ${CNA_WICKED_TEARDOWN_PATCH} to '${_root}':\n${_patch_error}\n"
            "The patch is authored against Wicked Engine ${CNA_WICKED_COMMIT}; a different revision "
            "may need it rebasing.")
    endif()
endfunction()

# At the pinned revision, GraphicsDevice_Vulkan::CreateTexture allocates UPLOAD/READBACK staging
# buffers with ComputeTextureMemorySizeInBytes -- the TIGHT texel size -- while the mapped layout
# it hands out (CreateTextureSubresourceDatas with optimalBufferCopyRowPitchAlignment) and the
# CopyTexture buffer addressing consume row pitches aligned to that limit (plan_wicked.md
# WICKED-80). Any subresource whose row bytes are not a multiple of the alignment makes the copy
# address past the end of the buffer (VUID-vkCmdCopyBufferToImage-pRegions-00171 /
# VUID-vkCmdCopyImageToBuffer-pRegions-00183), and whether the round trip corrupts depends only on
# what the suballocator placed next to the buffer. The patch sizes the buffer with exactly the
# footprint the mapped layout describes.
function(cna_wicked_check_staging_footprint_fix _root)
    file(READ "${_root}/WickedEngine/wiGraphicsDevice_Vulkan.cpp" _device_source)
    if(_device_source MATCHES "footprint CreateTextureSubresourceDatas lays out")
        return()
    endif()

    if(NOT CNA_WICKED_APPLY_STAGING_FOOTPRINT_PATCH)
        message(FATAL_ERROR
            "CNA Wicked: the Wicked Engine checkout at '${_root}' still under-allocates its Vulkan "
            "staging buffers (WICKED-80) and CNA_WICKED_APPLY_STAGING_FOOTPRINT_PATCH=OFF. Apply "
            "${CNA_WICKED_STAGING_FOOTPRINT_PATCH} manually (git -C ${_root} apply <patch>) or "
            "re-enable the option.")
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(FATAL_ERROR
            "CNA Wicked: git is required to apply ${CNA_WICKED_STAGING_FOOTPRINT_PATCH} to '${_root}'.")
    endif()

    message(STATUS "CNA Wicked: applying Vulkan staging-buffer footprint patch to ${_root}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${CNA_WICKED_STAGING_FOOTPRINT_PATCH}"
        WORKING_DIRECTORY "${_root}"
        RESULT_VARIABLE _patch_result
        ERROR_VARIABLE _patch_error)
    if(NOT _patch_result EQUAL 0)
        message(FATAL_ERROR
            "CNA Wicked: failed to apply ${CNA_WICKED_STAGING_FOOTPRINT_PATCH} to '${_root}':\n${_patch_error}\n"
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
                "  cmake -DCNA_GRAPHICS_RENDERER=WICKED -DCNA_WICKED_ROOT=<path> ...\n"
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
        cna_wicked_check_platform_support("${_root}")
        set(WICKED_USE_CNA_PLATFORM ON CACHE BOOL "" FORCE)
    endif()
    # Platform-independent: the Vulkan device is the one this renderer selects everywhere
    # (WICKED-60 keeps D3D12 unselectable), so its teardown fix applies on every platform.
    cna_wicked_check_device_teardown_fix("${_root}")
    # Equally platform-independent, and required by the WICKED-79/WICKED-80 staged-transfer
    # contract: every CNA texture upload/readback goes through these staging buffers.
    cna_wicked_check_staging_footprint_fix("${_root}")

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
