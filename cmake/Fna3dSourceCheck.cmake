# SPDX-License-Identifier: MS-PL
#
# Is the FNA3D checkout CNA is about to build the one it pins? Run after FetchContent has populated
# it (or taken FETCHCONTENT_SOURCE_DIR_FNA3D), every configure, read-only.
#
# FetchContent alone does not make sure: its update step returns early once FNA3D's HEAD is the
# pinned commit, before it looks at submodules, so a MojoShader moved or edited inside the checkout
# survives every reconfigure; and with FETCHCONTENT_UPDATES_DISCONNECTED, CMake before 3.27 does
# not move an existing checkout when the pin changes at all (2026-10-26 dependency audit, D-03 and
# D-04). So this compares, with git and nothing else:
#   - FNA3D's HEAD with CNA_FNA3D_GIT_TAG (exactly, when that is a full commit id);
#   - the MojoShader commit FNA3D's HEAD records (its gitlink) with MojoShader's own HEAD -- after
#     making sure MojoShader is a work tree of its own, since `git -C MojoShader` inside a plain
#     directory answers for FNA3D; and without `git status` for the submodule, whose report
#     submodule.MojoShader.ignore=all silences;
#   - local changes to tracked files in either.
#
# A FetchContent-populated checkout that fails is an error, with the repair, unless
# CNA_FNA3D_ALLOW_SOURCE_DRIFT=ON turns it into a warning. A FETCHCONTENT_SOURCE_DIR_FNA3D checkout
# is the developer's on purpose -- a fork change tried before the pin moves -- so there every
# finding is a warning, and a checkout git cannot read is built unverified. A missing MojoShader
# submodule is an error in both.
#
# Included by cmake/ThirdPartyFNA3D.cmake; cmake/Tests/Fna3dSourceCheckCase.cmake drives it on
# fixture repositories.

include_guard(GLOBAL)

# _cna_fna3d_check_git(<rc-var> <out-var> <dir> <git args>...)
# Runs git in <dir> without inheriting GIT_DIR/GIT_WORK_TREE from the environment, and without
# taking the optional index lock, so a read-only shared checkout stays untouched.
function(_cna_fna3d_check_git rc_var out_var dir)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=GIT_DIR --unset=GIT_WORK_TREE
                --unset=GIT_INDEX_FILE --unset=GIT_COMMON_DIR
                "${_cna_check_git}" --no-optional-locks -C "${dir}" ${ARGN}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(${rc_var} "${_rc}" PARENT_SCOPE)
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

# TRUE when <dir> is the top of a git work tree of its own (a clone, a worktree or a submodule with
# a .git file), not a directory inside some enclosing repository.
function(_cna_fna3d_check_is_own_worktree out_var dir)
    set(${out_var} FALSE PARENT_SCOPE)
    if(NOT EXISTS "${dir}/.git")
        return()
    endif()
    _cna_fna3d_check_git(_rc _top "${dir}" rev-parse --show-toplevel)
    if(NOT _rc EQUAL 0 OR _top STREQUAL "")
        return()
    endif()
    file(REAL_PATH "${_top}" _top)
    file(REAL_PATH "${dir}" _dir)
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${_top}" _top)
        string(TOLOWER "${_dir}" _dir)
    endif()
    if(_top STREQUAL _dir)
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

