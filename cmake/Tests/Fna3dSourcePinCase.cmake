# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, finding C-01: CNA_FNA3D_GIT_REPOSITORY and CNA_FNA3D_GIT_TAG follow
# CNA's pin only while they are CNA's own defaults, move together, and never silently replace a
# developer's choice -- typed, untyped, edited in the cache, or left by an earlier CNA revision.
#
# Every case configures cmake/Tests/Fna3dSourcePinFixture in a build directory of its own and
# reads back the pair it ended with. Build trees left by earlier CNA revisions are reproduced by
# running those revisions' verbatim cache statements first. The current module is driven with
# made-up pins, so nothing here depends on today's pin; two cases use cmake/ThirdPartyFNA3D.cmake
# itself. No network access, no compiler.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> -P cmake/Tests/Fna3dSourcePinCase.cmake

cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Fna3dSourcePinCase.cmake requires -D${_required}=...")
    endif()
endforeach()

set(_fixture "${CNA_SOURCE_DIR}/cmake/Tests/Fna3dSourcePinFixture")
file(REMOVE_RECURSE "${CNA_WORK_DIR}")
file(MAKE_DIRECTORY "${CNA_WORK_DIR}")

set(_upstream "https://github.com/FNA-XNA/FNA3D.git")
set(_fork "https://github.com/libcna/FNA3D.git")
set(_tag_81ab "f4bef618d2c2e59866b02896b52efa25815c829b")
set(_tag_13d0 "a78cbed4a29e28a128480b954f2a5edffcbc6d1e")
set(_repo_a "https://example.invalid/cna/FNA3D.git")
set(_tag_a "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
set(_tag_b "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
set(_repo_c "https://example.invalid/cna-moved/FNA3D.git")
set(_tag_c "cccccccccccccccccccccccccccccccccccccccc")

set(_failures 0)

# _configure(<case-dir> <revision> <default-repo> <default-tag> [cmake args...])
# Sets _rc, _log, _repository and _tag (empty when the configure failed).
function(_configure case revision default_repository default_tag)
    set(_bin "${CNA_WORK_DIR}/${case}")
    file(REMOVE "${_bin}/fna3d-pin.txt")
    set(_generator "")
    if(DEFINED CNA_GENERATOR)
        set(_generator -G "${CNA_GENERATOR}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_fixture}" -B "${_bin}" ${_generator}
            "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}"
            "-DFIXTURE_REVISION=${revision}"
            "-DFIXTURE_DEFAULT_REPOSITORY=${default_repository}"
            "-DFIXTURE_DEFAULT_TAG=${default_tag}"
            ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr)
    set(_repository "")
    set(_tag "")
    if(_result EQUAL 0 AND EXISTS "${_bin}/fna3d-pin.txt")
        file(STRINGS "${_bin}/fna3d-pin.txt" _lines)
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "^repository=(.*)$")
                set(_repository "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "^tag=(.*)$")
                set(_tag "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "^default-repository=(.*)$")
                set(_recorded_repository "${CMAKE_MATCH_1}")
            elseif(_line MATCHES "^default-tag=(.*)$")
                set(_recorded_tag "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    string(REGEX REPLACE "\n[ ]+" " " _flat "${_stdout}${_stderr}")
    set(_rc "${_result}" PARENT_SCOPE)
    set(_log "${_stdout}${_stderr}" PARENT_SCOPE)
    set(_flat_log "${_flat}" PARENT_SCOPE)
    set(_repository "${_repository}" PARENT_SCOPE)
    set(_tag "${_tag}" PARENT_SCOPE)
    set(_recorded_repository "${_recorded_repository}" PARENT_SCOPE)
    set(_recorded_tag "${_recorded_tag}" PARENT_SCOPE)
endfunction()

macro(_fail label text)
    message(SEND_ERROR "Fna3dSourcePinCase: ${label}: ${text}\n--- configure log ---\n${_log}")
    math(EXPR _failures "${_failures} + 1")
endmacro()

# _expect(<label> PIN <repo> <tag> | FAILS <regex>
#         [WARNS <regex>] [QUIET] [SAYS <regex>] [SILENT <regex>])
#   QUIET: no message(WARNING) at all.  SAYS/SILENT: the log does / does not match.
macro(_expect label)
    cmake_parse_arguments(_E "QUIET" "FAILS;WARNS;SAYS;SILENT" "PIN" ${ARGN})
    if(DEFINED _E_PIN)
        list(GET _E_PIN 0 _want_repository)
        list(GET _E_PIN 1 _want_tag)
        if(NOT _rc EQUAL 0)
            _fail("${label}" "expected ${_want_repository} at ${_want_tag}, the configure failed")
        elseif(NOT _repository STREQUAL _want_repository OR NOT _tag STREQUAL _want_tag)
            _fail("${label}" "expected ${_want_repository} at ${_want_tag}, got ${_repository} at ${_tag}")
        else()
            message(STATUS "ok: ${label}: ${_repository} at ${_tag}")
        endif()
    endif()
    if(DEFINED _E_FAILS)
        if(_rc EQUAL 0)
            _fail("${label}" "expected the configure to stop, it chose ${_repository} at ${_tag}")
        elseif(NOT _flat_log MATCHES "${_E_FAILS}")
            _fail("${label}" "the configure stopped without saying '${_E_FAILS}'")
        else()
            message(STATUS "ok: ${label}: refused")
        endif()
    endif()
    if(DEFINED _E_WARNS AND (NOT _log MATCHES "CMake Warning at" OR NOT _flat_log MATCHES "${_E_WARNS}"))
        _fail("${label}" "expected a warning matching '${_E_WARNS}'")
    endif()
    if(_E_QUIET AND _log MATCHES "CMake Warning at")
        _fail("${label}" "expected no warning")
    endif()
    if(DEFINED _E_SAYS AND NOT _flat_log MATCHES "${_E_SAYS}")
        _fail("${label}" "expected the log to say '${_E_SAYS}'")
    endif()
    if(DEFINED _E_SILENT AND _flat_log MATCHES "${_E_SILENT}")
        _fail("${label}" "expected the log not to say '${_E_SILENT}'")
    endif()
endmacro()

set(_follows "FNA3D pin follows CNA's current default")
set(_not_pin "not CNA's pin")

# --- fresh configure ---------------------------------------------------------------------------
_configure(fresh module "${_repo_a}" "${_tag_a}")
_expect("fresh configure takes CNA's pin" PIN "${_repo_a}" "${_tag_a}" QUIET SILENT "${_follows}")
file(STRINGS "${CNA_WORK_DIR}/fresh/fna3d-pin.txt" _entries REGEX "-(type|help)=")
set(_want_entries
    "repository-type=STRING" "tag-type=STRING"
    "repository-help=FNA3D repository fetched for the FNA3D graphics renderer (CNA's fork)"
    "tag-help=Pinned FNA3D commit used by the FNA3D graphics renderer")
list(SORT _entries)
list(SORT _want_entries)
if(NOT _entries STREQUAL _want_entries)
    _fail("fresh configure" "the public cache entries changed type or docstring: ${_entries}")
endif()
_configure(fresh module "${_repo_a}" "${_tag_a}")
_expect("repeated configure is stable" PIN "${_repo_a}" "${_tag_a}" QUIET SILENT "${_follows}")
_configure(fresh module "${_repo_a}" "${_tag_b}")
_expect("CNA's own default follows a pin bump" PIN "${_repo_a}" "${_tag_b}" QUIET
    SAYS "CNA_FNA3D_GIT_TAG ${_tag_a} -> ${_tag_b}")

# --- trees left by earlier CNA revisions -------------------------------------------------------
_configure(prefork prefork "" "")
_expect("pre-fork revision" PIN "${_upstream}" "3240147")
_configure(prefork module "${_repo_a}" "${_tag_a}")
_expect("pre-fork tree: both members follow together" PIN "${_repo_a}" "${_tag_a}" QUIET
    SAYS "CNA_FNA3D_GIT_REPOSITORY ${_upstream} -> ${_repo_a}, CNA_FNA3D_GIT_TAG 3240147 -> ${_tag_a}")
_configure(prefork module "${_repo_a}" "${_tag_a}")
_expect("pre-fork tree, configured again: no second migration" PIN "${_repo_a}" "${_tag_a}" QUIET
    SILENT "${_follows}")

_configure(rev81ab 81ab03e72 "" "")
_expect("81ab03e72 revision" PIN "${_fork}" "${_tag_81ab}")
_configure(rev81ab module "${_repo_a}" "${_tag_a}")
_expect("81ab03e72 tree follows" PIN "${_repo_a}" "${_tag_a}" QUIET SAYS "${_follows}")

_configure(rev13d0 13d0ed91a "" "")
_expect("13d0ed91a revision" PIN "${_fork}" "${_tag_13d0}")
_configure(rev13d0 module "${_fork}" "${_tag_13d0}")
_expect("13d0ed91a tree, same pin: unchanged and quiet" PIN "${_fork}" "${_tag_13d0}" QUIET
    SILENT "${_follows}")
_configure(rev13d0 module "${_fork}" "${_tag_b}")
_expect("13d0ed91a tree, pin bump: follows" PIN "${_fork}" "${_tag_b}" QUIET
    SAYS "CNA_FNA3D_GIT_TAG ${_tag_13d0} -> ${_tag_b}")

# The real module, whatever its pin is today.
_configure(real-fresh real "" "")
_expect("cmake/ThirdPartyFNA3D.cmake, fresh" PIN "${_recorded_repository}" "${_recorded_tag}" QUIET)
if(_recorded_tag STREQUAL "")
    _fail("cmake/ThirdPartyFNA3D.cmake, fresh" "no default recorded")
endif()
_configure(real-13d0 13d0ed91a "" "")
_configure(real-13d0 real "" "")
_expect("cmake/ThirdPartyFNA3D.cmake over a 13d0ed91a tree" PIN "${_recorded_repository}" "${_recorded_tag}" QUIET)

# --- an explicit upstream repository with CNA's (fork-only) commit -----------------------------
_configure(upstream-untyped module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_REPOSITORY=${_upstream}")
_expect("untyped upstream repository with CNA's commit" FAILS "is upstream FNA3D.*exists only in ${_repo_a}.*-DCNA_FNA3D_GIT_TAG:STRING=")
_configure(upstream-untyped module "${_repo_a}" "${_tag_a}")
_expect("... stays refused on the next configure (the override is remembered)" FAILS "is upstream FNA3D")
_configure(upstream-untyped module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_TAG:STRING=26.10")
_expect("... naming an upstream tag makes the pair the developer's" PIN "${_upstream}" "26.10"
    WARNS "${_not_pin}")
_configure(upstream-untyped module "${_repo_a}" "${_tag_b}")
_expect("... and it survives a pin bump" PIN "${_upstream}" "26.10" WARNS "${_not_pin}")
_configure(upstream-untyped module "${_repo_a}" "${_tag_b}"
    -U CNA_FNA3D_GIT_REPOSITORY -U CNA_FNA3D_GIT_TAG)
_expect("... -U returns both to the pin" PIN "${_repo_a}" "${_tag_b}" QUIET)

_configure(upstream-typed module "${_repo_a}" "${_tag_a}"
    "-DCNA_FNA3D_GIT_REPOSITORY:STRING=git@github.com:FNA-XNA/FNA3D")
_expect("typed upstream repository (ssh spelling) with CNA's commit" FAILS "is upstream FNA3D")
_configure(upstream-fork module "${_fork}" "${_tag_a}"
    "-DCNA_FNA3D_GIT_REPOSITORY:STRING=https://github.com/fna-xna/fna3d")
_expect("upstream repository, other spelling" FAILS "is upstream FNA3D")

# --- an explicit old commit ---------------------------------------------------------------------
_configure(old-sha module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_TAG:STRING=${_tag_81ab}")
_expect("typed old SHA is kept" PIN "${_repo_a}" "${_tag_81ab}" WARNS "${_not_pin}.*CNA_FNA3D_GIT_TAG is your override")
_configure(old-sha module "${_repo_a}" "${_tag_b}")
_expect("... and survives a pin bump" PIN "${_repo_a}" "${_tag_81ab}" WARNS "${_not_pin}")

# A value equal to an old default, set by hand in a tree an intermediate revision left.
_configure(old-default-by-hand 13d0ed91a "" "")
_configure(old-default-by-hand module "${_fork}" "${_tag_13d0}" "-DCNA_FNA3D_GIT_TAG=${_tag_81ab}")
_expect("untyped value equal to an old default is the developer's" PIN "${_fork}" "${_tag_81ab}"
    WARNS "${_not_pin}")
_configure(old-default-by-hand module "${_fork}" "${_tag_13d0}")
_expect("... and stays so on the next configure" PIN "${_fork}" "${_tag_81ab}" WARNS "${_not_pin}"
    SILENT "${_follows}")

# Both members of the pre-fork default, typed on purpose in a fresh tree.
_configure(old-pair-by-hand module "${_repo_a}" "${_tag_a}"
    "-DCNA_FNA3D_GIT_REPOSITORY:STRING=${_upstream}" "-DCNA_FNA3D_GIT_TAG:STRING=3240147")
_expect("typed pre-fork pair is the developer's" PIN "${_upstream}" "3240147" WARNS "${_not_pin}")
_configure(old-pair-by-hand module "${_repo_a}" "${_tag_a}")
_expect("... and is not migrated later" PIN "${_upstream}" "3240147" SILENT "${_follows}")

# --- both, only the repository, only the tag ---------------------------------------------------
_configure(both module "${_repo_a}" "${_tag_a}"
    "-DCNA_FNA3D_GIT_REPOSITORY=/local/developer.git" "-DCNA_FNA3D_GIT_TAG=topic")
_expect("both overridden (untyped)" PIN "/local/developer.git" "topic" WARNS "${_not_pin}")
_configure(both module "${_repo_c}" "${_tag_c}")
_expect("... kept when CNA's pin moves" PIN "/local/developer.git" "topic" WARNS "${_not_pin}")

_configure(only-repository module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_REPOSITORY=/local/developer.git")
_expect("only the repository" PIN "/local/developer.git" "${_tag_a}"
    WARNS "${_not_pin}.*CNA_FNA3D_GIT_REPOSITORY is your override")
_configure(only-repository module "${_repo_a}" "${_tag_a}")
_expect("... stays on the next configure" PIN "/local/developer.git" "${_tag_a}")
_configure(only-repository module "${_repo_a}" "${_tag_b}")
_expect("... a pin bump does not move CNA's half alone"
    FAILS "CNA_FNA3D_GIT_REPOSITORY=/local/developer.git is your override.*CNA_FNA3D_GIT_TAG=${_tag_a} is the default an earlier CNA revision chose.*-DCNA_FNA3D_GIT_TAG:STRING=.*-U CNA_FNA3D_GIT_REPOSITORY")
_configure(only-repository module "${_repo_a}" "${_tag_b}" -U CNA_FNA3D_GIT_REPOSITORY)
_expect("... -U of the override returns to the pin" PIN "${_repo_a}" "${_tag_b}" QUIET)

_configure(only-repository-named module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_REPOSITORY=/local/developer.git")
_configure(only-repository-named module "${_repo_a}" "${_tag_b}" "-DCNA_FNA3D_GIT_TAG:STRING=mine")
_expect("... or naming the other half makes the pair the developer's" PIN "/local/developer.git" "mine"
    WARNS "${_not_pin}")

_configure(only-tag module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_TAG=cna")
_expect("only the tag" PIN "${_repo_a}" "cna" WARNS "${_not_pin}")
_configure(only-tag module "${_repo_a}" "${_tag_b}")
_expect("... kept across a tag bump" PIN "${_repo_a}" "cna" WARNS "${_not_pin}")
_configure(only-tag module "${_repo_c}" "${_tag_c}")
_expect("... a repository move does not move CNA's half alone"
    FAILS "CNA_FNA3D_GIT_TAG=cna is your override.*CNA_FNA3D_GIT_REPOSITORY=${_repo_a} is the default an earlier CNA revision chose")

# The developer's half equals CNA's current default: the other half follows.
_configure(explicit-current prefork "" "")
_configure(explicit-current module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_REPOSITORY:STRING=${_repo_a}")
_expect("explicit current default beside an obsolete default" PIN "${_repo_a}" "${_tag_a}" QUIET
    SAYS "CNA_FNA3D_GIT_TAG 3240147 -> ${_tag_a}")

# A pre-fork tree with a tag of the developer's own (C-01: became fork + 26.09).
_configure(prefork-custom-tag prefork "" "" "-DCNA_FNA3D_GIT_TAG=26.09")
_expect("pre-fork revision with a custom tag" PIN "${_upstream}" "26.09")
_configure(prefork-custom-tag module "${_repo_a}" "${_tag_a}")
_expect("pre-fork tree with a custom tag is refused, not half-migrated"
    FAILS "CNA_FNA3D_GIT_TAG=26.09 is your override.*CNA_FNA3D_GIT_REPOSITORY=${_upstream} is the upstream default CNA chose before the libcna forks")

# --- FETCHCONTENT_SOURCE_DIR_FNA3D: nothing is fetched, refusals become warnings --------------
_configure(source-override prefork "" "" "-DCNA_FNA3D_GIT_TAG=26.09")
_configure(source-override module "${_repo_a}" "${_tag_a}"
    "-DFETCHCONTENT_SOURCE_DIR_FNA3D=${CNA_WORK_DIR}/no-such-fna3d")
_expect("mixed pair with a source override: warned, cache left alone" PIN "${_upstream}" "26.09"
    WARNS "is your override.*nothing is fetched")
_configure(source-override module "${_repo_a}" "${_tag_a}" -U FETCHCONTENT_SOURCE_DIR_FNA3D)
_expect("... still recognised once the override is gone" FAILS "CNA_FNA3D_GIT_TAG=26.09 is your override")
_configure(source-override-upstream module "${_repo_a}" "${_tag_a}"
    "-DFETCHCONTENT_SOURCE_DIR_FNA3D=${CNA_WORK_DIR}/no-such-fna3d"
    "-DCNA_FNA3D_GIT_REPOSITORY=${_upstream}")
_expect("upstream repository with CNA's commit and a source override: warned" PIN "${_upstream}" "${_tag_a}"
    WARNS "is upstream FNA3D")

# --- an untyped -D in a tree an intermediate revision left (C-01: overwritten next time) -------
_configure(untyped-81ab 81ab03e72 "" "" "-DCNA_FNA3D_GIT_REPOSITORY=${_upstream}")
_expect("81ab03e72 with an untyped upstream repository" PIN "${_upstream}" "${_tag_81ab}")
_configure(untyped-81ab module "${_fork}" "${_tag_13d0}")
_expect("... is the developer's under the current module, not silently replaced"
    FAILS "CNA_FNA3D_GIT_REPOSITORY=${_upstream} is your override")

# --- an untyped -D under the current module stays an override ----------------------------------
_configure(untyped module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_TAG=deadbeef")
_expect("untyped tag" PIN "${_repo_a}" "deadbeef" WARNS "${_not_pin}")
file(STRINGS "${CNA_WORK_DIR}/untyped/fna3d-pin.txt" _entries REGEX "^tag-(type|help)=")
if(NOT _entries STREQUAL "tag-type=STRING;tag-help=Pinned FNA3D commit used by the FNA3D graphics renderer")
    _fail("untyped tag" "the entry did not become a STRING with CNA's docstring: ${_entries}")
endif()
_configure(untyped module "${_repo_a}" "${_tag_a}")
_expect("... remains the developer's although it now carries CNA's docstring" PIN "${_repo_a}" "deadbeef"
    WARNS "${_not_pin}")
_configure(untyped module "${_repo_a}" "${_tag_b}")
_expect("... and across a pin bump" PIN "${_repo_a}" "deadbeef")
_configure(untyped module "${_repo_a}" "${_tag_b}" -U CNA_FNA3D_GIT_TAG)
_expect("... -U returns to the pin" PIN "${_repo_a}" "${_tag_b}" QUIET)
_configure(untyped module "${_repo_a}" "${_tag_c}")
_expect("... after which it follows the pin again" PIN "${_repo_a}" "${_tag_c}" QUIET SAYS "${_follows}")

# Naming CNA's current pin explicitly is a choice too: it stays when the pin moves.
_configure(explicit-pin module "${_repo_a}" "${_tag_a}")
_configure(explicit-pin module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_TAG=${_tag_a}")
_expect("-D naming the current pin in an existing tree" PIN "${_repo_a}" "${_tag_a}" QUIET)
_configure(explicit-pin module "${_repo_a}" "${_tag_b}")
_expect("... is kept, visibly, when the pin moves" PIN "${_repo_a}" "${_tag_a}"
    WARNS "CNA_FNA3D_GIT_TAG is your override .your override from an earlier configure.")
_configure(explicit-pin-typed module "${_repo_a}" "${_tag_a}")
_configure(explicit-pin-typed module "${_repo_a}" "${_tag_a}" "-DCNA_FNA3D_GIT_TAG:STRING=${_tag_a}")
_configure(explicit-pin-typed module "${_repo_a}" "${_tag_b}")
_expect("... typed as well" PIN "${_repo_a}" "${_tag_a}" WARNS "${_not_pin}")

# --- a ccmake / cmake-gui edit: STRING, CNA's docstring, another value --------------------------
_configure(ccmake module "${_repo_a}" "${_tag_a}")
file(READ "${CNA_WORK_DIR}/ccmake/CMakeCache.txt" _cache)
string(REPLACE "CNA_FNA3D_GIT_TAG:STRING=${_tag_a}" "CNA_FNA3D_GIT_TAG:STRING=feature" _edited "${_cache}")
if(_edited STREQUAL _cache)
    _fail("ccmake edit" "could not edit the fixture's cache")
endif()
file(WRITE "${CNA_WORK_DIR}/ccmake/CMakeCache.txt" "${_edited}")
_configure(ccmake module "${_repo_a}" "${_tag_a}")
_expect("ccmake edit is the developer's" PIN "${_repo_a}" "feature"
    WARNS "CNA_FNA3D_GIT_TAG is your override .not CNA's default for this tree, ${_tag_a}.")
_configure(ccmake module "${_repo_a}" "${_tag_b}")
_expect("... and survives a pin bump" PIN "${_repo_a}" "feature" WARNS "${_not_pin}")

# --- the revision copies are verbatim ----------------------------------------------------------
find_program(_git git)
if(_git)
    foreach(_rev IN ITEMS 81ab03e72 13d0ed91a 4bc4aa4bc)
        execute_process(COMMAND "${_git}" -C "${CNA_SOURCE_DIR}" show "${_rev}:cmake/ThirdPartyFNA3D.cmake"
            RESULT_VARIABLE _show_rc OUTPUT_VARIABLE _historic ERROR_QUIET)
        if(NOT _show_rc EQUAL 0)
            message(STATUS "skip: ${_rev} is not in this checkout's history; its copy is not compared")
            continue()
        endif()
        if(_rev STREQUAL "4bc4aa4bc")
            set(_copy "${_fixture}/revision-prefork.cmake")
        else()
            set(_copy "${_fixture}/revision-${_rev}.cmake")
        endif()
        file(READ "${_copy}" _copied)
        string(FIND "${_historic}" "${_copied}" _at)
        if(_at LESS 0)
            _fail("verbatim copies" "${_copy} is not a verbatim excerpt of ${_rev}:cmake/ThirdPartyFNA3D.cmake")
        else()
            message(STATUS "ok: ${_copy} is verbatim from ${_rev}")
        endif()
    endforeach()
endif()

if(_failures GREATER 0)
    message(FATAL_ERROR "Fna3dSourcePinCase: ${_failures} case(s) failed")
endif()
file(REMOVE_RECURSE "${CNA_WORK_DIR}")
message(STATUS "Fna3dSourcePinCase: all cases passed")
