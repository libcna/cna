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

foreach(CNA_FNA3D_MOJOSHADER_PATCH IN LISTS CNA_FNA3D_MOJOSHADER_PATCH_FILE)
    get_filename_component(CNA_FNA3D_MOJOSHADER_PATCH_NAME
                           "${CNA_FNA3D_MOJOSHADER_PATCH}" NAME)

    execute_process(
        COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
                "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" apply --reverse --check
                "${CNA_FNA3D_MOJOSHADER_PATCH}"
        RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_ALREADY_PATCHED
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(CNA_FNA3D_MOJOSHADER_ALREADY_PATCHED EQUAL 0)
        message(STATUS
            "CNA: ${CNA_FNA3D_MOJOSHADER_PATCH_NAME} already applied -- skipping")
        continue()
    endif()

    execute_process(
        COMMAND "${CNA_FNA3D_MOJOSHADER_PATCH_GIT_EXECUTABLE}" -C
                "${CNA_FNA3D_MOJOSHADER_SOURCE_DIR}" apply
                "${CNA_FNA3D_MOJOSHADER_PATCH}"
        RESULT_VARIABLE CNA_FNA3D_MOJOSHADER_PATCH_APPLY_RESULT
    )
    if(NOT CNA_FNA3D_MOJOSHADER_PATCH_APPLY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "CNA: failed to apply ${CNA_FNA3D_MOJOSHADER_PATCH} to FNA3D's MojoShader "
            "submodule -- the pinned FNA3D/MojoShader revisions may no longer match the patch.")
    endif()

    message(STATUS "CNA: applied ${CNA_FNA3D_MOJOSHADER_PATCH_NAME}")
endforeach()
