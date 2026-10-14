# Idempotently applies CNA's pinned FNA3D and MojoShader patches. The working directory is the
# FNA3D source root. The root patches are checked without restoring the checkout: an offline source
# override may contain owner changes, and CNA must never erase them to make its own patches apply.

if(NOT DEFINED CNA_FNA3D_MOJOSHADER_PATCH_FILE OR
   NOT DEFINED CNA_FNA3D_MOJOSHADER_PATCH_SCRIPT OR
   NOT DEFINED CNA_FNA3D_SOURCE_PATCH_FILE)
    message(FATAL_ERROR
        "CNA: apply-fna3d-patches.cmake requires the MojoShader patch list/script and the "
        "FNA3D source patch series")
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

find_program(CNA_FNA3D_PATCH_GIT_EXECUTABLE git)
if(NOT CNA_FNA3D_PATCH_GIT_EXECUTABLE)
    message(FATAL_ERROR "CNA: git not found -- required to apply the FNA3D source patches")
endif()

# CNA_FNA3D_SOURCE_PATCH_FILE is an ordered series, each patch made on top of the previous ones.
# A later patch may edit an earlier one's context lines, so an earlier patch cannot be checked on
# its own once a later one is applied. The newest patch whose reverse applies marks how far the
# series got (a tree configured before a patch was added stops at an earlier one); only the
# patches after it are applied.
set(CNA_FNA3D_SOURCE_PATCHES ${CNA_FNA3D_SOURCE_PATCH_FILE})
list(LENGTH CNA_FNA3D_SOURCE_PATCHES CNA_FNA3D_SOURCE_PATCH_COUNT)
foreach(CNA_FNA3D_SOURCE_PATCH IN LISTS CNA_FNA3D_SOURCE_PATCHES)
    if(NOT EXISTS "${CNA_FNA3D_SOURCE_PATCH}")
        message(FATAL_ERROR "CNA: ${CNA_FNA3D_SOURCE_PATCH} does not exist")
    endif()
endforeach()

set(CNA_FNA3D_SOURCE_PATCH_APPLIED 0)
set(CNA_FNA3D_SOURCE_PATCH_INDEX ${CNA_FNA3D_SOURCE_PATCH_COUNT})
while(CNA_FNA3D_SOURCE_PATCH_INDEX GREATER 0)
    math(EXPR CNA_FNA3D_SOURCE_PATCH_LAST "${CNA_FNA3D_SOURCE_PATCH_INDEX} - 1")
    list(GET CNA_FNA3D_SOURCE_PATCHES ${CNA_FNA3D_SOURCE_PATCH_LAST} CNA_FNA3D_SOURCE_PATCH)
    execute_process(
        COMMAND "${CNA_FNA3D_PATCH_GIT_EXECUTABLE}" -C "${CNA_FNA3D_PATCH_SOURCE_DIR}"
                apply --reverse --check "${CNA_FNA3D_SOURCE_PATCH}"
        RESULT_VARIABLE CNA_FNA3D_SOURCE_PATCH_REVERSE_RESULT
        ERROR_QUIET)
    if(CNA_FNA3D_SOURCE_PATCH_REVERSE_RESULT EQUAL 0)
        set(CNA_FNA3D_SOURCE_PATCH_APPLIED ${CNA_FNA3D_SOURCE_PATCH_INDEX})
        break()
    endif()
    set(CNA_FNA3D_SOURCE_PATCH_INDEX ${CNA_FNA3D_SOURCE_PATCH_LAST})
endwhile()

if(CNA_FNA3D_SOURCE_PATCH_APPLIED EQUAL CNA_FNA3D_SOURCE_PATCH_COUNT)
    message(STATUS "CNA: FNA3D source patches already applied -- skipping")
    return()
endif()

list(SUBLIST CNA_FNA3D_SOURCE_PATCHES ${CNA_FNA3D_SOURCE_PATCH_APPLIED} -1
     CNA_FNA3D_SOURCE_PATCHES_TO_APPLY)
foreach(CNA_FNA3D_SOURCE_PATCH IN LISTS CNA_FNA3D_SOURCE_PATCHES_TO_APPLY)
    execute_process(
        COMMAND "${CNA_FNA3D_PATCH_GIT_EXECUTABLE}" -C "${CNA_FNA3D_PATCH_SOURCE_DIR}"
                apply --check "${CNA_FNA3D_SOURCE_PATCH}"
        RESULT_VARIABLE CNA_FNA3D_SOURCE_PATCH_CHECK_RESULT
        ERROR_VARIABLE CNA_FNA3D_SOURCE_PATCH_CHECK_ERROR)
    if(NOT CNA_FNA3D_SOURCE_PATCH_CHECK_RESULT EQUAL 0)
        message(FATAL_ERROR
            "CNA: ${CNA_FNA3D_SOURCE_PATCH} is neither cleanly applied nor applicable to "
            "${CNA_FNA3D_PATCH_SOURCE_DIR}. Preserve any local changes and restore the pinned "
            "FNA3D files before configuring. git said:\n${CNA_FNA3D_SOURCE_PATCH_CHECK_ERROR}")
    endif()
    execute_process(
        COMMAND "${CNA_FNA3D_PATCH_GIT_EXECUTABLE}" -C "${CNA_FNA3D_PATCH_SOURCE_DIR}"
                apply "${CNA_FNA3D_SOURCE_PATCH}"
        RESULT_VARIABLE CNA_FNA3D_SOURCE_PATCH_APPLY_RESULT)
    if(NOT CNA_FNA3D_SOURCE_PATCH_APPLY_RESULT EQUAL 0)
        message(FATAL_ERROR "CNA: failed to apply ${CNA_FNA3D_SOURCE_PATCH}")
    endif()
    message(STATUS "CNA: applied ${CNA_FNA3D_SOURCE_PATCH}")
endforeach()
