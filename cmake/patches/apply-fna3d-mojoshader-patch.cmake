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
#
# The series is hashed in memory. The FNA3D checkout is usually a shared one under ~/deps, and an
# already-patched tree must not be written at all -- a read-only one then configures fine (House
# Simulator's BL-17 hit the write of a combined patch file into it).
set(CNA_FNA3D_MOJOSHADER_COMBINED_TEXT "")
foreach(CNA_FNA3D_MOJOSHADER_PATCH IN LISTS CNA_FNA3D_MOJOSHADER_PATCH_FILE)
    if(NOT EXISTS "${CNA_FNA3D_MOJOSHADER_PATCH}")
        message(FATAL_ERROR "CNA: ${CNA_FNA3D_MOJOSHADER_PATCH} does not exist")
    endif()
    file(READ "${CNA_FNA3D_MOJOSHADER_PATCH}" CNA_FNA3D_MOJOSHADER_PATCH_TEXT)
    string(APPEND CNA_FNA3D_MOJOSHADER_COMBINED_TEXT "${CNA_FNA3D_MOJOSHADER_PATCH_TEXT}")
endforeach()

# The patch series overlaps itself, so neither a forward nor a reverse check of the concatenated
# diff can identify its final state. Keep the hash of both the patch inputs and the resulting
# tracked-file diff. FetchContent can restore the submodule while leaving an untracked stamp.
string(SHA256 CNA_FNA3D_MOJOSHADER_SERIES_HASH "${CNA_FNA3D_MOJOSHADER_COMBINED_TEXT}")
set(CNA_FNA3D_MOJOSHADER_STAMP
    "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}/.cna-mojoshader-patch-series.sha256")

execute_process(
    COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" diff --binary HEAD
    RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_DIFF_RESULT
    OUTPUT_VARIABLE CNA_FNA3D_MOJOSHADER_CURRENT_DIFF
)
if(NOT CNA_FNA3D_MOJOSHADER_DIFF_RESULT EQUAL 0)
    message(FATAL_ERROR "CNA: could not inspect MojoShader's tracked-file diff")
endif()
string(SHA256 CNA_FNA3D_MOJOSHADER_CURRENT_DIFF_HASH
       "${CNA_FNA3D_MOJOSHADER_CURRENT_DIFF}")
if(EXISTS "${CNA_FNA3D_MOJOSHADER_STAMP}" AND
   NOT CNA_FNA3D_MOJOSHADER_CURRENT_DIFF STREQUAL "")
    file(READ "${CNA_FNA3D_MOJOSHADER_STAMP}" CNA_FNA3D_MOJOSHADER_STAMPED_HASH)
    string(STRIP "${CNA_FNA3D_MOJOSHADER_STAMPED_HASH}" CNA_FNA3D_MOJOSHADER_STAMPED_HASH)
    if(CNA_FNA3D_MOJOSHADER_STAMPED_HASH STREQUAL
       "${CNA_FNA3D_MOJOSHADER_SERIES_HASH}:${CNA_FNA3D_MOJOSHADER_CURRENT_DIFF_HASH}")
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

# Later patches modify lines introduced by earlier ones, so apply them in order. `git apply`
# checks every hunk in one patch against the tree before changing it; concatenating the series
# into one patch makes those overlapping hunks fail even on the pinned pristine source.
foreach(CNA_FNA3D_MOJOSHADER_PATCH IN LISTS CNA_FNA3D_MOJOSHADER_PATCH_FILE)
    execute_process(
        COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
                "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" apply "${CNA_FNA3D_MOJOSHADER_PATCH}"
        RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_PATCH_APPLY_RESULT
    )
    if(NOT CNA_FNA3D_MOJOSHADER_PATCH_APPLY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "CNA: failed to apply ${CNA_FNA3D_MOJOSHADER_PATCH} to FNA3D's MojoShader "
            "submodule -- the pinned revisions may no longer match the patches.")
    endif()
endforeach()

execute_process(
    COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" diff --binary HEAD
    RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_DIFF_RESULT
    OUTPUT_VARIABLE CNA_FNA3D_MOJOSHADER_CURRENT_DIFF
)
if(NOT CNA_FNA3D_MOJOSHADER_DIFF_RESULT EQUAL 0 OR
   CNA_FNA3D_MOJOSHADER_CURRENT_DIFF STREQUAL "")
    message(FATAL_ERROR "CNA: MojoShader patch series left no tracked-file changes")
endif()
string(SHA256 CNA_FNA3D_MOJOSHADER_CURRENT_DIFF_HASH
       "${CNA_FNA3D_MOJOSHADER_CURRENT_DIFF}")
file(WRITE "${CNA_FNA3D_MOJOSHADER_STAMP}"
     "${CNA_FNA3D_MOJOSHADER_SERIES_HASH}:${CNA_FNA3D_MOJOSHADER_CURRENT_DIFF_HASH}\n")

list(LENGTH CNA_FNA3D_MOJOSHADER_PATCH_FILE CNA_FNA3D_MOJOSHADER_PATCH_COUNT)
message(STATUS
    "CNA: applied the MojoShader patch series (${CNA_FNA3D_MOJOSHADER_PATCH_COUNT} patches)")
