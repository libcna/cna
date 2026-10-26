# SPDX-License-Identifier: MS-PL
#
# CNA's one build-integration patch to SDL_shadercross (cmake/ThirdPartySDLShaderCross.cmake says
# why it is needed), applied so that the result is certain and nobody's checkout is changed:
#
#   cna_sdl_shadercross_apply_patch(SOURCE_DIR <dir> PATCH <file>)
#     Applies <file> to <dir> in place -- only ever to the clone FetchContent made for this build
#     tree (its PATCH_COMMAND, cmake/patches/apply-sdl-shadercross-patch.cmake) or to a staged copy.
#     `git apply` resolves paths against the repository it discovers, and inside an enclosing
#     repository that does not track these files it exits 0 having changed nothing (2026-10-26
#     dependency audit C-04). So discovery stops at <dir> (GIT_CEILING_DIRECTORIES), and success is
#     read from the files: every hunk's post-image must be there afterwards. Idempotent: a tree that
#     already has every post-image is left alone, one with neither image is refused.
#
#   cna_sdl_shadercross_stage_source(<out-var> SOURCE <dir> DESTINATION <dir> PATCH <file>)
#     For FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE: copies the developer's checkout
#     (without .git and the external/ submodules, which CNA's non-vendored build never reads) to
#     <DESTINATION> and patches the copy. The copy is redone only when the checkout or the patch
#     changed, so an unchanged configure rewrites no file and triggers no regeneration.
#
# Included by cmake/ThirdPartySDLShaderCross.cmake and the patch script;
# cmake/Tests/SdlShadercrossPatchCase.cmake drives both functions.

include_guard(GLOBAL)

# Every hunk of a unified diff as two lists of text blocks, pre- and post-image. Semicolons travel
# as a placeholder, so a line of CMake code cannot split a list element.
function(_cna_sdl_shadercross_patch_images pre_var post_var patch)
    file(READ "${patch}" _text)
    string(REPLACE "\r\n" "\n" _text "${_text}")
    string(REPLACE ";" "<cna-semicolon>" _text "${_text}")
    string(REPLACE "[" "<cna-open>" _text "${_text}")
    string(REPLACE "]" "<cna-close>" _text "${_text}")
    string(REGEX MATCHALL "[^\n]*\n" _lines "${_text}")
    set(_pre_blocks "")
    set(_post_blocks "")
    set(_in_hunk FALSE)
    foreach(_line IN LISTS _lines)
        string(REGEX REPLACE "\n$" "" _line "${_line}")
        if(_line MATCHES "^@@ ")
            if(_in_hunk)
                list(APPEND _pre_blocks "${_pre}")
                list(APPEND _post_blocks "${_post}")
            endif()
            set(_in_hunk TRUE)
            set(_pre "")
            set(_post "")
        elseif(_in_hunk AND _line MATCHES "^diff ")
            list(APPEND _pre_blocks "${_pre}")
            list(APPEND _post_blocks "${_post}")
            set(_in_hunk FALSE)
        elseif(_in_hunk)
            string(SUBSTRING "${_line}" 0 1 _kind)
            string(SUBSTRING "${_line}" 1 -1 _content)
            if(_kind STREQUAL " " OR _line STREQUAL "")
                string(APPEND _pre "${_content}\n")
                string(APPEND _post "${_content}\n")
            elseif(_kind STREQUAL "-")
                string(APPEND _pre "${_content}\n")
            elseif(_kind STREQUAL "+")
                string(APPEND _post "${_content}\n")
            endif()
        endif()
    endforeach()
    if(_in_hunk)
        list(APPEND _pre_blocks "${_pre}")
        list(APPEND _post_blocks "${_post}")
    endif()
    set(${pre_var} "${_pre_blocks}" PARENT_SCOPE)
    set(${post_var} "${_post_blocks}" PARENT_SCOPE)
endfunction()

