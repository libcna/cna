# WEBGPU-1: SHA-256 integrity verification for the pinned wgpu-native binary release assets.
#
# The auto-download path (ThirdPartyWebGPU.cmake) verifies every package against a pinned hash BEFORE
# extracting it, and fails closed (removing the invalid archive and extract stamp) on any mismatch, so
# a corrupted download or a tampered CDN response can never be extracted or linked. The offline
# `CNA_WEBGPU_ROOT` path is unaffected -- an already-extracted tree the integrator vouches for is not
# re-hashed.
#
# Hashes below are the SHA-256 of the official GitHub release assets at
# https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1/<asset>, computed with `sha256sum`.
# If CNA_WEBGPU_VERSION changes, regenerate all of them (a stale hash must fail closed, never silently
# pass).

# Returns, in out_var, the pinned SHA-256 for `asset` at wgpu-native `version`, or "" if unknown.
function(cna_webgpu_expected_sha256 version asset out_var)
    set(_hash "")
    if(version STREQUAL "v29.0.1.1")
        if(asset STREQUAL "wgpu-linux-x86_64-release.zip")
            set(_hash "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a")
        elseif(asset STREQUAL "wgpu-linux-aarch64-release.zip")
            set(_hash "015fcdf1dbae82e614a783cc38017e5399ae0927a889fe9b69c9b664bc61b47a")
        elseif(asset STREQUAL "wgpu-macos-x86_64-release.zip")
            set(_hash "8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41")
        elseif(asset STREQUAL "wgpu-macos-aarch64-release.zip")
            set(_hash "a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e")
        elseif(asset STREQUAL "wgpu-windows-x86_64-msvc-release.zip")
            set(_hash "7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132")
        elseif(asset STREQUAL "wgpu-windows-aarch64-msvc-release.zip")
            set(_hash "4a876421a8c1e5fe72f849b3722214280fe485cb1c56f77f8b0c82414be5b29f")
        elseif(asset STREQUAL "wgpu-windows-x86_64-gnu-release.zip")
            set(_hash "d471e3614733c1d4ddd61bfd19868356477d0d37bf531bf8c6cb64a7f579bd2a")
        endif()
    endif()
    set(${out_var} "${_hash}" PARENT_SCOPE)
endfunction()

# Verifies that `archive` has SHA-256 exactly `expected`. On any mismatch (or an empty expected hash),
# removes `archive` and every extra path in ARGN (e.g. the extract stamp) and aborts with FATAL_ERROR,
# so nothing downstream can extract or link an unverified package.
function(cna_webgpu_verify_sha256 archive expected)
    if(NOT expected)
        file(REMOVE "${archive}" ${ARGN})
        message(FATAL_ERROR
            "CNA WebGPU: no pinned SHA-256 is known for '${archive}' at this CNA_WEBGPU_VERSION -- "
            "refusing to trust an unverifiable package. Set CNA_WEBGPU_ROOT to a package you vouch "
            "for, or add its hash to cmake/WebGPUChecksum.cmake.")
    endif()
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "CNA WebGPU: cannot verify '${archive}' -- file does not exist.")
    endif()
    file(SHA256 "${archive}" _actual)
    string(TOLOWER "${expected}" _expected_lc)
    string(TOLOWER "${_actual}" _actual_lc)
    if(NOT _actual_lc STREQUAL _expected_lc)
        file(REMOVE "${archive}" ${ARGN})
        message(FATAL_ERROR
            "CNA WebGPU: SHA-256 mismatch for '${archive}'.\n"
            "  expected ${_expected_lc}\n"
            "  actual   ${_actual_lc}\n"
            "The invalid archive was removed. Re-run to re-download, or set CNA_WEBGPU_ROOT.")
    endif()
    message(STATUS "CNA WebGPU: verified SHA-256 of ${archive}")
endfunction()
