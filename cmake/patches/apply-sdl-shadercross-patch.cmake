# FetchContent's PATCH_COMMAND for SDL_shadercross, run in the clone FetchContent made for this
# build tree. The work, and why it is done this way, is in cmake/SdlShadercrossPatch.cmake.
#
#   cmake -DCNA_SDL_SHADERCROSS_PATCH_FILE=<patch> [-DCNA_SDL_SHADERCROSS_SOURCE_DIR=<dir>] -P <this>

if(NOT DEFINED CNA_SDL_SHADERCROSS_PATCH_FILE)
    message(FATAL_ERROR
        "CNA: apply-sdl-shadercross-patch.cmake requires "
        "-DCNA_SDL_SHADERCROSS_PATCH_FILE=<path>")
endif()
if(NOT DEFINED CNA_SDL_SHADERCROSS_SOURCE_DIR)
    get_filename_component(CNA_SDL_SHADERCROSS_SOURCE_DIR "." ABSOLUTE)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../SdlShadercrossPatch.cmake")
cna_sdl_shadercross_apply_patch(
    SOURCE_DIR "${CNA_SDL_SHADERCROSS_SOURCE_DIR}"
    PATCH "${CNA_SDL_SHADERCROSS_PATCH_FILE}")
