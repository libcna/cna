# WEBGPU-151 integration test for cmake/WebGPUChecksum.cmake's cache logic (cna_webgpu_prepare_cache
# + the stamp/purge helpers that ThirdPartyWebGPU.cmake now uses). It builds a REAL tiny archive so the
# extract path runs, and drives every cache state: old (legacy) stamp, correct new stamp, wrong hash,
# changed version/asset, interrupted extraction, and no archive. No network.
# Params (via -D): MODULE (WebGPUChecksum.cmake), WORKDIR.
include("${MODULE}")
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

set(_pass 0)
set(_total 0)
macro(expect_eq label a b)
    math(EXPR _total "${_total}+1")
    if("${a}" STREQUAL "${b}")
        math(EXPR _pass "${_pass}+1")
        message(STATUS "[PASS] ${label}")
    else()
        message(WARNING "[FAIL] ${label}: '${a}' != '${b}'")
    endif()
endmacro()

set(_version "v1.0.0")
set(_asset "pkg.zip")

# Build a real tiny archive containing lib/dummy.txt, and record its SHA-256.
set(_srcdir "${WORKDIR}/src")
file(MAKE_DIRECTORY "${_srcdir}/lib")
file(WRITE "${_srcdir}/lib/dummy.txt" "wgpu-native cache test payload\n")
set(_realarchive "${WORKDIR}/pkg-good.zip")
# Use `cmake -E tar` with a working directory so the archive stores RELATIVE paths (lib/dummy.txt),
# mirroring a real wgpu-native release zip, so extraction lands at <dir>/lib/... .
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${_realarchive}" --format=zip lib
    WORKING_DIRECTORY "${_srcdir}"
    RESULT_VARIABLE _tar_rc)
if(NOT _tar_rc EQUAL 0)
    message(FATAL_ERROR "cache test: could not create the fixture archive (${_tar_rc})")
endif()
file(SHA256 "${_realarchive}" _goodhash)

# Fresh per-scenario download dir seeded with the real archive as <dir>/<asset>.
function(seed_dir dir)
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    configure_file("${_realarchive}" "${dir}/${_asset}" COPYONLY)
endfunction()

# --- 1. Correct new (3-line) stamp + archive present -> trusted, no download. ---
set(_d "${WORKDIR}/s1")
seed_dir("${_d}")
cna_webgpu_write_stamp("${_d}/.cna-extracted" "${_version}" "${_asset}" "${_goodhash}")
cna_webgpu_prepare_cache("${_d}" "${_version}" "${_asset}" "${_goodhash}" _nd)
expect_eq("correct new stamp -> no download" "${_nd}" "FALSE")

# --- 2. Old (legacy 2-line) stamp + authentic archive -> re-extract + upgrade stamp, no download. ---
set(_d "${WORKDIR}/s2")
seed_dir("${_d}")
file(WRITE "${_d}/.cna-extracted" "${_version}\n${_asset}\n")   # legacy, no hash
cna_webgpu_prepare_cache("${_d}" "${_version}" "${_asset}" "${_goodhash}" _nd)
expect_eq("legacy stamp + authentic archive -> no download" "${_nd}" "FALSE")
set(_ok "no")
if(EXISTS "${_d}/lib/dummy.txt")
    set(_ok "yes")
endif()
expect_eq("legacy stamp -> package re-extracted" "${_ok}" "yes")
cna_webgpu_stamp_trusted("${_d}/.cna-extracted" "${_version}" "${_asset}" "${_goodhash}" _t)
expect_eq("legacy stamp -> upgraded to trusted hashed stamp" "${_t}" "TRUE")

# --- 3. Wrong expected hash (tampered cache) -> purge archive/stamp/extracted, download needed. ---
set(_d "${WORKDIR}/s3")
seed_dir("${_d}")
file(MAKE_DIRECTORY "${_d}/lib")
file(WRITE "${_d}/lib/dummy.txt" "stale extracted\n")
cna_webgpu_write_stamp("${_d}/.cna-extracted" "${_version}" "${_asset}" "deadbeef")
set(_wrong "0000000000000000000000000000000000000000000000000000000000000000")
cna_webgpu_prepare_cache("${_d}" "${_version}" "${_asset}" "${_wrong}" _nd)
expect_eq("wrong hash -> download needed" "${_nd}" "TRUE")
set(_gone "removed")
if(EXISTS "${_d}/${_asset}")
    set(_gone "present")
endif()
expect_eq("wrong hash -> tampered archive removed" "${_gone}" "removed")
set(_libgone "removed")
if(EXISTS "${_d}/lib/dummy.txt")
    set(_libgone "present")
endif()
expect_eq("wrong hash -> stale extracted package removed" "${_libgone}" "removed")

# --- 4. Changed version/asset (stamp names the old ones) + authentic archive -> re-validate. ---
set(_d "${WORKDIR}/s4")
seed_dir("${_d}")
cna_webgpu_write_stamp("${_d}/.cna-extracted" "v0.9.0" "old-asset.zip" "${_goodhash}")
cna_webgpu_prepare_cache("${_d}" "${_version}" "${_asset}" "${_goodhash}" _nd)
expect_eq("changed version/asset + authentic archive -> no download" "${_nd}" "FALSE")
cna_webgpu_stamp_trusted("${_d}/.cna-extracted" "${_version}" "${_asset}" "${_goodhash}" _t)
expect_eq("changed version/asset -> stamp rewritten to current" "${_t}" "TRUE")

# --- 5. Interrupted extraction: archive present, NO stamp -> extract + write stamp, no download. ---
set(_d "${WORKDIR}/s5")
seed_dir("${_d}")   # no stamp, no lib
cna_webgpu_prepare_cache("${_d}" "${_version}" "${_asset}" "${_goodhash}" _nd)
expect_eq("interrupted extraction -> no download" "${_nd}" "FALSE")
set(_ok "no")
if(EXISTS "${_d}/lib/dummy.txt" AND EXISTS "${_d}/.cna-extracted")
    set(_ok "yes")
endif()
expect_eq("interrupted extraction -> extracted + stamped" "${_ok}" "yes")

# --- 6. No archive at all -> download needed. ---
set(_d "${WORKDIR}/s6")
file(REMOVE_RECURSE "${_d}")
file(MAKE_DIRECTORY "${_d}")
cna_webgpu_prepare_cache("${_d}" "${_version}" "${_asset}" "${_goodhash}" _nd)
expect_eq("no cached archive -> download needed" "${_nd}" "TRUE")

message(STATUS "=== WEBGPU-151 cache test: ${_pass}/${_total} PASS ===")
if(NOT _pass EQUAL _total)
    message(FATAL_ERROR "WEBGPU-151 cache test failed (${_pass}/${_total})")
endif()
