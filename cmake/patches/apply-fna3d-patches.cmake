# Idempotently applies CNA's pinned FNA3D and MojoShader patches. The working directory is the
# FNA3D source root. The root patch is checked without restoring the checkout: an offline source
# override may contain owner changes, and CNA must never erase them to make its own patch apply.

if(NOT DEFINED CNA_FNA3D_MOJOSHADER_PATCH_FILE OR
   NOT DEFINED CNA_FNA3D_MOJOSHADER_PATCH_SCRIPT OR
   NOT DEFINED CNA_FNA3D_SOURCE_PATCH_FILE)
    message(FATAL_ERROR
        "CNA: apply-fna3d-patches.cmake requires the MojoShader patch list/script and the "
        "FNA3D source patch path")
endif()

get_filename_component(CNA_FNA3D_PATCH_SOURCE_DIR "." ABSOLUTE)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DCNA_FNA3D_MOJOSHADER_PATCH_FILE=${CNA_FNA3D_MOJOSHADER_PATCH_FILE}"
            -P "${CNA_FNA3D_MOJOSHADER_PATCH_SCRIPT}"
    WORKING_DIRECTORY "${CNA_FNA3D_PATCH_SOURCE_DIR}"
    RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_PATCH_RESULT)
if(NOT CNA_FNA3D_MOJOSHADER_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "CNA: failed to apply the pinned MojoShader patch series")
endif()

if(NOT EXISTS "${CNA_FNA3D_SOURCE_PATCH_FILE}")
    message(FATAL_ERROR "CNA: ${CNA_FNA3D_SOURCE_PATCH_FILE} does not exist")
endif()
find_program(CNA_FNA3D_PATCH_GIT_EXECUTABLE git)
if(NOT CNA_FNA3D_PATCH_GIT_EXECUTABLE)
    message(FATAL_ERROR
        "CNA: git not found -- required to apply ${CNA_FNA3D_SOURCE_PATCH_FILE}")
endif()

execute_process(
    COMMAND "${CNA_FNA3D_PATCH_GIT_EXECUTABLE}" -C "${CNA_FNA3D_PATCH_SOURCE_DIR}"
            apply --reverse --check "${CNA_FNA3D_SOURCE_PATCH_FILE}"
    RESULT_VARIABLE CNA_FNA3D_SOURCE_PATCH_REVERSE_RESULT
    ERROR_QUIET)
if(CNA_FNA3D_SOURCE_PATCH_REVERSE_RESULT EQUAL 0)
    message(STATUS "CNA: FNA3D source patch already applied -- skipping")
    return()
endif()

execute_process(
    COMMAND "${CNA_FNA3D_PATCH_GIT_EXECUTABLE}" -C "${CNA_FNA3D_PATCH_SOURCE_DIR}"
            apply --check "${CNA_FNA3D_SOURCE_PATCH_FILE}"
    RESULT_VARIABLE CNA_FNA3D_SOURCE_PATCH_CHECK_RESULT
    ERROR_VARIABLE CNA_FNA3D_SOURCE_PATCH_CHECK_ERROR)
if(NOT CNA_FNA3D_SOURCE_PATCH_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR
        "CNA: ${CNA_FNA3D_SOURCE_PATCH_FILE} is neither cleanly applied nor applicable to "
        "${CNA_FNA3D_PATCH_SOURCE_DIR}. Preserve any local changes and restore the pinned FNA3D "
        "files before configuring. git said:\n${CNA_FNA3D_SOURCE_PATCH_CHECK_ERROR}")
endif()

execute_process(
    COMMAND "${CNA_FNA3D_PATCH_GIT_EXECUTABLE}" -C "${CNA_FNA3D_PATCH_SOURCE_DIR}"
            apply "${CNA_FNA3D_SOURCE_PATCH_FILE}"
    RESULT_VARIABLE CNA_FNA3D_SOURCE_PATCH_APPLY_RESULT)
if(NOT CNA_FNA3D_SOURCE_PATCH_APPLY_RESULT EQUAL 0)
    message(FATAL_ERROR "CNA: failed to apply ${CNA_FNA3D_SOURCE_PATCH_FILE}")
endif()
message(STATUS "CNA: applied ${CNA_FNA3D_SOURCE_PATCH_FILE}")
