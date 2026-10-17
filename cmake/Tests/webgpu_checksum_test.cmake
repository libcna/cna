# WEBGPU-1 local test: exercises cmake/WebGPUChecksum.cmake with a good archive, a corrupt archive and
# an unknown asset, asserting a hash mismatch FAILS CLOSED (aborts and removes the archive + stamp).
# Params (via -D): MODULE (WebGPUChecksum.cmake), CHILD (webgpu_checksum_verify_once.cmake), WORKDIR.
include("${MODULE}")
file(MAKE_DIRECTORY "${WORKDIR}")

# 1. Pinned-hash table returns the known value for a supported asset, and "" for unknowns.
cna_webgpu_expected_sha256("v29.0.1.1" "wgpu-linux-x86_64-release.zip" _h)
if(NOT _h STREQUAL "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a")
    message(FATAL_ERROR "checksum-test: pinned linux-x86_64 hash wrong: '${_h}'")
endif()
cna_webgpu_expected_sha256("v29.0.1.1" "no-such-asset.zip" _none)
if(_none)
    message(FATAL_ERROR "checksum-test: an unknown asset must map to empty, got '${_none}'")
endif()
cna_webgpu_expected_sha256("v0.0.0-bogus" "wgpu-linux-x86_64-release.zip" _badver)
if(_badver)
    message(FATAL_ERROR "checksum-test: an unknown version must map to empty, got '${_badver}'")
endif()

# 2. A matching archive verifies without aborting and is kept.
set(_good "${WORKDIR}/good.bin")
file(WRITE "${_good}" "CNA WebGPU checksum test payload\n")
file(SHA256 "${_good}" _goodhash)
cna_webgpu_verify_sha256("${_good}" "${_goodhash}")
if(NOT EXISTS "${_good}")
    message(FATAL_ERROR "checksum-test: a matching archive must be kept")
endif()

# 3. A corrupt archive fails closed and both the archive and the extract stamp are removed.
set(_bad "${WORKDIR}/bad.bin")
set(_stamp "${WORKDIR}/bad.stamp")
file(WRITE "${_bad}" "tampered payload\n")
file(WRITE "${_stamp}" "stamp\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DMODULE=${MODULE}" "-DFILE=${_bad}" "-DSTAMP=${_stamp}"
            "-DHASH=0000000000000000000000000000000000000000000000000000000000000000"
            -P "${CHILD}"
    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
if(_rc EQUAL 0)
    message(FATAL_ERROR "checksum-test: a hash mismatch must fail closed (child exited 0)")
endif()
if(EXISTS "${_bad}")
    message(FATAL_ERROR "checksum-test: a mismatched archive must be removed")
endif()
if(EXISTS "${_stamp}")
    message(FATAL_ERROR "checksum-test: the extract stamp must be removed on mismatch")
endif()

# 4. An empty (unknown) expected hash also fails closed rather than trusting the file.
set(_unk "${WORKDIR}/unk.bin")
file(WRITE "${_unk}" "x\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DMODULE=${MODULE}" "-DFILE=${_unk}" "-DSTAMP=" "-DHASH=" -P "${CHILD}"
    RESULT_VARIABLE _rc2 OUTPUT_QUIET ERROR_QUIET)
if(_rc2 EQUAL 0)
    message(FATAL_ERROR "checksum-test: an empty expected hash must fail closed")
endif()

message(STATUS "checksum-test: all checks passed")
