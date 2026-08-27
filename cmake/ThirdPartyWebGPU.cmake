# WebGPU / wgpu-native integration for the CNA WebGPU graphics renderer.
#
# The preferred input is an extracted official wgpu-native binary release:
#   cmake -DCNA_GRAPHICS_RENDERER=WEBGPU \
#         -DCNA_WEBGPU_ROOT=/path/to/wgpu-linux-x86_64-release ...
#
# When CNA_WEBGPU_ROOT is empty and CNA_WEBGPU_AUTO_DOWNLOAD is ON, CMake
# downloads the pinned official release matching the host platform.

include_guard(GLOBAL)

set(CNA_WEBGPU_VERSION "v29.0.1.1" CACHE STRING "Pinned wgpu-native release used by CNA")
set(CNA_WEBGPU_ROOT "" CACHE PATH "Root of an extracted official wgpu-native release")
option(CNA_WEBGPU_AUTO_DOWNLOAD "Download the pinned wgpu-native binary release when CNA_WEBGPU_ROOT is empty" ON)

function(cna_configure_webgpu)
    if(TARGET WebGPU::WebGPU)
        return()
    endif()

    # Web / browser target (WEBGPU-119): the browser owns the WebGPU implementation, reached from
    # WebAssembly through Emscripten's emdawnwebgpu port. There is no wgpu-native library to
    # download, find or copy -- the port supplies the same unified webgpu.h that wgpu-native v29
    # exposes natively, plus the JavaScript bindings that route wgpu* calls to navigator.gpu. The
    # single --use-port flag carries both the include path (at compile time) and those bindings (at
    # link time); the WebAssembly module needs no sibling runtime file, so CNA_WEBGPU_RUNTIME_LIBRARY
    # stays unset and the renderer's runtime-copy step is skipped. Asyncify is already enabled
    # project-wide for Emscripten links (see top-level CMakeLists.txt), which is what lets this
    # renderer's synchronous adapter/device waits yield to the browser event loop.
    if(EMSCRIPTEN)
        add_library(WebGPU::WebGPU INTERFACE IMPORTED GLOBAL)
        set_target_properties(WebGPU::WebGPU PROPERTIES
            INTERFACE_COMPILE_OPTIONS "--use-port=emdawnwebgpu"
            INTERFACE_LINK_OPTIONS "--use-port=emdawnwebgpu")
        message(STATUS "CNA WebGPU: using Emscripten emdawnwebgpu port (browser navigator.gpu)")
        return()
    endif()

    set(_root "${CNA_WEBGPU_ROOT}")

    if(NOT _root)
        if(NOT CNA_WEBGPU_AUTO_DOWNLOAD)
            message(FATAL_ERROR
                "CNA WebGPU: CNA_WEBGPU_ROOT is empty and CNA_WEBGPU_AUTO_DOWNLOAD=OFF. "
                "Extract an official wgpu-native ${CNA_WEBGPU_VERSION} release and set CNA_WEBGPU_ROOT.")
        endif()

        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)
        if(_arch MATCHES "^(x86_64|amd64)$")
            set(_wgpu_arch "x86_64")
        elseif(_arch MATCHES "^(aarch64|arm64)$")
            set(_wgpu_arch "aarch64")
        else()
            message(FATAL_ERROR "CNA WebGPU: unsupported processor '${CMAKE_SYSTEM_PROCESSOR}'. Set CNA_WEBGPU_ROOT manually if an official package exists.")
        endif()

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(_asset "wgpu-linux-${_wgpu_arch}-release.zip")
        elseif(APPLE)
            set(_asset "wgpu-macos-${_wgpu_arch}-release.zip")
        elseif(WIN32)
            if(MINGW)
                if(NOT _wgpu_arch STREQUAL "x86_64")
                    message(FATAL_ERROR "CNA WebGPU: the GNU wgpu-native release is currently configured only for Windows x86_64.")
                endif()
                set(_asset "wgpu-windows-x86_64-gnu-release.zip")
            else()
                set(_asset "wgpu-windows-${_wgpu_arch}-msvc-release.zip")
            endif()
        else()
            message(FATAL_ERROR "CNA WebGPU: automatic wgpu-native download is unsupported on ${CMAKE_SYSTEM_NAME}; set CNA_WEBGPU_ROOT.")
        endif()

        set(_download_dir "${CMAKE_BINARY_DIR}/_deps/wgpu-native-${CNA_WEBGPU_VERSION}")
        set(_archive "${_download_dir}/${_asset}")
        set(_extract_stamp "${_download_dir}/.cna-extracted")
        file(MAKE_DIRECTORY "${_download_dir}")

        if(NOT EXISTS "${_extract_stamp}")
            # WEBGPU-1: refuse to auto-download a package we have no pinned SHA-256 for.
            include("${CMAKE_CURRENT_LIST_DIR}/WebGPUChecksum.cmake")
            cna_webgpu_expected_sha256("${CNA_WEBGPU_VERSION}" "${_asset}" _expected_sha256)
            if(NOT _expected_sha256)
                message(FATAL_ERROR
                    "CNA WebGPU: no pinned SHA-256 for ${_asset} at ${CNA_WEBGPU_VERSION}; refusing to "
                    "auto-download an unverifiable package. Set CNA_WEBGPU_ROOT, or add the hash to "
                    "cmake/WebGPUChecksum.cmake.")
            endif()
            set(_url "https://github.com/gfx-rs/wgpu-native/releases/download/${CNA_WEBGPU_VERSION}/${_asset}")
            message(STATUS "CNA WebGPU: downloading ${_url}")
            # EXPECTED_HASH makes file(DOWNLOAD) verify the transfer itself; the explicit
            # cna_webgpu_verify_sha256 below re-checks before extraction (also covering a pre-placed
            # archive) and fails closed, removing the archive + stamp on mismatch.
            file(DOWNLOAD "${_url}" "${_archive}"
                SHOW_PROGRESS
                STATUS _download_status
                EXPECTED_HASH SHA256=${_expected_sha256}
                TLS_VERIFY ON)
            list(GET _download_status 0 _download_code)
            list(GET _download_status 1 _download_message)
            if(NOT _download_code EQUAL 0)
                file(REMOVE "${_archive}")
                message(FATAL_ERROR
                    "CNA WebGPU: failed to download wgpu-native (${_download_message}). "
                    "Download ${_asset} manually and set CNA_WEBGPU_ROOT to its extracted directory.")
            endif()
            # Verify BEFORE extracting; fails closed (removes archive + stamp) on mismatch.
            cna_webgpu_verify_sha256("${_archive}" "${_expected_sha256}" "${_extract_stamp}")
            file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_download_dir}")
            file(WRITE "${_extract_stamp}" "${CNA_WEBGPU_VERSION}\n${_asset}\n")
        endif()
        set(_root "${_download_dir}")
    endif()

    # Accept both archives that unpack directly into <root>/include + <root>/lib
    # and archives with one top-level directory.
    if(NOT EXISTS "${_root}/include")
        file(GLOB _webgpu_root_children LIST_DIRECTORIES TRUE "${_root}/*")
        foreach(_candidate IN LISTS _webgpu_root_children)
            if(IS_DIRECTORY "${_candidate}" AND EXISTS "${_candidate}/include")
                set(_root "${_candidate}")
                break()
            endif()
        endforeach()
    endif()

    # Official release archives use include/webgpu/webgpu.h and lib/.
    # Older package layouts used include/webgpu-headers/webgpu.h; accept both.
    find_path(_webgpu_include_dir
        NAMES webgpu/webgpu.h webgpu-headers/webgpu.h webgpu.h
        PATHS "${_root}" "${_root}/include"
        PATH_SUFFIXES include
        NO_DEFAULT_PATH)
    if(NOT _webgpu_include_dir)
        message(FATAL_ERROR "CNA WebGPU: webgpu.h was not found below '${_root}'.")
    endif()

    if(WIN32)
        find_library(_webgpu_import_library
            NAMES wgpu_native wgpu_native.dll libwgpu_native
            PATHS "${_root}" "${_root}/lib"
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH)
        find_file(_webgpu_runtime_library
            NAMES wgpu_native.dll
            PATHS "${_root}" "${_root}/lib" "${_root}/bin"
            PATH_SUFFIXES lib bin
            NO_DEFAULT_PATH)
        if(NOT _webgpu_import_library)
            message(FATAL_ERROR "CNA WebGPU: wgpu-native import library was not found below '${_root}'.")
        endif()
        add_library(WebGPU::WebGPU SHARED IMPORTED GLOBAL)
        set_target_properties(WebGPU::WebGPU PROPERTIES
            IMPORTED_IMPLIB "${_webgpu_import_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_webgpu_include_dir}")
        if(_webgpu_runtime_library)
            set_target_properties(WebGPU::WebGPU PROPERTIES IMPORTED_LOCATION "${_webgpu_runtime_library}")
            set(CNA_WEBGPU_RUNTIME_LIBRARY "${_webgpu_runtime_library}" CACHE INTERNAL "wgpu-native runtime library")
        endif()
    else()
        find_library(_webgpu_library
            NAMES wgpu_native libwgpu_native
            PATHS "${_root}" "${_root}/lib"
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH)
        if(NOT _webgpu_library)
            message(FATAL_ERROR "CNA WebGPU: libwgpu_native was not found below '${_root}'.")
        endif()
        add_library(WebGPU::WebGPU SHARED IMPORTED GLOBAL)
        set_target_properties(WebGPU::WebGPU PROPERTIES
            IMPORTED_LOCATION "${_webgpu_library}"
            IMPORTED_NO_SONAME TRUE
            INTERFACE_INCLUDE_DIRECTORIES "${_webgpu_include_dir}")
        set(CNA_WEBGPU_RUNTIME_LIBRARY "${_webgpu_library}" CACHE INTERNAL "wgpu-native runtime library")
    endif()

    set(CNA_WEBGPU_RESOLVED_ROOT "${_root}" CACHE INTERNAL "Resolved wgpu-native root")
    message(STATUS "CNA WebGPU: using wgpu-native ${CNA_WEBGPU_VERSION} from ${_root}")
endfunction()
