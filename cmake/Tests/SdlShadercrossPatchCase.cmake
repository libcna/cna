# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, finding C-04: CNA's SDL_shadercross patch is applied with git's
# repository discovery stopped at the source tree and its result read back from the file, and a
# FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE checkout is never patched in place -- a patched
# copy is staged in the build tree.
#
# Fixture source trees carry the patch's own pre-image (taken from the real patch file), placed
# inside another git repository (the case where `git apply` used to exit 0 having changed nothing),
# tracked by it, and as a repository of their own. No network.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> -P cmake/Tests/SdlShadercrossPatchCase.cmake

cmake_minimum_required(VERSION 3.20)

set(_patch "${CNA_SOURCE_DIR}/cmake/patches/sdl-shadercross-1ff05bec-disable-unused-export.patch")
set(_script "${CNA_SOURCE_DIR}/cmake/patches/apply-sdl-shadercross-patch.cmake")

if(DEFINED CNA_STAGE_CASE)
    include("${CNA_SOURCE_DIR}/cmake/SdlShadercrossPatch.cmake")
    cna_sdl_shadercross_stage_source(_staged SOURCE "${STAGE_SOURCE}"
        DESTINATION "${STAGE_DESTINATION}" PATCH "${_patch}")
    message(STATUS "staged-at=${_staged}")
    return()
endif()

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "SdlShadercrossPatchCase.cmake requires -D${_required}=...")
    endif()
endforeach()
find_program(_git_exe git)
if(NOT _git_exe)
    message(STATUS "SdlShadercrossPatchCase: git not found -- nothing to test")
    return()
endif()
include("${CNA_SOURCE_DIR}/cmake/SdlShadercrossPatch.cmake")

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
        message(FATAL_ERROR "SdlShadercrossPatchCase: git ${ARGN} in ${dir} failed:\n${_out}${_err}")
    endif()
    set(_git_out "${_out}" PARENT_SCOPE)
endfunction()

# The fixture CMakeLists.txt: the patch's pre-image between lines of padding.
_cna_sdl_shadercross_patch_images(_pre _post "${_patch}")
list(LENGTH _pre _hunks)
if(NOT _hunks EQUAL 1)
    message(FATAL_ERROR "SdlShadercrossPatchCase: expected one hunk in ${_patch}, found ${_hunks}")
endif()
foreach(_name IN ITEMS _pre _post)
    string(REPLACE "<cna-semicolon>" ";" ${_name} "${${_name}}")
    string(REPLACE "<cna-open>" "[" ${_name} "${${_name}}")
    string(REPLACE "<cna-close>" "]" ${_name} "${${_name}}")
endforeach()
if(NOT _post MATCHES "if\\(SDLSHADERCROSS_INSTALL\\)" OR _pre MATCHES "if\\(SDLSHADERCROSS_INSTALL\\)")
    message(FATAL_ERROR "SdlShadercrossPatchCase: the patch no longer guards the export; update this test")
endif()
set(_padding "")
foreach(_i RANGE 1 230)
    string(APPEND _padding "# line ${_i}\n")
endforeach()
set(_unpatched "${_padding}${_pre}# tail\n")
set(_patched "${_padding}${_post}# tail\n")

function(_fixture dir content)
    file(WRITE "${dir}/CMakeLists.txt" "${content}")
    file(WRITE "${dir}/src/SDL_shadercross.c" "int SDL_ShaderCross_Init(void) { return 1; }\n")
    file(WRITE "${dir}/include/SDL3_shadercross/SDL_shadercross.h" "int SDL_ShaderCross_Init(void);\n")
endfunction()

set(_failures 0)
macro(_fail label text)
    message(SEND_ERROR "SdlShadercrossPatchCase: ${label}: ${text}\n--- log ---\n${_log}")
    math(EXPR _failures "${_failures} + 1")
endmacro()

