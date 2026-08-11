# Idempotently applies shivavg-context-dtor-leak.patch to the FetchContent'd ShivaVG source tree.
# Invoked as this repo's OPENVG PATCH_COMMAND (see cmake/ThirdPartyOpenVG.cmake) with its working
# directory already set to the ShivaVG source dir by FetchContent/ExternalProject's own step
# machinery -- every path below is deliberately relative, not absolute, so this script does not
# need (and must not be passed) the source directory itself.
#
# FetchContent re-runs PATCH_COMMAND on every reconfigure of an already-populated source tree; a
# plain `git apply` would fail the second time ("patch does not apply", since the target lines no
# longer match). Checking with `git apply --reverse --check` first -- which succeeds only when the
# patch's changes are already present -- makes this reproducible: apply on a pristine checkout,
# skip on an already-patched one, hard error on anything else (e.g. the pin changed underneath the
# patch).
#
# Required variable: CNA_SHIVAVG_PATCH_FILE (absolute path to the .patch file to apply).

if(NOT DEFINED CNA_SHIVAVG_PATCH_FILE)
    message(FATAL_ERROR "CNA: apply-shivavg-patch.cmake requires -DCNA_SHIVAVG_PATCH_FILE=<path>")
endif()

find_program(CNA_SHIVAVG_PATCH_GIT_EXECUTABLE git)
if(NOT CNA_SHIVAVG_PATCH_GIT_EXECUTABLE)
    message(FATAL_ERROR "CNA: git not found -- required to apply ${CNA_SHIVAVG_PATCH_FILE}")
endif()

execute_process(
    COMMAND "${CNA_SHIVAVG_PATCH_GIT_EXECUTABLE}" apply --reverse --check "${CNA_SHIVAVG_PATCH_FILE}"
    RESULT_VARIABLE CNA_SHIVAVG_ALREADY_PATCHED
    OUTPUT_QUIET
    ERROR_QUIET
)
if(CNA_SHIVAVG_ALREADY_PATCHED EQUAL 0)
    message(STATUS "CNA: ShivaVG context-dtor-leak patch already applied -- skipping")
    return()
endif()

execute_process(
    COMMAND "${CNA_SHIVAVG_PATCH_GIT_EXECUTABLE}" apply "${CNA_SHIVAVG_PATCH_FILE}"
    RESULT_VARIABLE CNA_SHIVAVG_PATCH_APPLY_RESULT
)
if(NOT CNA_SHIVAVG_PATCH_APPLY_RESULT EQUAL 0)
    message(FATAL_ERROR
        "CNA: failed to apply ${CNA_SHIVAVG_PATCH_FILE} to the fetched ShivaVG source -- the "
        "pinned CNA_OPENVG_GIT_TAG revision may no longer match the patch's expected context.")
endif()

message(STATUS "CNA: applied ShivaVG context-dtor-leak patch")
