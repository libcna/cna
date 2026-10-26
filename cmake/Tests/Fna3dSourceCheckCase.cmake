# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, findings D-03 and D-04: the FNA3D checkout CNA builds is compared
# with the pin on every configure -- FNA3D's HEAD with CNA_FNA3D_GIT_TAG, MojoShader's HEAD with the
# gitlink FNA3D records, and local changes in either -- instead of trusting FetchContent, whose
# update step returns early once FNA3D's HEAD is right and never looks at a drifted submodule.
#
# Fixture repositories stand in for FNA3D and MojoShader: a fake FNA3D (CMakeLists shaped like the
# real one) with a MojoShader submodule. Part 1 runs cmake/Fna3dSourceCheck.cmake directly on
# clones put into each state; part 2 configures a small project through the real
# cna_configure_mojoshader(), FetchContent and all, from those local repositories. No network.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> [-DCNA_GENERATOR=<g>] \
#         -P cmake/Tests/Fna3dSourceCheckCase.cmake

cmake_minimum_required(VERSION 3.20)

# Child process for part 1: a FATAL_ERROR has to end a process of its own.
if(DEFINED CNA_CHECK_CASE)
    include("${CNA_SOURCE_DIR}/cmake/Fna3dSourceCheck.cmake")
    cna_fna3d_verify_source(SOURCE_DIR "${CHECK_SOURCE}" TAG "${CHECK_TAG}"
        REPOSITORY "fixture-fna3d" MODE "${CHECK_MODE}" ALLOW_DRIFT "${CHECK_ALLOW}"
        GIT "${CHECK_GIT}")
    return()
endif()

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Fna3dSourceCheckCase.cmake requires -D${_required}=...")
    endif()
endforeach()
find_program(_git_exe git)
if(NOT _git_exe)
    message(STATUS "Fna3dSourceCheckCase: git not found -- nothing to test")
    return()
endif()