# _apply(<label> <dir> <PATCHED|FAIL> <regex>): the PATCH_COMMAND script, run in <dir>.
macro(_apply label dir want regex)
    execute_process(
        COMMAND ${_git_env} "${CMAKE_COMMAND}" "-DCNA_SDL_SHADERCROSS_PATCH_FILE=${_patch}" -P "${_script}"
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    set(_log "${_stdout}${_stderr}")
    string(REGEX REPLACE "\n[ ]+" " " _flat "${_log}")
    file(READ "${dir}/CMakeLists.txt" _now)
    if("${want}" STREQUAL "FAIL")
        if(_rc EQUAL 0)
            _fail("${label}" "expected a refusal")
        elseif(NOT _flat MATCHES "${regex}")
            _fail("${label}" "the refusal does not say '${regex}'")
        else()
            message(STATUS "ok: ${label}")
        endif()
    elseif(NOT _rc EQUAL 0)
        _fail("${label}" "the script failed")
    elseif(NOT _now STREQUAL _patched)
        _fail("${label}" "CMakeLists.txt is not the patch's result")
    elseif(NOT _flat MATCHES "${regex}")
        _fail("${label}" "the output does not say '${regex}'")
    else()
        message(STATUS "ok: ${label}")
    endif()
endmacro()

# --- applying in place (FetchContent's own clone) ----------------------------------------------
# An archive inside another git repository that does not track it: where `git apply` used to exit
# 0 having changed nothing, and the script said it had applied the patch.
set(_outer "${_w}/outer")
file(WRITE "${_outer}/README" "outer repository\n")
_git("${_outer}" init -q)
_git("${_outer}" add README)
_git("${_outer}" commit -q -m outer)
_fixture("${_outer}/vendor/untracked" "${_unpatched}")
_apply("archive inside an unrelated git repository" "${_outer}/vendor/untracked" PATCHED "CNA: applied")
_apply("... a second run leaves it alone" "${_outer}/vendor/untracked" PATCHED "already applied")

_fixture("${_outer}/vendor/tracked" "${_unpatched}")
_git("${_outer}" add vendor/tracked)
_git("${_outer}" commit -q -m "track the archive")
_apply("archive tracked by an enclosing repository" "${_outer}/vendor/tracked" PATCHED "CNA: applied")

_fixture("${_w}/clone" "${_unpatched}")
_git("${_w}/clone" init -q)
_git("${_w}/clone" add -A)
_git("${_w}/clone" commit -q -m upstream)
_apply("a repository of its own (FetchContent's clone)" "${_w}/clone" PATCHED "CNA: applied")
_git("${_w}/clone" status --porcelain)
if(NOT _git_out STREQUAL " M CMakeLists.txt")
    _fail("clone" "expected exactly CMakeLists.txt modified, got '${_git_out}'")
endif()

_fixture("${_w}/neither" "${_padding}# something else entirely\n")
_apply("neither side of the patch" "${_w}/neither" FAIL "matches neither side")

# --- a FETCHCONTENT_SOURCE_DIR checkout: staged, never patched in place ---------------------------
# _stage(<label> <source> <destination> <OK|FAIL> <regex>)
macro(_stage label source destination want regex)
    execute_process(
        COMMAND ${_git_env} "${CMAKE_COMMAND}" "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" -DCNA_STAGE_CASE=1
            "-DSTAGE_SOURCE=${source}" "-DSTAGE_DESTINATION=${destination}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    set(_log "${_stdout}${_stderr}")
    string(REGEX REPLACE "\n[ ]+" " " _flat "${_log}")
    if("${want}" STREQUAL "FAIL")
        if(_rc EQUAL 0)
            _fail("${label}" "expected a refusal")
        elseif(NOT _flat MATCHES "${regex}")
            _fail("${label}" "the refusal does not say '${regex}'")
        else()
            message(STATUS "ok: ${label}")
        endif()
    elseif(NOT _rc EQUAL 0 OR NOT _flat MATCHES "staged-at=${destination}")
        _fail("${label}" "staging failed")
    elseif(NOT _flat MATCHES "${regex}")
        _fail("${label}" "the output does not say '${regex}'")
    else()
        message(STATUS "ok: ${label}")
    endif()
endmacro()

set(_override "${_w}/override")
_fixture("${_override}" "${_unpatched}")
file(WRITE "${_override}/external/SPIRV-Tools/huge.txt" "not needed by CNA's build\n")
_git("${_override}" init -q)
_git("${_override}" add -A)
_git("${_override}" commit -q -m upstream)
set(_staged "${_w}/build/_deps/cna_sdl_shadercross_source-staged")
_stage("an unpatched override is staged and patched" "${_override}" "${_staged}" OK
    "staged a patched copy")
file(READ "${_override}/CMakeLists.txt" _override_now)
_git("${_override}" status --porcelain)
if(NOT _override_now STREQUAL _unpatched OR NOT _git_out STREQUAL "")
    _fail("override" "the developer's checkout was modified")
endif()
file(READ "${_staged}/CMakeLists.txt" _staged_now)
if(NOT _staged_now STREQUAL _patched)
    _fail("override" "the staged copy is not patched")
endif()
if(EXISTS "${_staged}/.git" OR EXISTS "${_staged}/external")
    _fail("override" "the staged copy has .git or external/")
endif()
file(TIMESTAMP "${_staged}/CMakeLists.txt" _stamp_before "%s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
_stage("... an unchanged override is not staged again" "${_override}" "${_staged}" OK "staged-at=")
file(TIMESTAMP "${_staged}/CMakeLists.txt" _stamp_after "%s")
if(_flat MATCHES "staged a patched copy" OR NOT _stamp_before STREQUAL _stamp_after)
    _fail("override" "an unchanged override rewrote the staged copy")
endif()
file(APPEND "${_override}/src/SDL_shadercross.c" "/* developer change */\n")
_stage("... a changed override is staged again" "${_override}" "${_staged}" OK "staged a patched copy")
file(READ "${_staged}/src/SDL_shadercross.c" _staged_source)
if(NOT _staged_source MATCHES "developer change")
    _fail("override" "the restaged copy misses the developer's change")
endif()
_stage("... an override that names the staged copy itself" "${_staged}" "${_staged}" OK "already applied")
if(NOT EXISTS "${_staged}/src/SDL_shadercross.c")
    _fail("override" "staging into itself removed the copy")
endif()

_fixture("${_w}/override-patched" "${_patched}")
_stage("an override the developer patched already" "${_w}/override-patched" "${_w}/build/staged-2" OK
    "already applied")

_fixture("${_w}/override-other" "${_padding}# something else entirely\n")
_stage("an override matching neither side" "${_w}/override-other" "${_w}/build/staged-3" FAIL
    "matches neither side")
if(EXISTS "${_w}/build/staged-3")
    _fail("override" "a refused staging left a copy behind")
endif()

# FetchContent takes the function-scoped normal variable cna_configure_sdl_shadercross() sets over
# the developer's cache entry, and leaves that entry as the developer set it.
set(_consumer "${_w}/consumer")
file(WRITE "${_w}/consumer-dep/developer/CMakeLists.txt" "message(STATUS \"fixture-dep=developer\")\n")
file(WRITE "${_w}/consumer-dep/staged/CMakeLists.txt" "message(STATUS \"fixture-dep=staged\")\n")
file(WRITE "${_consumer}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)
project(CnaShadercrossOverrideConsumer NONE)
include(FetchContent)
FetchContent_Declare(fixture_dep GIT_REPOSITORY \"${_w}/no-such-repository\" GIT_TAG main)
function(use_staged)
    set(FETCHCONTENT_SOURCE_DIR_FIXTURE_DEP \"${_w}/consumer-dep/staged\")
    FetchContent_MakeAvailable(fixture_dep)
endfunction()
use_staged()
message(STATUS \"fixture-cache=\$CACHE{FETCHCONTENT_SOURCE_DIR_FIXTURE_DEP}\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_consumer}" -B "${_w}/consumer-build"
        "-DFETCHCONTENT_SOURCE_DIR_FIXTURE_DEP=${_w}/consumer-dep/developer"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
set(_log "${_stdout}${_stderr}")
if(NOT _rc EQUAL 0 OR NOT _log MATCHES "fixture-dep=staged"
   OR NOT _log MATCHES "fixture-cache=${_w}/consumer-dep/developer")
    _fail("FetchContent override" "the staged copy was not used, or the cache entry changed")
else()
    message(STATUS "ok: FetchContent builds the staged copy and keeps the developer's override")
endif()

if(_failures GREATER 0)
    message(FATAL_ERROR "SdlShadercrossPatchCase: ${_failures} case(s) failed")
endif()
file(REMOVE_RECURSE "${_w}")
message(STATUS "SdlShadercrossPatchCase: all cases passed")
