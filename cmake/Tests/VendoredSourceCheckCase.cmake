# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, finding C-03: a vendored SDL directory is checked against the
# revision CNA pins before SDL is built -- an initialized submodule against the superproject's
# gitlink, a plain copy or a source archive by its git tree id -- instead of being accepted because
# it has a CMakeLists.txt.
#
# Fixture repositories stand in for SDL (with an executable file and a nested submodule, as
# SDL_mixer has) and for a CNA superproject. Each case runs cmake/VendoredSourceCheck.cmake in a
# child process, because a refusal is a FATAL_ERROR. The last case checks this checkout's real
# third_party/SDL and third_party/SDL_mixer against cmake/VendoredSources.cmake. No network.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> -P cmake/Tests/VendoredSourceCheckCase.cmake

cmake_minimum_required(VERSION 3.20)

if(DEFINED CNA_CHECK_CASE)
    include("${CNA_SOURCE_DIR}/cmake/VendoredSourceCheck.cmake")
    if(CHECK_REAL)
        include("${CNA_SOURCE_DIR}/cmake/VendoredSources.cmake")
        foreach(_dep IN ITEMS SDL SDL_mixer)
            cna_vendored_source_check(NAME ${_dep} SOURCE_DIR "${CNA_SOURCE_DIR}/third_party/${_dep}"
                SUPERPROJECT "${CNA_SOURCE_DIR}" MODE ON
                COMMIT "${CNA_VENDORED_SOURCE_${_dep}_COMMIT}"
                TREE "${CNA_VENDORED_SOURCE_${_dep}_TREE}"
                GITLINKS ${CNA_VENDORED_SOURCE_${_dep}_GITLINKS}
                SCRATCH_DIR "${CHECK_SCRATCH}" GIT "${CHECK_GIT}")
        endforeach()
        return()
    endif()
    cna_vendored_source_check(NAME SDL SOURCE_DIR "${CHECK_SOURCE}" SUPERPROJECT "${CHECK_SUPER}"
        MODE "${CHECK_MODE}" COMMIT "${CHECK_COMMIT}" TREE "${CHECK_TREE}"
        GITLINKS ${CHECK_GITLINKS} SCRATCH_DIR "${CHECK_SCRATCH}" GIT "${CHECK_GIT}"
        HOST_LOSES_MODES "${CHECK_LOSES_MODES}")
    return()
endif()

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "VendoredSourceCheckCase.cmake requires -D${_required}=...")
    endif()
endforeach()
find_program(_git_exe git)
if(NOT _git_exe)
    message(STATUS "VendoredSourceCheckCase: git not found -- nothing to test")
    return()
endif()

set(_w "${CNA_WORK_DIR}")
file(REMOVE_RECURSE "${_w}")
file(MAKE_DIRECTORY "${_w}")
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
        message(FATAL_ERROR "VendoredSourceCheckCase: git ${ARGN} in ${dir} failed:\n${_out}${_err}")
    endif()
    set(_git_out "${_out}" PARENT_SCOPE)
endfunction()

# --- fixture repositories --------------------------------------------------------------------
set(_dep_origin "${_w}/origin/dep")
file(WRITE "${_dep_origin}/dep.c" "int dep;\n")
_git("${_dep_origin}" init -q)
_git("${_dep_origin}" add -A)
_git("${_dep_origin}" commit -q -m "dep")
_git("${_dep_origin}" rev-parse HEAD)
set(_d1 "${_git_out}")