set(_w "${CNA_WORK_DIR}")
file(REMOVE_RECURSE "${_w}")
file(MAKE_DIRECTORY "${_w}")
# Fixture git runs see only this configuration, never the developer's.
set(_gitconfig "${_w}/gitconfig")
file(WRITE "${_gitconfig}" "[user]
\tname = CNA fixture
\temail = fixture@example.invalid
[commit]
\tgpgsign = false
[init]
\tdefaultBranch = main
[protocol \"file\"]
\tallow = always
[core]
\tautocrlf = false
")
set(_git_env "${CMAKE_COMMAND}" -E env GIT_CONFIG_NOSYSTEM=1 "GIT_CONFIG_GLOBAL=${_gitconfig}"
    --unset=GIT_DIR --unset=GIT_WORK_TREE)

function(_git dir)
    execute_process(COMMAND ${_git_env} "${_git_exe}" -C "${dir}" ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Fna3dSourceCheckCase: git ${ARGN} in ${dir} failed:\n${_out}${_err}")
    endif()
    set(_git_out "${_out}" PARENT_SCOPE)
endfunction()

# --- fixture repositories --------------------------------------------------------------------
set(_mojo_origin "${_w}/origin/mojoshader")
file(WRITE "${_mojo_origin}/mojoshader.h" "int mojoshader_version(void);\n")
file(WRITE "${_mojo_origin}/mojoshader.c" "int mojoshader_version(void) { return 1; }\n")
file(WRITE "${_mojo_origin}/mojoshader_sdlgpu.c" "#error the SDL-free build drops this file\n")
file(WRITE "${_mojo_origin}/profiles/mojoshader_profile_bytecode.c" "int mojoshader_bytecode;\n")
_git("${_mojo_origin}" init -q)
_git("${_mojo_origin}" add -A)
_git("${_mojo_origin}" commit -q -m "MojoShader one")
_git("${_mojo_origin}" rev-parse HEAD)
set(_m1 "${_git_out}")
file(WRITE "${_mojo_origin}/mojoshader.c" "int mojoshader_version(void) { return 2; }\n")
_git("${_mojo_origin}" commit -q -a -m "MojoShader two")
_git("${_mojo_origin}" rev-parse HEAD)
set(_m2 "${_git_out}")

set(_fna3d_origin "${_w}/origin/fna3d")
file(WRITE "${_fna3d_origin}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.10)
project(FNA3D C)
option(BUILD_SHARED_LIBS \"Build shared library\" ON)
add_definitions(-DMOJOSHADER_USE_SDL_STDLIB -DUSE_SDL3 -DSUPPORT_PROFILE_BYTECODE=0)
add_library(FNA3D src/FNA3D.c)
target_include_directories(FNA3D PUBLIC include)
add_library(mojoshader STATIC MojoShader/mojoshader.c MojoShader/mojoshader_sdlgpu.c)
target_include_directories(mojoshader PUBLIC MojoShader)
target_link_libraries(FNA3D PRIVATE mojoshader)
")
file(WRITE "${_fna3d_origin}/include/FNA3D.h" "int fna3d_version(void);\n")
file(WRITE "${_fna3d_origin}/src/FNA3D.c" "int fna3d_version(void) { return 1; }\n")
_git("${_fna3d_origin}" init -q)
_git("${_fna3d_origin}" submodule --quiet add "${_mojo_origin}" MojoShader)
_git("${_fna3d_origin}/MojoShader" checkout -q --detach "${_m1}")
_git("${_fna3d_origin}" add -A)
_git("${_fna3d_origin}" commit -q -m "FNA3D one")
_git("${_fna3d_origin}" tag v1)
_git("${_fna3d_origin}" rev-parse HEAD)
set(_f1 "${_git_out}")
file(WRITE "${_fna3d_origin}/src/FNA3D.c" "int fna3d_version(void) { return 2; }\n")
_git("${_fna3d_origin}" commit -q -a -m "FNA3D two")
_git("${_fna3d_origin}" rev-parse HEAD)
set(_f2 "${_git_out}")

# A clone of FNA3D at F1 with MojoShader at the commit F1 records, as FetchContent leaves it.
function(_clone name)
    set(_dir "${_w}/${name}")
    _git("${_w}" clone -q "${_fna3d_origin}" "${_dir}")
    _git("${_dir}" checkout -q --detach "${_f1}")
    _git("${_dir}" submodule --quiet update --init --recursive)
endfunction()

set(_failures 0)
macro(_fail label text)
    message(SEND_ERROR "Fna3dSourceCheckCase: ${label}: ${text}\n--- log ---\n${_log}")
    math(EXPR _failures "${_failures} + 1")
endmacro()

# _check(<label> <dir> <tag> <mode> <allow> <PASS|WARN|STATUS|FAIL> <regex> [git])
macro(_check label dir tag mode allow want regex)
    set(_check_git "${_git_exe}")
    if(NOT "${ARGN}" STREQUAL "")
        set(_check_git "${ARGN}")
    endif()
    execute_process(
        COMMAND ${_git_env} "${CMAKE_COMMAND}" "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" -DCNA_CHECK_CASE=1
            "-DCHECK_SOURCE=${dir}" "-DCHECK_TAG=${tag}" "-DCHECK_MODE=${mode}"
            "-DCHECK_ALLOW=${allow}" "-DCHECK_GIT=${_check_git}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    set(_log "${_stdout}${_stderr}")
    string(REGEX REPLACE "\n[ ]+" " " _flat "${_log}")
    set(_ok TRUE)
    if("${want}" STREQUAL "FAIL")
        if(_rc EQUAL 0)
            _fail("${label}" "expected the check to stop the configure")
            set(_ok FALSE)
        endif()
    elseif(NOT _rc EQUAL 0)
        _fail("${label}" "expected the configure to continue (${want})")
        set(_ok FALSE)
    elseif("${want}" STREQUAL "WARN" AND NOT _log MATCHES "CMake Warning")
        _fail("${label}" "expected a warning")
        set(_ok FALSE)
    elseif(NOT "${want}" STREQUAL "WARN" AND _log MATCHES "CMake Warning")
        _fail("${label}" "expected no warning")
        set(_ok FALSE)
    endif()
    if(_ok AND NOT _flat MATCHES "${regex}")
        _fail("${label}" "the output does not say '${regex}'")
        set(_ok FALSE)
    endif()
    if(_ok)
        message(STATUS "ok: ${label}")
    endif()
endmacro()

# --- part 1: the check itself ------------------------------------------------------------------
set(_verified "FNA3D source verified at ${_f1}, MojoShader at ${_m1}")
set(_restore "delete .*fna3d-src, fna3d-build and fna3d-subbuild.*CNA_FNA3D_ALLOW_SOURCE_DRIFT=ON")

_clone(correct)
_check("correct checkout, full commit" "${_w}/correct" "${_f1}" PINNED OFF PASS "${_verified}")
_check("correct checkout, symbolic tag" "${_w}/correct" v1 PINNED OFF PASS "${_verified}")
_check("correct checkout, override" "${_w}/correct" "${_f1}" OVERRIDE OFF PASS "${_verified}")
file(WRITE "${_w}/correct/notes.txt" "untracked\n")
_check("untracked files are not changes" "${_w}/correct" "${_f1}" PINNED OFF PASS "${_verified}")

_clone(fna3d-moved)
_git("${_w}/fna3d-moved" checkout -q --detach "${_f2}")
set(_moved "FNA3D is at ${_f2}, not CNA_FNA3D_GIT_TAG=${_f1}")
_check("FNA3D HEAD moved, pinned" "${_w}/fna3d-moved" "${_f1}" PINNED OFF FAIL
    "${_moved}.*checkout --detach ${_f1}.*${_restore}")
_check("FNA3D HEAD moved, escape hatch" "${_w}/fna3d-moved" "${_f1}" PINNED ON WARN "${_moved}")
_check("FNA3D HEAD moved, override" "${_w}/fna3d-moved" "${_f1}" OVERRIDE OFF WARN
    "${_moved}.*built as it is")
_check("unknown tag, override" "${_w}/fna3d-moved" no-such-ref OVERRIDE OFF WARN
    "CNA_FNA3D_GIT_TAG=no-such-ref names no commit")
_check("unknown tag, pinned" "${_w}/fna3d-moved" no-such-ref PINNED OFF FAIL "names no commit")

_clone(mojo-drift)
_git("${_w}/mojo-drift/MojoShader" checkout -q --detach "${_m2}")
set(_drift "MojoShader is at ${_m2}, but FNA3D ${_f1} records ${_m1}")
_check("MojoShader drifted, pinned" "${_w}/mojo-drift" "${_f1}" PINNED OFF FAIL
    "${_drift}.*submodule update --init --recursive")
_check("MojoShader drifted, escape hatch" "${_w}/mojo-drift" "${_f1}" PINNED ON WARN "${_drift}")
_check("MojoShader drifted, override" "${_w}/mojo-drift" "${_f1}" OVERRIDE OFF WARN "${_drift}")
_git("${_w}/mojo-drift" config submodule.MojoShader.ignore all)
_git("${_w}/mojo-drift" status --porcelain)
if(NOT _git_out STREQUAL "")
    _fail("ignore=all" "git status still reports the submodule, so this case proves nothing")
endif()
_check("MojoShader drifted under submodule.MojoShader.ignore=all (git status is silent)"
    "${_w}/mojo-drift" "${_f1}" PINNED OFF FAIL "${_drift}")

_clone(mojo-missing)
_git("${_w}/mojo-missing" submodule --quiet deinit -f MojoShader)
_check("MojoShader not initialized, pinned" "${_w}/mojo-missing" "${_f1}" PINNED OFF FAIL
    "MojoShader submodule is not initialized.*submodule update --init")
_check("MojoShader not initialized, override" "${_w}/mojo-missing" "${_f1}" OVERRIDE OFF FAIL
    "MojoShader submodule is not initialized.*git -C .*mojo-missing submodule update --init")

_clone(dirty-fna3d)
file(APPEND "${_w}/dirty-fna3d/src/FNA3D.c" "/* local edit */\n")
_check("FNA3D modified, pinned" "${_w}/dirty-fna3d" "${_f1}" PINNED OFF FAIL
    "FNA3D has local changes .* M src/FNA3D.c")
_check("FNA3D modified, override" "${_w}/dirty-fna3d" "${_f1}" OVERRIDE OFF WARN
    "FNA3D has local changes")

_clone(dirty-mojo)
file(APPEND "${_w}/dirty-mojo/MojoShader/mojoshader.c" "/* local edit */\n")
_check("MojoShader modified, pinned" "${_w}/dirty-mojo" "${_f1}" PINNED OFF FAIL
    "MojoShader has local changes .* M mojoshader.c")
_check("MojoShader modified, override" "${_w}/dirty-mojo" "${_f1}" OVERRIDE OFF WARN
    "MojoShader has local changes")

# MojoShader's files present but not a work tree of its own: `git -C MojoShader` then answers for
# FNA3D, which is the trap the check has to avoid.
_clone(mojo-plain)
file(REMOVE "${_w}/mojo-plain/MojoShader/.git")
_git("${_w}/mojo-plain/MojoShader" rev-parse HEAD)
if(NOT _git_out STREQUAL _f1)
    _fail("plain MojoShader" "expected git -C MojoShader to answer for FNA3D (${_f1}), got ${_git_out}")
endif()
_check("MojoShader is a plain directory, pinned" "${_w}/mojo-plain" "${_f1}" PINNED OFF FAIL
    "MojoShader is not a checked-out submodule")

# A copy without git metadata (a source export).
file(COPY "${_w}/correct/" DESTINATION "${_w}/export" PATTERN ".git" EXCLUDE)
if(EXISTS "${_w}/export/.git" OR EXISTS "${_w}/export/MojoShader/.git")
    _fail("source export" "the copy kept git metadata")
endif()
_check("source export, override" "${_w}/export" "${_f1}" OVERRIDE OFF STATUS
    "cannot be checked against CNA_FNA3D_GIT_TAG=${_f1}: it is not a git checkout. It is built as it is")
_check("source export, pinned" "${_w}/export" "${_f1}" PINNED OFF FAIL "it is not a git checkout")
_check("source export, pinned, escape hatch" "${_w}/export" "${_f1}" PINNED ON WARN
    "it is not a git checkout")

# No usable git.
_check("no git, override" "${_w}/correct" "${_f1}" OVERRIDE OFF STATUS "git is not available"
    "${_w}/no-such-git")
_check("no git, pinned" "${_w}/correct" "${_f1}" PINNED OFF FAIL "git is not available"
    "${_w}/no-such-git")

# A git worktree of an FNA3D clone (its .git is a file), with its own submodule checkout.
_git("${_w}/correct" worktree add -q --detach "${_w}/worktree" "${_f1}")
_git("${_w}/worktree" submodule --quiet update --init)
_check("git worktree" "${_w}/worktree" "${_f1}" PINNED OFF PASS
    "FNA3D source verified at ${_f1}, MojoShader at ${_m1}")

# --- part 2: the real cna_configure_mojoshader(), populated by FetchContent --------------------
set(_consumer "${_w}/consumer")
file(WRITE "${_consumer}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)
project(CnaFna3dSourceCheckConsumer C)
set(CNA_ENABLE_SDL OFF)
include(\"\${CNA_SOURCE_DIR}/cmake/ThirdPartyFNA3D.cmake\")
cna_configure_mojoshader()
")
set(_generator "")
if(DEFINED CNA_GENERATOR)
    set(_generator -G "${CNA_GENERATOR}")
endif()

# _consume(<label> <build-dir> <PASS|WARN|FAIL> <regex> [cmake args...])
macro(_consume label bin want regex)
    execute_process(
        COMMAND ${_git_env} "${CMAKE_COMMAND}" -S "${_consumer}" -B "${_w}/${bin}" ${_generator}
            "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}"
            "-DCNA_FNA3D_GIT_REPOSITORY:STRING=${_fna3d_origin}"
            "-DCNA_FNA3D_GIT_TAG:STRING=${_f1}"
            -DFETCHCONTENT_QUIET=ON
            ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    set(_log "${_stdout}${_stderr}")
    string(REGEX REPLACE "\n[ ]+" " " _flat "${_log}")
    string(REGEX REPLACE "CMake Warning at [^\n]*Fna3dSourcePin.cmake[^\n]*" "" _log_without_pin "${_log}")
    set(_ok TRUE)
    if("${want}" STREQUAL "FAIL")
        if(_rc EQUAL 0)
            _fail("${label}" "expected the configure to stop")
            set(_ok FALSE)
        endif()
    elseif(NOT _rc EQUAL 0)
        _fail("${label}" "expected the configure to succeed")
        set(_ok FALSE)
    elseif("${want}" STREQUAL "WARN" AND NOT _log_without_pin MATCHES "CMake Warning")
        _fail("${label}" "expected a warning")
        set(_ok FALSE)
    endif()
    if(_ok AND NOT _flat MATCHES "${regex}")
        _fail("${label}" "the output does not say '${regex}'")
        set(_ok FALSE)
    endif()
    if(_ok)
        message(STATUS "ok: ${label}")
    endif()
endmacro()

_consume("FetchContent-populated pin" fetch PASS "${_verified}")
# The D-03 case: FNA3D still at the pin, so FetchContent's update step returns early -- and the
# drifted submodule is built unless CNA looks.
_git("${_w}/fetch/_deps/fna3d-src/MojoShader" checkout -q --detach "${_m2}")
_consume("FetchContent checkout with a drifted MojoShader stops the configure" fetch FAIL
    "MojoShader is at ${_m2}, but FNA3D ${_f1} records ${_m1}")
_consume("... unless CNA_FNA3D_ALLOW_SOURCE_DRIFT=ON" fetch WARN "MojoShader is at ${_m2}"
    -DCNA_FNA3D_ALLOW_SOURCE_DRIFT=ON)
_git("${_w}/fetch/_deps/fna3d-src" submodule --quiet update --init --recursive)
_consume("... and the documented repair restores it" fetch PASS "${_verified}"
    -DCNA_FNA3D_ALLOW_SOURCE_DRIFT=OFF)
# D-04: a reused checkout that is not moved to a new pin -- FETCHCONTENT_FULLY_DISCONNECTED here,
# FETCHCONTENT_UPDATES_DISCONNECTED on CMake before 3.27 -- is caught by the HEAD comparison, on
# any CMake version.
_consume("a pin move that the disconnected checkout did not follow stops the configure" fetch FAIL
    "FNA3D is at ${_f1}, not CNA_FNA3D_GIT_TAG=${_f2}.*FETCHCONTENT_FULLY_DISCONNECTED"
    "-DCNA_FNA3D_GIT_TAG:STRING=${_f2}" -DFETCHCONTENT_FULLY_DISCONNECTED=ON)
_consume("... and moves once the checkout is allowed to update" fetch PASS
    "FNA3D source verified at ${_f2}, MojoShader at ${_m1}"
    "-DCNA_FNA3D_GIT_TAG:STRING=${_f2}" -DFETCHCONTENT_FULLY_DISCONNECTED=OFF)
_consume("FETCHCONTENT_SOURCE_DIR_FNA3D with a drifted MojoShader is a warning" override WARN
    "FETCHCONTENT_SOURCE_DIR_FNA3D=.*MojoShader is at ${_m2}"
    "-DFETCHCONTENT_SOURCE_DIR_FNA3D=${_w}/mojo-drift")

if(_failures GREATER 0)
    message(FATAL_ERROR "Fna3dSourceCheckCase: ${_failures} case(s) failed")
endif()
file(REMOVE_RECURSE "${_w}")
message(STATUS "Fna3dSourceCheckCase: all cases passed")
