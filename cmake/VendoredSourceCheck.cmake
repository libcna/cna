# SPDX-License-Identifier: MS-PL
#
# Is a vendored source directory (third_party/SDL, third_party/SDL_mixer) the revision CNA pins?
# CNA builds them as they are -- on desktop platforms SDL has no CNA patch left that would fail
# against the wrong base -- so `git pull` without `git submodule update` would otherwise rebuild SDL
# from the stale submodule and silently lose fixes the pin brought, such as the Vulkan
# defragmentation fix (2026-10-26 dependency audit C-03). Read-only: nothing in the source
# directory, the superproject or any repository is written.
#
# Three shapes, decided per directory:
#   A  the directory is a git work tree of its own (an initialized submodule, a clone, a worktree):
#      its HEAD must be the superproject's gitlink (`git ls-files -s`, mode 160000), or the
#      recorded commit when there is no gitlink. A mismatch stops the configure with the repair;
#      local changes are a warning. A stale record is a warning naming the right pair.
#   B  a plain directory inside a git checkout of CNA (submodule contents copied in): its git tree
#      id is computed in a private repository under the build directory and compared with the
#      tree recorded for the gitlink's commit in cmake/VendoredSources.cmake; a gitlink with no
#      record stops the configure too, so a moved gitlink cannot go unrecorded.
#   C  no git checkout of CNA around it (a source archive): the tree id against the record.
# On a host without executable bits and symbolic links (Windows) a tree mismatch can be the file
# modes alone, so there it is a warning. Without git nothing can be verified: a STATUS line, and the
# configure goes on. CNA_VENDORED_SOURCE_CHECK=WARN turns every refusal into a warning, OFF skips
# the check -- for building other sources on purpose.
#
# Included by cmake/ThirdPartySDL.cmake; cmake/Tests/VendoredSourceCheckCase.cmake drives it on
# fixture repositories.

include_guard(GLOBAL)

# _cna_vendored_git(<rc-var> <out-var> <dir> <git args>...), with no GIT_DIR & co. from outside and
# without the optional index lock.
function(_cna_vendored_git rc_var out_var dir)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=GIT_DIR --unset=GIT_WORK_TREE
                --unset=GIT_INDEX_FILE --unset=GIT_COMMON_DIR --unset=GIT_OBJECT_DIRECTORY
                "${_cna_vendored_git_exe}" --no-optional-locks -C "${dir}" ${ARGN}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(${rc_var} "${_rc}" PARENT_SCOPE)
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

function(_cna_vendored_same_path out_var a b)
    file(REAL_PATH "${a}" _a)
    file(REAL_PATH "${b}" _b)
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${_a}" _a)
        string(TOLOWER "${_b}" _b)
    endif()
    if(_a STREQUAL _b)
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# TRUE when <dir> is the top of a git work tree of its own.
function(_cna_vendored_is_own_worktree out_var dir)
    set(${out_var} FALSE PARENT_SCOPE)
    if(NOT EXISTS "${dir}/.git")
        return()
    endif()
    _cna_vendored_git(_rc _top "${dir}" rev-parse --show-toplevel)
    if(NOT _rc EQUAL 0 OR _top STREQUAL "")
        return()
    endif()
    _cna_vendored_same_path(_same "${_top}" "${dir}")
    set(${out_var} ${_same} PARENT_SCOPE)
endfunction()