set(_sdl_origin "${_w}/origin/sdl")
file(WRITE "${_sdl_origin}/CMakeLists.txt" "project(FixtureSDL C)\n")
file(WRITE "${_sdl_origin}/src/a.c" "int a(void) { return 1; }\n")
file(WRITE "${_sdl_origin}/build-scripts/run.sh" "#!/bin/sh\nexit 0\n")
file(CHMOD "${_sdl_origin}/build-scripts/run.sh"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
_git("${_sdl_origin}" init -q)
_git("${_sdl_origin}" submodule --quiet add "${_dep_origin}" external/dep)
_git("${_sdl_origin}" add -A)
_git("${_sdl_origin}" update-index --chmod=+x build-scripts/run.sh)
_git("${_sdl_origin}" commit -q -m "SDL one")
_git("${_sdl_origin}" rev-parse HEAD)
set(_s1 "${_git_out}")
_git("${_sdl_origin}" rev-parse "HEAD^{tree}")
set(_t1 "${_git_out}")
file(WRITE "${_sdl_origin}/src/a.c" "int a(void) { return 2; }\n")
_git("${_sdl_origin}" commit -q -a -m "SDL two")
_git("${_sdl_origin}" rev-parse HEAD)
set(_s2 "${_git_out}")
_git("${_sdl_origin}" rev-parse "HEAD^{tree}")
set(_t2 "${_git_out}")
set(_gitlinks "external/dep ${_d1}")

set(_super_origin "${_w}/origin/super")
file(WRITE "${_super_origin}/CMakeLists.txt" "project(FixtureCNA NONE)\n")
_git("${_super_origin}" init -q)
_git("${_super_origin}" submodule --quiet add "${_sdl_origin}" third_party/SDL)
_git("${_super_origin}/third_party/SDL" checkout -q --detach "${_s1}")
_git("${_super_origin}" add -A)
_git("${_super_origin}" commit -q -m "CNA with SDL one")
_git("${_super_origin}" rev-parse HEAD)
set(_p1 "${_git_out}")
_git("${_super_origin}/third_party/SDL" checkout -q --detach "${_s2}")
_git("${_super_origin}" commit -q -a -m "CNA with SDL two")
_git("${_super_origin}" rev-parse HEAD)
set(_p2 "${_git_out}")

# A clone of the superproject at <commit>; SDL initialized (not recursively) unless PLAIN, in which
# case third_party/SDL is filled from `git archive <sdl-commit>`, as a plain directory.
function(_super name commit)
    cmake_parse_arguments(_C "" "PLAIN" "" ${ARGN})
    set(_dir "${_w}/${name}")
    _git("${_w}" clone -q "${_super_origin}" "${_dir}")
    _git("${_dir}" checkout -q --detach "${commit}")
    if(DEFINED _C_PLAIN)
        _sdl_files("${_dir}/third_party/SDL" "${_C_PLAIN}")
    else()
        _git("${_dir}" submodule --quiet update --init third_party/SDL)
    endif()
endfunction()

# The files of SDL <commit>, without git metadata, into <dir>.
function(_sdl_files dir commit)
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    _git("${_sdl_origin}" archive --format=tar -o "${_w}/sdl-${commit}.tar" "${commit}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_w}/sdl-${commit}.tar"
        WORKING_DIRECTORY "${dir}" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "VendoredSourceCheckCase: could not extract SDL ${commit}")
    endif()
endfunction()

set(_failures 0)
macro(_fail label text)
    message(SEND_ERROR "VendoredSourceCheckCase: ${label}: ${text}\n--- log ---\n${_log}")
    math(EXPR _failures "${_failures} + 1")
endmacro()

# _check(<label> <PASS|WARN|STATUS|QUIET|FAIL> <regex> SOURCE <dir> SUPER <dir>
#        [MODE ON] [COMMIT <c>] [TREE <t>] [NO_RECORD] [GITLINKS ...] [GIT <git>]
#        [LOSES_MODES ON] [SCRATCH <dir>])
macro(_check label want regex)
    cmake_parse_arguments(_K "NO_RECORD" "SOURCE;SUPER;MODE;COMMIT;TREE;GIT;LOSES_MODES;SCRATCH"
        "GITLINKS" ${ARGN})
    foreach(_default IN ITEMS "MODE|ON" "COMMIT|${_s1}" "TREE|${_t1}" "GIT|${_git_exe}"
            "LOSES_MODES|OFF" "SCRATCH|${_w}/scratch")
        string(REPLACE "|" ";" _pair "${_default}")
        list(GET _pair 0 _key)
        list(GET _pair 1 _value)
        if(NOT DEFINED _K_${_key})
            set(_K_${_key} "${_value}")
        endif()
    endforeach()
    if(NOT DEFINED _K_GITLINKS)
        set(_K_GITLINKS "${_gitlinks}")
    endif()
    if(_K_NO_RECORD)
        set(_K_COMMIT "")
        set(_K_TREE "")
    endif()
    string(REPLACE ";" "\\;" _gitlinks_arg "${_K_GITLINKS}")
    execute_process(
        COMMAND ${_git_env} "${CMAKE_COMMAND}" "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" -DCNA_CHECK_CASE=1
            "-DCHECK_SOURCE=${_K_SOURCE}" "-DCHECK_SUPER=${_K_SUPER}" "-DCHECK_MODE=${_K_MODE}"
            "-DCHECK_COMMIT=${_K_COMMIT}" "-DCHECK_TREE=${_K_TREE}"
            "-DCHECK_GITLINKS=${_gitlinks_arg}" "-DCHECK_GIT=${_K_GIT}"
            "-DCHECK_LOSES_MODES=${_K_LOSES_MODES}" "-DCHECK_SCRATCH=${_K_SCRATCH}"
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
    elseif("${want}" STREQUAL "QUIET" AND NOT _log STREQUAL "")
        _fail("${label}" "expected no output at all")
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

set(_switch "CNA_VENDORED_SOURCE_CHECK=WARN")

# --- A: an initialized submodule -----------------------------------------------------------
_super(a-correct "${_p1}")
_check("submodule at the gitlink" PASS "SDL source verified at ${_s1}"
    SOURCE "${_w}/a-correct/third_party/SDL" SUPER "${_w}/a-correct")

_super(a-stale "${_p1}")
_git("${_w}/a-stale/third_party/SDL" checkout -q --detach "${_s2}")
set(_stale_a "is a SDL checkout at ${_s2}, but the superproject's gitlink .* is ${_s1}")
_check("submodule left at another commit (git pull without submodule update)" FAIL
    "${_stale_a}.*git -C .*a-stale/third_party/SDL log --oneline ${_s1}..HEAD.*submodule update --init third_party/SDL.*${_switch}"
    SOURCE "${_w}/a-stale/third_party/SDL" SUPER "${_w}/a-stale")
_check("... CNA_VENDORED_SOURCE_CHECK=WARN" WARN "${_stale_a}"
    SOURCE "${_w}/a-stale/third_party/SDL" SUPER "${_w}/a-stale" MODE WARN)
_check("... CNA_VENDORED_SOURCE_CHECK=OFF" QUIET ""
    SOURCE "${_w}/a-stale/third_party/SDL" SUPER "${_w}/a-stale" MODE OFF)
_git("${_w}/a-stale/third_party/SDL" status --porcelain)
if(NOT _git_out STREQUAL "")
    _fail("stale submodule" "the check changed the submodule: ${_git_out}")
endif()
_git("${_w}/a-stale/third_party/SDL" rev-parse HEAD)
if(NOT _git_out STREQUAL _s2)
    _fail("stale submodule" "the check moved the submodule")
endif()

_super(a-dirty "${_p1}")
file(APPEND "${_w}/a-dirty/third_party/SDL/src/a.c" "/* local edit */\n")
_check("submodule with local changes" WARN "has local changes .* M src/a.c"
    SOURCE "${_w}/a-dirty/third_party/SDL" SUPER "${_w}/a-dirty")

_super(a-record "${_p2}")
_check("gitlink moved, record not updated" WARN
    "records SDL ${_s1} .tree ${_t1}., but the checkout is the gitlink ${_s2} .tree ${_t2}.*Record COMMIT ${_s2} and TREE ${_t2}"
    SOURCE "${_w}/a-record/third_party/SDL" SUPER "${_w}/a-record")

_git("${_w}/a-correct" worktree add -q --detach "${_w}/a-worktree" "${_p1}")
_git("${_w}/a-worktree" submodule --quiet update --init third_party/SDL)
_check("a git worktree of the superproject" PASS "SDL source verified at ${_s1}"
    SOURCE "${_w}/a-worktree/third_party/SDL" SUPER "${_w}/a-worktree")

_check("no superproject gitlink: the record decides" FAIL
    "is a SDL checkout at ${_s2}, but cmake/VendoredSources.cmake records ${_s1}"
    SOURCE "${_w}/a-stale/third_party/SDL" SUPER "${_w}/a-stale/third_party")

# --- B: a plain directory inside a git checkout of CNA ----------------------------------------
_super(b-correct "${_p1}" PLAIN "${_s1}")
_check("plain directory with the gitlink's files" PASS "SDL source verified: .* has the files of ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" SCRATCH "${_w}/scratch-b")
_check("... again, from the stat cache" PASS "has the files of ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" SCRATCH "${_w}/scratch-b")
file(TOUCH "${_w}/b-correct/third_party/SDL/src/a.c")
_check("... a touched file with the same content" PASS "has the files of ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" SCRATCH "${_w}/scratch-b")
file(WRITE "${_w}/b-correct/third_party/SDL/src/a.c" "int a(void) { return 3; }\n")
_check("... a same-size edit is not hidden by the stat cache" FAIL "files are not SDL ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" SCRATCH "${_w}/scratch-b")
file(WRITE "${_w}/b-correct/third_party/SDL/src/a.c" "int a(void) { return 1; }\n")
file(WRITE "${_w}/b-correct/third_party/SDL/src/extra.c" "int extra;\n")
_check("... nor an added file" FAIL "files are not SDL ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" SCRATCH "${_w}/scratch-b")
file(REMOVE "${_w}/b-correct/third_party/SDL/src/extra.c")
file(REMOVE "${_w}/b-correct/third_party/SDL/build-scripts/run.sh")
_check("... nor a removed one" FAIL "files are not SDL ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" SCRATCH "${_w}/scratch-b")

_super(b-stale "${_p1}" PLAIN "${_s2}")
set(_stale_b "is a plain directory, not a checked-out submodule, and its files are not SDL ${_s1}: their git tree is ${_t2}, that commit's is ${_t1}")
_check("plain directory with another revision's files" FAIL
    "${_stale_b}.*move the directory aside.*submodule update --init third_party/SDL.*${_switch}"
    SOURCE "${_w}/b-stale/third_party/SDL" SUPER "${_w}/b-stale")
_check("... CNA_VENDORED_SOURCE_CHECK=WARN" WARN "${_stale_b}"
    SOURCE "${_w}/b-stale/third_party/SDL" SUPER "${_w}/b-stale" MODE WARN)

_check("gitlink without a matching record" FAIL
    "gitlink for third_party/SDL is ${_s1}, but cmake/VendoredSources.cmake records SDL ${_s2}.*record COMMIT ${_s1}"
    SOURCE "${_w}/b-correct/third_party/SDL" SUPER "${_w}/b-correct" COMMIT "${_s2}" TREE "${_t2}")

_super(b-modes "${_p1}" PLAIN "${_s1}")
file(CHMOD "${_w}/b-modes/third_party/SDL/build-scripts/run.sh"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ)
_check("a file mode differs" FAIL "files are not SDL ${_s1}"
    SOURCE "${_w}/b-modes/third_party/SDL" SUPER "${_w}/b-modes")
_check("... on a host without file modes it is a warning" WARN
    "files are not SDL ${_s1}.*On this host file modes"
    SOURCE "${_w}/b-modes/third_party/SDL" SUPER "${_w}/b-modes" LOSES_MODES ON)

# --- C: no git checkout of CNA around it (a source archive) ------------------------------------
# CNA_WORK_DIR may itself be inside CNA's checkout; that is still not a superproject of this tree.
_sdl_files("${_w}/archive/third_party/SDL" "${_s1}")
_check("source archive with the pinned files" PASS "has the files of ${_s1}"
    SOURCE "${_w}/archive/third_party/SDL" SUPER "${_w}/archive")
_sdl_files("${_w}/archive-stale/third_party/SDL" "${_s2}")
_check("source archive with other files" FAIL
    "is not part of a git checkout of CNA .a source archive.., and its files are not SDL ${_s1}.*Replace it with the sources of SDL ${_s1}"
    SOURCE "${_w}/archive-stale/third_party/SDL" SUPER "${_w}/archive-stale")
_check("source archive, nothing recorded" STATUS "no revision is recorded for SDL"
    SOURCE "${_w}/archive-stale/third_party/SDL" SUPER "${_w}/archive-stale" NO_RECORD)

# --- no git ------------------------------------------------------------------------------------
_check("no git" STATUS "git is not available .* cannot be verified"
    SOURCE "${_w}/a-stale/third_party/SDL" SUPER "${_w}/a-stale" GIT "${_w}/no-such-git")

# --- this checkout ---------------------------------------------------------------------------
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" -DCNA_CHECK_CASE=1 -DCHECK_REAL=1
        "-DCHECK_SCRATCH=${_w}/scratch-real" "-DCHECK_GIT=${_git_exe}"
        -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
set(_log "${_stdout}${_stderr}")
if(NOT _rc EQUAL 0 OR _log MATCHES "CMake Warning")
    _fail("this checkout" "third_party/SDL or third_party/SDL_mixer does not match cmake/VendoredSources.cmake")
else()
    message(STATUS "ok: this checkout's vendored SDL and SDL_mixer match cmake/VendoredSources.cmake")
endif()

if(_failures GREATER 0)
    message(FATAL_ERROR "VendoredSourceCheckCase: ${_failures} case(s) failed")
endif()
file(REMOVE_RECURSE "${_w}")
message(STATUS "VendoredSourceCheckCase: all cases passed")
