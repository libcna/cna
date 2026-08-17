# Idempotently applies bgfx-max-render-target-msaa.patch to the bgfx submodule
# of the FetchContent'd bgfx.cmake source tree. FetchContent can rerun its
# PATCH_COMMAND after a normal CMake reconfigure, so a plain `git apply` would
# duplicate this zero-context patch instead of rejecting it.

if(NOT DEFINED CNA_BGFX_PATCH_FILE)
    message(FATAL_ERROR
        "CNA: apply-bgfx-max-render-target-msaa-patch.cmake requires "
        "-DCNA_BGFX_PATCH_FILE=<path>")
endif()

find_program(CNA_BGFX_PATCH_GIT_EXECUTABLE git)
if(NOT CNA_BGFX_PATCH_GIT_EXECUTABLE)
    message(FATAL_ERROR "CNA: git not found -- required to apply ${CNA_BGFX_PATCH_FILE}")
endif()

execute_process(
    COMMAND "${CNA_BGFX_PATCH_GIT_EXECUTABLE}" -C bgfx apply --unidiff-zero
            --whitespace=nowarn --reverse --check
            "${CNA_BGFX_PATCH_FILE}"
    RESULT_VARIABLE CNA_BGFX_ALREADY_PATCHED
    OUTPUT_QUIET
    ERROR_QUIET
)
if(CNA_BGFX_ALREADY_PATCHED EQUAL 0)
    message(STATUS "CNA: Bgfx max-render-target-MSAA patch already applied -- skipping")
    return()
endif()

execute_process(
    COMMAND "${CNA_BGFX_PATCH_GIT_EXECUTABLE}" -C bgfx apply --unidiff-zero
            --whitespace=nowarn "${CNA_BGFX_PATCH_FILE}"
    RESULT_VARIABLE CNA_BGFX_PATCH_APPLY_RESULT
)
if(NOT CNA_BGFX_PATCH_APPLY_RESULT EQUAL 0)
    message(FATAL_ERROR
        "CNA: failed to apply ${CNA_BGFX_PATCH_FILE} to the fetched Bgfx source -- the "
        "pinned bgfx.cmake revision may no longer match the patch's expected context.")
endif()

message(STATUS "CNA: applied Bgfx max-render-target-MSAA patch")