# PATCHED, UNPATCHED or NEITHER, from the content of the one file the patch changes.
function(_cna_sdl_shadercross_patch_state out_var dir patch)
    file(STRINGS "${patch}" _target REGEX "^\\+\\+\\+ b/")
    list(LENGTH _target _count)
    if(NOT _count EQUAL 1)
        message(FATAL_ERROR "CNA: ${patch} must change exactly one file")
    endif()
    string(REGEX REPLACE "^\\+\\+\\+ b/" "" _target "${_target}")
    set(${out_var} NEITHER PARENT_SCOPE)
    if(NOT EXISTS "${dir}/${_target}")
        return()
    endif()
    file(READ "${dir}/${_target}" _content)
    string(REPLACE "\r\n" "\n" _content "${_content}")
    string(REPLACE ";" "<cna-semicolon>" _content "${_content}")
    string(REPLACE "[" "<cna-open>" _content "${_content}")
    string(REPLACE "]" "<cna-close>" _content "${_content}")
    _cna_sdl_shadercross_patch_images(_pre_blocks _post_blocks "${patch}")
    set(_all_post TRUE)
    set(_all_pre TRUE)
    foreach(_block IN LISTS _post_blocks)
        string(FIND "${_content}" "${_block}" _at)
        if(_at LESS 0)
            set(_all_post FALSE)
        endif()
    endforeach()
    foreach(_block IN LISTS _pre_blocks)
        string(FIND "${_content}" "${_block}" _at)
        if(_at LESS 0)
            set(_all_pre FALSE)
        endif()
    endforeach()
    if(_all_post AND NOT _all_pre)
        set(${out_var} PATCHED PARENT_SCOPE)
    elseif(_all_pre AND NOT _all_post)
        set(${out_var} UNPATCHED PARENT_SCOPE)
    endif()
endfunction()

function(cna_sdl_shadercross_apply_patch)
    cmake_parse_arguments(_A "" "SOURCE_DIR;PATCH" "" ${ARGN})
    get_filename_component(_dir "${_A_SOURCE_DIR}" ABSOLUTE)
    get_filename_component(_patch_name "${_A_PATCH}" NAME)
    if(NOT EXISTS "${_A_PATCH}")
        message(FATAL_ERROR "CNA: ${_A_PATCH} does not exist")
    endif()
    if(NOT EXISTS "${_dir}/src/SDL_shadercross.c")
        message(FATAL_ERROR "CNA: ${_dir} is not an SDL_shadercross source tree")
    endif()
    _cna_sdl_shadercross_patch_state(_state "${_dir}" "${_A_PATCH}")
    if(_state STREQUAL "PATCHED")
        message(STATUS "CNA: ${_patch_name} is already applied in ${_dir}")
        return()
    elseif(NOT _state STREQUAL "UNPATCHED")
        message(FATAL_ERROR
            "CNA: SDL_shadercross in ${_dir} matches neither side of ${_patch_name}; check "
            "CNA_SDL_SHADERCROSS_GIT_TAG and any FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE")
    endif()

    find_program(CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE git)
    if(NOT CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE)
        message(FATAL_ERROR "CNA: git not found -- required to apply ${_patch_name}")
    endif()
    file(REAL_PATH "${_dir}/.." _ceiling)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                --unset=GIT_DIR --unset=GIT_WORK_TREE --unset=GIT_INDEX_FILE
                "${CNA_SDL_SHADERCROSS_PATCH_GIT_EXECUTABLE}" apply --whitespace=nowarn "${_A_PATCH}"
        WORKING_DIRECTORY "${_dir}"
        RESULT_VARIABLE _rc
        ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "CNA: ${_patch_name} does not apply to ${_dir}:\n${_err}")
    endif()
    _cna_sdl_shadercross_patch_state(_state "${_dir}" "${_A_PATCH}")
    if(NOT _state STREQUAL "PATCHED")
        message(FATAL_ERROR
            "CNA: git apply reported success for ${_patch_name}, but ${_dir} does not have its "
            "result")
    endif()
    message(STATUS "CNA: applied ${_patch_name} to ${_dir}")