# cna_fna3d_verify_source(SOURCE_DIR <dir> TAG <tag> REPOSITORY <url> MODE PINNED|OVERRIDE
#                         [ALLOW_DRIFT <bool>] [GIT <git executable>])
function(cna_fna3d_verify_source)
    cmake_parse_arguments(_V "" "SOURCE_DIR;TAG;REPOSITORY;MODE;ALLOW_DRIFT;GIT" "" ${ARGN})
    set(_dir "${_V_SOURCE_DIR}")
    set(_mojo "${_dir}/MojoShader")
    get_filename_component(_deps "${_dir}" DIRECTORY)
    if(_V_MODE STREQUAL "OVERRIDE")
        set(_severity WARNING)
        set(_what "FETCHCONTENT_SOURCE_DIR_FNA3D=${_dir}")
    elseif(_V_MODE STREQUAL "PINNED")
        if(_V_ALLOW_DRIFT)
            set(_severity WARNING)
        else()
            set(_severity FATAL_ERROR)
        endif()
        set(_what "The FNA3D checkout FetchContent populated, ${_dir},")
    else()
        message(FATAL_ERROR "cna_fna3d_verify_source: MODE must be PINNED or OVERRIDE")
    endif()

    if(NOT EXISTS "${_mojo}/mojoshader.h")
        if(_V_MODE STREQUAL "PINNED")
            set(_repair "git -C ${_dir} submodule update --init --recursive, or delete ${_deps}/fna3d-src, fna3d-build and fna3d-subbuild and configure again to fetch it afresh")
        else()
            set(_repair "git -C ${_dir} submodule update --init")
        endif()
        message(FATAL_ERROR
            "CNA: ${_what} has no MojoShader/mojoshader.h -- its MojoShader submodule is not "
            "initialized. Run: ${_repair}")
    endif()

    # What cannot be verified at all.
    set(_git "${_V_GIT}")
    set(_cna_check_git "${_git}")
    set(_git_works FALSE)
    if(_git)
        execute_process(COMMAND "${_git}" --version RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(_rc EQUAL 0)
            set(_git_works TRUE)
        endif()
    endif()
    set(_unverifiable "")
    if(NOT _git_works)
        set(_unverifiable "git is not available ('${_git}')")
    else()
        _cna_fna3d_check_is_own_worktree(_fna3d_is_repo "${_dir}")
        if(NOT _fna3d_is_repo)
            set(_unverifiable "it is not a git checkout")
        endif()
    endif()
    if(NOT _unverifiable STREQUAL "")
        if(_V_MODE STREQUAL "OVERRIDE")
            message(STATUS
                "CNA: ${_what} cannot be checked against CNA_FNA3D_GIT_TAG=${_V_TAG}: "
                "${_unverifiable}. It is built as it is.")
        else()
            message(${_severity}
                "CNA: ${_what} cannot be checked against CNA_FNA3D_GIT_TAG=${_V_TAG}: "
                "${_unverifiable}. Delete ${_deps}/fna3d-src, fna3d-build and fna3d-subbuild and "
                "configure again (with git on PATH) to fetch it afresh, or configure with "
                "-DCNA_FNA3D_ALLOW_SOURCE_DRIFT=ON to build it unverified.")
        endif()
        return()
    endif()

    set(_findings "")
    set(_repairs "")

    _cna_fna3d_check_git(_rc _head "${_dir}" rev-parse --verify --quiet HEAD)
    string(TOLOWER "${_V_TAG}" _tag_lower)
    string(LENGTH "${_tag_lower}" _tag_length)
    if(_tag_length EQUAL 40 AND _tag_lower MATCHES "^[0-9a-f]+$")
        set(_expected "${_tag_lower}")
    else()
        _cna_fna3d_check_git(_rc _expected "${_dir}" rev-parse --verify --quiet "${_V_TAG}^{commit}")
        if(NOT _rc EQUAL 0)
            _cna_fna3d_check_git(_rc _expected "${_dir}" rev-parse --verify --quiet
                "refs/remotes/origin/${_V_TAG}^{commit}")
        endif()
        if(NOT _rc EQUAL 0)
            set(_expected "")
        endif()
    endif()
    if(_expected STREQUAL "")
        list(APPEND _findings
            "FNA3D is at ${_head}, and CNA_FNA3D_GIT_TAG=${_V_TAG} names no commit in this checkout")
    elseif(NOT _head STREQUAL _expected)
        list(APPEND _findings "FNA3D is at ${_head}, not CNA_FNA3D_GIT_TAG=${_V_TAG} (${_expected})")
        if(_V_MODE STREQUAL "PINNED")
            list(APPEND _repairs "git -C ${_dir} checkout --detach ${_expected}")
        endif()
    endif()
    _cna_fna3d_check_git(_rc _changes "${_dir}" status --porcelain --untracked-files=no
        --ignore-submodules=all)
    if(NOT _changes STREQUAL "")
        string(REPLACE "\n" ", " _changes "${_changes}")
        list(APPEND _findings "FNA3D has local changes (${_changes})")
        list(APPEND _repairs "git -C ${_dir} status (to see them), git -C ${_dir} checkout -- . (to discard them)")
    endif()

    # The MojoShader commit FNA3D's HEAD records. No gitlink means an FNA3D revision that carried
    # MojoShader's files itself; FNA3D's own status above already covers them.
    _cna_fna3d_check_git(_rc _entry "${_dir}" ls-tree HEAD -- MojoShader)
    set(_mojo_head "")
    if(_entry MATCHES "^160000 commit ([0-9a-f]+)\t")
        set(_gitlink "${CMAKE_MATCH_1}")
        _cna_fna3d_check_is_own_worktree(_mojo_is_repo "${_mojo}")
        if(NOT _mojo_is_repo)
            list(APPEND _findings
                "MojoShader is not a checked-out submodule (no git work tree of its own), so it cannot be compared with the ${_gitlink} FNA3D records")
            list(APPEND _repairs "git -C ${_dir} submodule update --init --recursive")
        else()
            _cna_fna3d_check_git(_rc _mojo_head "${_mojo}" rev-parse --verify --quiet HEAD)
            if(NOT _mojo_head STREQUAL _gitlink)
                list(APPEND _findings
                    "MojoShader is at ${_mojo_head}, but FNA3D ${_head} records ${_gitlink}")
                list(APPEND _repairs "git -C ${_dir} submodule update --init --recursive")
            endif()
            _cna_fna3d_check_git(_rc _mojo_changes "${_mojo}" status --porcelain
                --untracked-files=no)
            if(NOT _mojo_changes STREQUAL "")
                string(REPLACE "\n" ", " _mojo_changes "${_mojo_changes}")
                list(APPEND _findings "MojoShader has local changes (${_mojo_changes})")
                list(APPEND _repairs
                    "git -C ${_mojo} status (to see them), git -C ${_mojo} checkout -- . (to discard them)")
            endif()
        endif()
    endif()

    if(NOT _findings)
        if(_mojo_head STREQUAL "")
            message(STATUS "CNA: FNA3D source verified at ${_head} (${_dir})")
        else()
            message(STATUS "CNA: FNA3D source verified at ${_head}, MojoShader at ${_mojo_head} (${_dir})")
        endif()
        return()
    endif()

    string(REPLACE ";" "\n  - " _findings_text "${_findings}")
    list(REMOVE_DUPLICATES _repairs)
    if(_V_MODE STREQUAL "OVERRIDE")
        message(WARNING
            "CNA: ${_what} is not CNA's pin ${_V_REPOSITORY} at ${_V_TAG}:\n  - ${_findings_text}\n"
            "It is built as it is; CNA does not patch or update FNA3D or MojoShader.")
        return()
    endif()
    list(APPEND _repairs
        "or delete ${_deps}/fna3d-src, fna3d-build and fna3d-subbuild and configure again to fetch the pin afresh")
    string(REPLACE ";" "\n  " _repairs_text "${_repairs}")
    set(_disconnected "")
    if(FETCHCONTENT_FULLY_DISCONNECTED OR FETCHCONTENT_UPDATES_DISCONNECTED
       OR FETCHCONTENT_UPDATES_DISCONNECTED_FNA3D)
        set(_disconnected
            " FETCHCONTENT_FULLY_DISCONNECTED / FETCHCONTENT_UPDATES_DISCONNECTED is set, which can "
            "leave an existing checkout where it was.")
    endif()
    message(${_severity}
        "CNA: ${_what} is not CNA's pin ${_V_REPOSITORY} at ${_V_TAG}:\n  - ${_findings_text}\n"
        "FetchContent does not repair this by itself.${_disconnected} To restore the pin:\n  "
        "${_repairs_text}\n"
        "To build these sources as they are, configure with -DCNA_FNA3D_ALLOW_SOURCE_DRIFT=ON.")
endfunction()