# The git tree id of the files in <dir>, computed in a private repository at <scratch> (in the build
# tree): blobs are hashed, never written, and nothing outside <scratch> is touched. Each
# "<path> <commit>" in ARGN is a nested submodule: its directory is left out and its commit put back
# as a gitlink. The index is kept between configures as a stat cache: when git's own change
# detection (update-index --refresh, as `git status` uses) finds no changed, added or deleted file,
# the files are not hashed again -- about 0.4 s for SDL -- and on any doubt the index is rebuilt.
function(_cna_vendored_tree_id out_var error_var dir scratch)
    set(${out_var} "" PARENT_SCOPE)
    set(_git "${_cna_vendored_git_exe}")
    set(_env "${CMAKE_COMMAND}" -E env --unset=GIT_COMMON_DIR --unset=GIT_OBJECT_DIRECTORY
        "GIT_DIR=${scratch}/repo" "GIT_WORK_TREE=${dir}" "GIT_INDEX_FILE=${scratch}/index")
    set(_exclude "")
    set(_index_info "")
    foreach(_gitlink IN LISTS ARGN)
        if(_gitlink MATCHES "^(.+) ([0-9a-f]+)$")
            list(APPEND _exclude ":(exclude)${CMAKE_MATCH_1}")
            string(APPEND _index_info "160000 ${CMAKE_MATCH_2}\t${CMAKE_MATCH_1}\n")
        endif()
    endforeach()
    set(_key "source ${dir}\ngitlinks ${ARGN}\n")

    set(_reuse FALSE)
    if(EXISTS "${scratch}/index" AND EXISTS "${scratch}/key.txt")
        file(READ "${scratch}/key.txt" _old_key)
        if(_old_key STREQUAL _key)
            execute_process(COMMAND ${_env} "${_git}" update-index --refresh
                WORKING_DIRECTORY "${dir}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
            if(_rc EQUAL 0)
                execute_process(
                    COMMAND ${_env} "${_git}" ls-files --others --deleted -z -- . ${_exclude}
                    WORKING_DIRECTORY "${dir}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _extra
                    ERROR_QUIET)
                if(_rc EQUAL 0 AND _extra STREQUAL "")
                    set(_reuse TRUE)
                endif()
            endif()
        endif()
    endif()

    set(_rc 0)
    set(_err "")
    if(NOT _reuse)
        file(REMOVE_RECURSE "${scratch}")
        file(MAKE_DIRECTORY "${scratch}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env --unset=GIT_DIR --unset=GIT_WORK_TREE
                    "${_git}" init -q --bare "${scratch}/repo"
            RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_QUIET)
        if(_rc EQUAL 0)
            execute_process(COMMAND ${_env} "${_git}" config core.bare false
                RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_QUIET)
        endif()
        if(_rc EQUAL 0)
            execute_process(
                COMMAND ${_env} "${_git}" ls-files --others -z -- . ${_exclude}
                COMMAND ${_env} "${_git}" update-index --add --info-only -z --stdin
                WORKING_DIRECTORY "${dir}"
                RESULTS_VARIABLE _rcs ERROR_VARIABLE _err OUTPUT_QUIET)
            foreach(_r IN LISTS _rcs)
                if(NOT _r EQUAL 0)
                    set(_rc "${_r}")
                endif()
            endforeach()
        endif()
        if(_rc EQUAL 0 AND NOT _index_info STREQUAL "")
            file(WRITE "${scratch}/gitlinks.txt" "${_index_info}")
            execute_process(COMMAND ${_env} "${_git}" update-index --index-info
                INPUT_FILE "${scratch}/gitlinks.txt"
                RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_QUIET)
        endif()
        if(_rc EQUAL 0)
            file(WRITE "${scratch}/key.txt" "${_key}")
        endif()
    endif()
    set(_tree "")
    if(_rc EQUAL 0)
        execute_process(COMMAND ${_env} "${_git}" write-tree --missing-ok
            RESULT_VARIABLE _rc OUTPUT_VARIABLE _tree ERROR_VARIABLE _err
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(NOT _rc EQUAL 0)
        file(REMOVE_RECURSE "${scratch}")
        string(STRIP "${_err}" _err)
        set(${error_var} "git failed (${_rc}): ${_err}" PARENT_SCOPE)
        return()
    endif()
    set(${out_var} "${_tree}" PARENT_SCOPE)
    set(${error_var} "" PARENT_SCOPE)
endfunction()

# cna_vendored_source_check(NAME <name> SOURCE_DIR <dir> SUPERPROJECT <dir> MODE ON|WARN|OFF
#                           COMMIT <commit> TREE <tree> [GITLINKS "<path> <commit>"...]
#                           SCRATCH_DIR <dir> [GIT <git>] [HOST_LOSES_MODES <bool>])
function(cna_vendored_source_check)
    cmake_parse_arguments(_S ""
        "NAME;SOURCE_DIR;SUPERPROJECT;MODE;COMMIT;TREE;SCRATCH_DIR;GIT;HOST_LOSES_MODES"
        "GITLINKS" ${ARGN})
    # An empty one-value argument leaves its variable undefined before CMP0174.
    foreach(_arg IN ITEMS NAME SOURCE_DIR SUPERPROJECT MODE COMMIT TREE SCRATCH_DIR GIT)
        if(NOT DEFINED _S_${_arg})
            set(_S_${_arg} "")
        endif()
    endforeach()
    if(_S_MODE STREQUAL "OFF")
        return()
    elseif(_S_MODE STREQUAL "ON")
        set(_severity FATAL_ERROR)
    elseif(_S_MODE STREQUAL "WARN")
        set(_severity WARNING)
    else()
        message(FATAL_ERROR "CNA_VENDORED_SOURCE_CHECK must be ON, WARN or OFF, not '${_S_MODE}'")
    endif()
    if(NOT DEFINED _S_HOST_LOSES_MODES)
        set(_S_HOST_LOSES_MODES ${CMAKE_HOST_WIN32})
    endif()
    set(_name "${_S_NAME}")
    set(_dir "${_S_SOURCE_DIR}")
    set(_super "${_S_SUPERPROJECT}")
    file(RELATIVE_PATH _rel "${_super}" "${_dir}")
    set(_switch
        "To build these sources on purpose, configure with -DCNA_VENDORED_SOURCE_CHECK=WARN (a warning) or OFF (no check).")

    set(_cna_vendored_git_exe "${_S_GIT}")
    set(_git_works FALSE)
    if(_cna_vendored_git_exe)
        execute_process(COMMAND "${_cna_vendored_git_exe}" --version
            RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(_git_works TRUE)
        endif()
    endif()
    if(NOT _git_works)
        message(STATUS
            "CNA: git is not available ('${_cna_vendored_git_exe}'), so the revision of ${_name} "
            "in ${_dir} cannot be verified; it is built as it is")
        return()
    endif()

    set(_gitlink "")
    _cna_vendored_is_own_worktree(_super_is_repo "${_super}")
    if(_super_is_repo)
        _cna_vendored_git(_rc _entry "${_super}" ls-files -s -- "${_rel}")
        if(_entry MATCHES "^160000 ([0-9a-f]+) 0\t")
            set(_gitlink "${CMAKE_MATCH_1}")
        endif()
    endif()

    # A: a work tree of its own.
    _cna_vendored_is_own_worktree(_own "${_dir}")
    if(_own)
        _cna_vendored_git(_rc _head "${_dir}" rev-parse --verify --quiet HEAD)
        if(NOT _gitlink STREQUAL "")
            set(_expected "${_gitlink}")
            set(_says "the superproject's gitlink (git -C ${_super} ls-files -s -- ${_rel}) is")
        else()
            set(_expected "${_S_COMMIT}")
            set(_says "cmake/VendoredSources.cmake records")
        endif()
        if(NOT _head STREQUAL _expected)
            message(${_severity}
                "CNA: ${_dir} is a ${_name} checkout at ${_head}, but ${_says} ${_expected} -- "
                "usually a `git pull` without `git submodule update`. ${_name} is built from this "
                "directory as it is, so the build would not be the pinned revision. First check "
                "for work of your own (git -C ${_dir} status, git -C ${_dir} log --oneline "
                "${_expected}..HEAD), then update it:\n"
                "  git -C ${_super} submodule update --init ${_rel}\n${_switch}")
        endif()
        _cna_vendored_git(_rc _changes "${_dir}" status --porcelain --ignore-submodules=all)
        if(NOT _changes STREQUAL "")
            string(REPLACE "\n" ", " _changes "${_changes}")
            message(WARNING
                "CNA: ${_dir} (${_name}) has local changes (${_changes}); they are built as "
                "they are.")
        endif()
        if(_head STREQUAL _expected)
            _cna_vendored_git(_rc _head_tree "${_dir}" rev-parse --verify --quiet "HEAD^{tree}")
            if(NOT _head STREQUAL _S_COMMIT OR NOT _head_tree STREQUAL _S_TREE)
                message(WARNING
                    "CNA: cmake/VendoredSources.cmake records ${_name} ${_S_COMMIT} (tree "
                    "${_S_TREE}), but the checkout is the gitlink ${_head} (tree ${_head_tree}). "
                    "Record COMMIT ${_head} and TREE ${_head_tree} there (and its GITLINKS), or a "
                    "configure from a plain copy of the sources will refuse it.")
            else()
                message(STATUS "CNA: ${_name} source verified at ${_head} (${_dir})")
            endif()
        endif()
        return()
    endif()

    # B and C: files only.
    if(NOT _gitlink STREQUAL "")
        if(NOT _gitlink STREQUAL _S_COMMIT)
            message(${_severity}
                "CNA: the superproject's gitlink for ${_rel} is ${_gitlink}, but "
                "cmake/VendoredSources.cmake records ${_name} ${_S_COMMIT}, so the plain directory "
                "${_dir} cannot be verified. Whoever moved the gitlink: record COMMIT ${_gitlink}, "
                "TREE (git rev-parse ${_gitlink}^{tree}) and GITLINKS in "
                "cmake/VendoredSources.cmake. ${_switch}")
            return()
        endif()
        set(_shape "is a plain directory, not a checked-out submodule,")
        string(CONCAT _repair
            "Replace it with that revision: move the directory aside and run\n"
            "  git -C ${_super} submodule update --init ${_rel}\n")
    else()
        if(_S_COMMIT STREQUAL "" OR _S_TREE STREQUAL "")
            message(STATUS
                "CNA: no revision is recorded for ${_name}, so ${_dir} cannot be verified; it is "
                "built as it is")
            return()
        endif()
        set(_shape "is not part of a git checkout of CNA (a source archive?),")
        set(_repair "Replace it with the sources of ${_name} ${_S_COMMIT}.\n")
    endif()
    string(TOLOWER "${_name}" _scratch_name)
    _cna_vendored_tree_id(_tree _error "${_dir}" "${_S_SCRATCH_DIR}/${_scratch_name}"
        ${_S_GITLINKS})
    if(_tree STREQUAL "")
        message(WARNING
            "CNA: the files of ${_name} in ${_dir} could not be compared with ${_S_COMMIT}: "
            "${_error}. They are built as they are.")
        return()
    endif()
    if(_tree STREQUAL _S_TREE)
        message(STATUS "CNA: ${_name} source verified: ${_dir} has the files of ${_S_COMMIT}")
        return()
    endif()
    string(CONCAT _text
        "CNA: ${_dir} ${_shape} and its files are not ${_name} ${_S_COMMIT}: their git tree is "
        "${_tree}, that commit's is ${_S_TREE}. ${_name} is built from this directory as it is, so "
        "the build would not be the pinned revision. ${_repair}")
    if(_S_HOST_LOSES_MODES)
        message(WARNING
            "${_text}On this host file modes and symbolic links are not what git records, so a "
            "difference in them alone also changes the tree id; the contents may still be right.")
    else()
        message(${_severity} "${_text}${_switch}")
    endif()
endfunction()
