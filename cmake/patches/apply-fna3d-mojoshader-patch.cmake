# Idempotently applies CNA's pinned MojoShader patches inside the FetchContent'd FNA3D source
# tree. The working directory is the FNA3D source root, and MojoShader is FNA3D's initialized git
# submodule. CNA_FNA3D_MOJOSHADER_PATCH_FILE is a CMake list, so several independent patches can
# be carried without merging unrelated fixes into one file.

if(NOT DEFINED CNA_FNA3D_MOJOSHADER_PATCH_FILE)
    message(FATAL_ERROR
        "CNA: apply-fna3d-mojoshader-patch.cmake requires "
        "-DCNA_FNA3D_MOJOSHADER_PATCH_FILE=<path>")
endif()

find_program(CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE git)
if(NOT CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE)
    message(FATAL_ERROR
        "CNA: git not found -- required to apply ${CNA_FNA3D_MOJOSHADER_PATCH_FILE}")
endif()

get_filename_component(CNA_FNA3D_PATCH_SOURCE_DIR "." ABSOLUTE)
set(CNA_FNA3D_MOJOSHADER_SOURCE_DIR
    "${CNA_FNA3D_PATCH_SOURCE_DIR}/MojoShader")
if(NOT EXISTS "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}/mojoshader_effects.c")
    message(FATAL_ERROR
        "CNA: ${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}/mojoshader_effects.c is missing -- the FNA3D "
        "MojoShader submodule must be initialized before applying the parser patch.")
endif()

# The series is judged as a WHOLE, not one patch at a time. Two patches may legitimately edit the
# same lines -- the ILP32 float-literal fix rewrites statements the effect-parser hardening
# introduced -- and once they do, the later one's post-image makes the earlier one's
# `--reverse --check` fail even though both are applied. The per-patch loop this replaced then
# tried to apply an already-applied patch, failed, and stopped the configure with a message
# blaming the pinned revisions. What is asked instead is the question that actually matters: is
# this tree the series' final state?
set(CNA_FNA3D_MOJOSHADER_COMBINED
    "${CMAKE_CURRENT_BINARY_DIR}/cna-mojoshader-patch-series.patch")
set(CNA_FNA3D_MOJOSHADER_COMBINED_TEXT "")
foreach(CNA_FNA3D_MOJOSHADER_PATCH IN LISTS CNA_FNA3D_MOJOSHADER_PATCH_FILE)
    if(NOT EXISTS "${CNA_FNA3D_MOJOSHADER_PATCH}")
        message(FATAL_ERROR "CNA: ${CNA_FNA3D_MOJOSHADER_PATCH} does not exist")
    endif()
    file(READ "${CNA_FNA3D_MOJOSHADER_PATCH}" CNA_FNA3D_MOJOSHADER_PATCH_TEXT)
    string(APPEND CNA_FNA3D_MOJOSHADER_COMBINED_TEXT "${CNA_FNA3D_MOJOSHADER_PATCH_TEXT}")
endforeach()
file(WRITE "${CNA_FNA3D_MOJOSHADER_COMBINED}" "${CNA_FNA3D_MOJOSHADER_COMBINED_TEXT}")

# "Already applied?" needs two answers, because neither alone is sound. Reverse-checking the series
# does not work at all once two patches edit the same lines -- patch A's post-image is patch B's
# pre-image, so no single file state satisfies both halves of the combined diff. Instead:
#
#   * the series must not apply FORWARD. On a pristine tree it would; on a fully patched one every
#     hunk's context is gone, so `--check` fails. This reads the files themselves, which is what
#     makes it survive FetchContent's update step restoring the checkout underneath us.
#   * the stamp must name this exact series. That is what separates "fully applied" from
#     "half-applied by an older revision of the list", which also fails to apply forward.
file(SHA256 "${CNA_FNA3D_MOJOSHADER_COMBINED}" CNA_FNA3D_MOJOSHADER_SERIES_HASH)
set(CNA_FNA3D_MOJOSHADER_STAMP
    "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}/.cna-mojoshader-patch-series.sha256")

execute_process(
    COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" apply --check
            "${CNA_FNA3D_MOJOSHADER_COMBINED}"
    RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_SERIES_STILL_APPLIES
    OUTPUT_QUIET
    ERROR_QUIET
)
if(EXISTS "${CNA_FNA3D_MOJOSHADER_STAMP}" AND NOT CNA_FNA3D_MOJOSHADER_SERIES_STILL_APPLIES EQUAL 0)
    file(READ "${CNA_FNA3D_MOJOSHADER_STAMP}" CNA_FNA3D_MOJOSHADER_STAMPED_HASH)
    string(STRIP "${CNA_FNA3D_MOJOSHADER_STAMPED_HASH}" CNA_FNA3D_MOJOSHADER_STAMPED_HASH)
    if(CNA_FNA3D_MOJOSHADER_STAMPED_HASH STREQUAL CNA_FNA3D_MOJOSHADER_SERIES_HASH)
        message(STATUS "CNA: MojoShader patch series already applied -- skipping")
        return()
    endif()
endif()

# Not the final state. It may be pristine, or half-patched by an earlier revision of this list --
# the tracked files' only intended contents are this series, so restoring them and applying the
# whole series in order is both correct and the one thing that is always safe to repeat.
execute_process(
    COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" checkout -- .
    RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_RESTORE_RESULT
)
if(NOT CNA_FNA3D_MOJOSHADER_RESTORE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "CNA: could not restore ${CNA_FNA3D_MOJOSHADER_SOURCE_DIR} before applying the patch "
        "series.")
endif()

execute_process(
    COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" apply "${CNA_FNA3D_MOJOSHADER_COMBINED}"
    RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_PATCH_APPLY_RESULT
)
if(NOT CNA_FNA3D_MOJOSHADER_PATCH_APPLY_RESULT EQUAL 0)
    message(FATAL_ERROR
        "CNA: failed to apply the MojoShader patch series to FNA3D's MojoShader submodule -- the "
        "pinned FNA3D/MojoShader revisions may no longer match the patches.")
endif()

file(WRITE "${CNA_FNA3D_MOJOSHADER_STAMP}" "${CNA_FNA3D_MOJOSHADER_SERIES_HASH}\n")

list(LENGTH CNA_FNA3D_MOJOSHADER_PATCH_FILE CNA_FNA3D_MOJOSHADER_PATCH_COUNT)
message(STATUS
    "CNA: applied the MojoShader patch series (${CNA_FNA3D_MOJOSHADER_PATCH_COUNT} patches)")
