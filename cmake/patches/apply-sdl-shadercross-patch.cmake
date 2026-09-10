# Idempotently applies CNA's narrow build-integration patch to the pinned SDL_shadercross tree.

if(NOT DEFINED CNA_SDL_SHADERCROSS_PATCH_FILE)
    message(FATAL_ERROR
        "CNA: apply-sdl-shadercross-patch.cmake requires "
        "-DCNA_SDL_SHADERCROSS_PATCH_FILE=<path>")
endif()
if(NOT EXISTS "${CNA_SDL_SHADERCROSS_PATCH_FILE}")
    message(FATAL_ERROR "CNA: ${CNA_SDL_SHADERCROSS_PATCH_FILE} does not exist")
endif()

find_program(CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE git)
if(NOT CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE)
    message(FATAL_ERROR
        "CNA: git not found -- required to apply ${CNA_SDL_SHADERCROSS_PATCH_FILE}")
endif()

get_filename_component(CNA_SDL_SHADERCROSS_SOURCE_DIR "." ABSOLUTE)
if(NOT EXISTS "${CNA_SDL_SHADERCROSS_SOURCE_DIR}/src/SDL_shadercross.c")
    message(FATAL_ERROR
        "CNA: ${CNA_SDL_SHADERCROSS_SOURCE_DIR} is not an SDL_shadercross source tree")
endif()

execute_process(
    COMMAND "${CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_SDL_SHADERCROSS_SOURCE_DIR}" apply --check
            "${CNA_SDL_SHADERCROSS_PATCH_FILE}"
    RESULT_VARIABLE CNA_SDL_SHADERCROSS_PATCH_FORWARD
    OUTPUT_QUIET ERROR_QUIET)
if(CNA_SDL_SHADERCROSS_PATCH_FORWARD EQUAL 0)
    execute_process(
        COMMAND "${CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE}" -C
                "${CNA_SDL_SHADERCROSS_SOURCE_DIR}" apply
                "${CNA_SDL_SHADERCROSS_PATCH_FILE}"
        RESULT_VARIABLE CNA_SDL_SHADERCROSS_PATCH_RESULT)
    if(NOT CNA_SDL_SHADERCROSS_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "CNA: failed to patch the pinned SDL_shadercross source")
    endif()
    message(STATUS "CNA: applied the SDL_shadercross build-integration patch")
    return()
endif()

execute_process(
    COMMAND "${CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE}" -C
            "${CNA_SDL_SHADERCROSS_SOURCE_DIR}" apply --reverse --check
            "${CNA_SDL_SHADERCROSS_PATCH_FILE}"
    RESULT_VARIABLE CNA_SDL_SHADERCROSS_PATCH_REVERSE
    OUTPUT_QUIET ERROR_QUIET)
if(CNA_SDL_SHADERCROSS_PATCH_REVERSE EQUAL 0)
    message(STATUS "CNA: SDL_shadercross build-integration patch already applied -- skipping")
    return()
endif()

message(FATAL_ERROR
    "CNA: SDL_shadercross source matches neither side of the required patch; verify "
    "CNA_SDL_SHADERCROSS_GIT_TAG and any FETCHCONTENT_SOURCE_DIR override")