endfunction()

function(cna_sdl_shadercross_stage_source out_var)
    cmake_parse_arguments(_S "" "SOURCE;DESTINATION;PATCH" "" ${ARGN})
    get_filename_component(_source "${_S_SOURCE}" ABSOLUTE)
    # An override that already names the staged copy (a parent project's normal variable that
    # FetchContent turned into a cache entry) is the copy itself: only make sure it is patched.
    if(EXISTS "${_S_DESTINATION}")
        file(REAL_PATH "${_source}" _real_source)
        file(REAL_PATH "${_S_DESTINATION}" _real_destination)
        if(_real_source STREQUAL _real_destination)
            cna_sdl_shadercross_apply_patch(SOURCE_DIR "${_S_DESTINATION}" PATCH "${_S_PATCH}")
            set(${out_var} "${_S_DESTINATION}" PARENT_SCOPE)
            return()
        endif()
    endif()
    if(NOT EXISTS "${_source}/src/SDL_shadercross.c")
        message(FATAL_ERROR
            "CNA: FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE=${_source} is not an "
            "SDL_shadercross source tree")
    endif()
    # What the copy is made of: every file outside .git and external/, and the patch.
    file(GLOB_RECURSE _files LIST_DIRECTORIES false RELATIVE "${_source}" "${_source}/*")
    list(FILTER _files EXCLUDE REGEX "(^|/)\\.git(/|$)")
    list(FILTER _files EXCLUDE REGEX "^external/")
    list(SORT _files)
    file(SHA256 "${_S_PATCH}" _identity)
    set(_identity "patch ${_identity}\n")
    foreach(_file IN LISTS _files)
        file(SHA256 "${_source}/${_file}" _hash)
        string(APPEND _identity "${_file} ${_hash}\n")
    endforeach()
    string(SHA256 _identity "${_identity}")

    set(_stamp "${_S_DESTINATION}.cna-stage")
    if(EXISTS "${_stamp}" AND EXISTS "${_S_DESTINATION}/src/SDL_shadercross.c")
        file(READ "${_stamp}" _recorded)
        if(_recorded STREQUAL _identity)
            set(${out_var} "${_S_DESTINATION}" PARENT_SCOPE)
            return()
        endif()
    endif()
    file(REMOVE "${_stamp}")
    file(REMOVE_RECURSE "${_S_DESTINATION}")
    foreach(_file IN LISTS _files)
        get_filename_component(_subdir "${_file}" DIRECTORY)
        file(COPY "${_source}/${_file}" DESTINATION "${_S_DESTINATION}/${_subdir}")
    endforeach()
    # A refusal stops the configure; leave nothing half-staged behind. (Without the stamp a copy
    # is never trusted anyway.)
    _cna_sdl_shadercross_patch_state(_state "${_S_DESTINATION}" "${_S_PATCH}")
    if(NOT _state STREQUAL "PATCHED" AND NOT _state STREQUAL "UNPATCHED")
        file(REMOVE_RECURSE "${_S_DESTINATION}")
        get_filename_component(_patch_name "${_S_PATCH}" NAME)
        message(FATAL_ERROR
            "CNA: FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE=${_source} matches neither "
            "side of ${_patch_name}; it is not the SDL_shadercross revision CNA pins "
            "(CNA_SDL_SHADERCROSS_GIT_TAG).")
    endif()
    cna_sdl_shadercross_apply_patch(SOURCE_DIR "${_S_DESTINATION}" PATCH "${_S_PATCH}")
    file(WRITE "${_stamp}" "${_identity}")
    message(STATUS "CNA: staged a patched copy of ${_source} in ${_S_DESTINATION}")
    set(${out_var} "${_S_DESTINATION}" PARENT_SCOPE)
endfunction()
