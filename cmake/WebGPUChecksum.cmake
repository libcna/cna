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

# WEBGPU-151: the extract stamp is now VERSIONED + HASHED -- three lines: version, asset, expected
# SHA-256. A stamp is trusted only if all three match, so an old two-line stamp (written before the
# checksum work, or by a different version/asset) forces a re-validation instead of being blindly
# trusted. Writes the stamp in that format.
function(cna_webgpu_write_stamp stamp version asset expected)
    file(WRITE "${stamp}" "${version}\n${asset}\n${expected}\n")
endfunction()

# Sets out TRUE iff `stamp` exists and its three lines are exactly (version, asset, expected).
function(cna_webgpu_stamp_trusted stamp version asset expected out)
    set(${out} FALSE PARENT_SCOPE)
    if(NOT EXISTS "${stamp}")
        return()
    endif()
    file(STRINGS "${stamp}" _lines)
    list(LENGTH _lines _n)
    if(_n LESS 3)
        return()  # legacy / truncated stamp with no pinned hash -> never trusted
    endif()
    list(GET _lines 0 _v)
    list(GET _lines 1 _a)
    list(GET _lines 2 _h)
    string(TOLOWER "${_h}" _hl)
    string(TOLOWER "${expected}" _el)
    if(_v STREQUAL version AND _a STREQUAL asset AND _hl STREQUAL _el)
        set(${out} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Removes exactly THIS package's cached archive, extract stamp and extracted output (lib/ + include/).
# Targeted on purpose -- never a wide/recursive delete of _deps or any shared directory.
function(cna_webgpu_purge_cache download_dir asset)
    file(REMOVE "${download_dir}/${asset}" "${download_dir}/.cna-extracted")
    file(REMOVE_RECURSE "${download_dir}/lib" "${download_dir}/include")
endfunction()

# WEBGPU-151: decide (and repair) the cache state for one package WITHOUT downloading. Sets
# out_needs_download:
#   - trusted stamp (version+asset+hash) with the archive still present  -> FALSE (nothing to do).
#   - authentic cached archive (hash matches) but a missing/legacy/mismatched stamp
#       (covers a migration from an old stamp AND an interrupted extraction) -> re-extract + write the
#       versioned/hashed stamp, FALSE (still no download).
#   - a corrupt/tampered cached archive OR a changed asset/version whose archive no longer matches
#       -> purge this package's archive/stamp/extracted output and set TRUE.
#   - no cached archive -> TRUE.
function(cna_webgpu_prepare_cache download_dir version asset expected out_needs_download)
    set(_stamp "${download_dir}/.cna-extracted")
    set(_archive "${download_dir}/${asset}")

    cna_webgpu_stamp_trusted("${_stamp}" "${version}" "${asset}" "${expected}" _trusted)
    if(_trusted AND EXISTS "${_archive}")
        set(${out_needs_download} FALSE PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${_archive}")
        file(SHA256 "${_archive}" _actual)
        string(TOLOWER "${_actual}" _al)
        string(TOLOWER "${expected}" _el)
        if(_al STREQUAL _el)
            # Authentic archive -- re-extract (idempotent) and upgrade/refresh the stamp.
            file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${download_dir}")
            cna_webgpu_write_stamp("${_stamp}" "${version}" "${asset}" "${expected}")
            message(STATUS "CNA WebGPU: re-validated cached ${asset} against its pinned SHA-256")
            set(${out_needs_download} FALSE PARENT_SCOPE)
            return()
        endif()
        message(STATUS "CNA WebGPU: cached ${asset} failed its pinned SHA-256 -- purging and re-downloading")
        cna_webgpu_purge_cache("${download_dir}" "${asset}")
    endif()

    set(${out_needs_download} TRUE PARENT_SCOPE)
endfunction()
