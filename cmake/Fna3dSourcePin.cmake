# SPDX-License-Identifier: MS-PL
#
# Which FNA3D repository and commit a build tree uses: CNA_FNA3D_GIT_REPOSITORY and
# CNA_FNA3D_GIT_TAG, decided as one pair.
#
# A build tree outlives the CNA revision that configured it, so its cache can hold defaults an
# earlier CNA chose. Those follow CNA's current pin; a developer's own choice never does. The value
# alone cannot tell the two apart -- a developer may name an old default on purpose, and an untyped
# -D is given CNA's docstring as soon as set(CACHE) has run -- so each member's provenance is
# recorded beside it:
#
#   _CNA_FNA3D_GIT_<member>_ORIGIN   cna | user
#   _CNA_FNA3D_DEFAULT_GIT_<member>  CNA's default this tree last had (also written by 81ab03e72
#                                    and 13d0ed91a, the two revisions before this file)
#
# A member is the developer's (USER) when it was set on the command line this run (type
# UNINITIALIZED, or the "No help, variable specified on the command line." docstring a typed -D or
# a preset leaves), when its recorded origin says so, or when its value is not the default recorded
# for the tree (an edit in ccmake or cmake-gui). Otherwise it is CNA's: current, or obsolete when it
# is an earlier default -- including a tree from before the forks, recognised by the upstream value
# *and* the docstring every revision before 81ab03e72 wrote.
#
# The pair moves as one. Both CNA's: both follow the current pin. Both the developer's: both kept.
# A developer's member beside an obsolete CNA member is refused, because neither moving nor keeping
# the obsolete half is what anyone asked for -- unless the developer's value is CNA's current
# default anyway, in which case the other half simply follows. Upstream FNA-XNA/FNA3D paired with
# CNA's default commit is refused too: that commit exists only in the fork. With
# FETCHCONTENT_SOURCE_DIR_FNA3D set nothing is fetched, so both refusals are warnings there and the
# cache is left as it is. Any pair other than CNA's pin is warned about on every configure, so an
# override stays visible. Nothing here touches the network.
#
# Included by cmake/ThirdPartyFNA3D.cmake; cmake/Tests/Fna3dSourcePinCase.cmake drives it with
# defaults of its own.

include_guard(GLOBAL)

# The docstrings CNA writes, and what every revision before 81ab03e72 (2026-10-25) wrote, value and
# docstring alike. A macro, so each function below has them whichever directory included this file.
macro(_cna_fna3d_pin_constants)
    set(_CNA_FNA3D_PIN_DOC_REPOSITORY
        "FNA3D repository fetched for the FNA3D graphics renderer (CNA's fork)")
    set(_CNA_FNA3D_PIN_DOC_TAG "Pinned FNA3D commit used by the FNA3D graphics renderer")
    set(_CNA_FNA3D_PIN_PREFORK_REPOSITORY "https://github.com/FNA-XNA/FNA3D.git")
    set(_CNA_FNA3D_PIN_PREFORK_DOC_REPOSITORY
        "Upstream FNA3D repository fetched for the FNA3D graphics renderer")
    set(_CNA_FNA3D_PIN_PREFORK_TAG "3240147")
    set(_CNA_FNA3D_PIN_PREFORK_DOC_TAG "Pinned FNA3D revision used by the FNA3D graphics renderer")
    set(_CNA_FNA3D_PIN_COMMAND_LINE_HELP "No help, variable specified on the command line.")
endmacro()

# Upstream FNA3D in any of the spellings GitHub serves it under.
function(_cna_fna3d_pin_is_upstream out_var url)
    string(TOLOWER "${url}" _url)
    if(_url MATCHES
       "^(https?://(www\\.)?github\\.com/|git@github\\.com:|ssh://git@github\\.com/|git://github\\.com/)fna-xna/fna3d(\\.git)?/?$")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Sets <prefix>_CLASS (ABSENT, USER, CNA_CURRENT or CNA_OBSOLETE), <prefix>_VALUE and <prefix>_WHY.
function(_cna_fna3d_pin_classify prefix member default)
    _cna_fna3d_pin_constants()
    set(_var "CNA_FNA3D_GIT_${member}")
    if(NOT DEFINED "CACHE{${_var}}")
        set(${prefix}_CLASS ABSENT PARENT_SCOPE)
        set(${prefix}_VALUE "${default}" PARENT_SCOPE)
        set(${prefix}_WHY "not set" PARENT_SCOPE)
        return()
    endif()
    set(_value "$CACHE{${_var}}")
    set(${prefix}_VALUE "${_value}" PARENT_SCOPE)
    get_property(_type CACHE "${_var}" PROPERTY TYPE)
    get_property(_help CACHE "${_var}" PROPERTY HELPSTRING)
    set(_origin "")
    if(DEFINED "CACHE{_CNA_FNA3D_GIT_${member}_ORIGIN}")
        set(_origin "$CACHE{_CNA_FNA3D_GIT_${member}_ORIGIN}")
    endif()

    if(_type STREQUAL "UNINITIALIZED" OR _help STREQUAL _CNA_FNA3D_PIN_COMMAND_LINE_HELP)
        set(_class USER)
        set(_why "set on the command line")
    elseif(_origin STREQUAL "user")
        set(_class USER)
        set(_why "your override from an earlier configure")
    elseif(DEFINED "CACHE{_CNA_FNA3D_DEFAULT_GIT_${member}}")
        set(_recorded "$CACHE{_CNA_FNA3D_DEFAULT_GIT_${member}}")
        if(NOT _value STREQUAL _recorded)
            set(_class USER)
            set(_why "not CNA's default for this tree, ${_recorded}")
        elseif(_value STREQUAL default)
            set(_class CNA_CURRENT)
            set(_why "CNA's default")
        else()
            set(_class CNA_OBSOLETE)
            set(_why "the default an earlier CNA revision chose")
        endif()
    elseif(_value STREQUAL _CNA_FNA3D_PIN_PREFORK_${member}
           AND _help STREQUAL _CNA_FNA3D_PIN_PREFORK_DOC_${member})
        if(_value STREQUAL default)
            set(_class CNA_CURRENT)
            set(_why "CNA's default")
        else()
            set(_class CNA_OBSOLETE)
            set(_why "the upstream default CNA chose before the libcna forks")
        endif()
    else()
        set(_class USER)
        set(_why "not a default CNA chose")
    endif()
    set(${prefix}_CLASS "${_class}" PARENT_SCOPE)
    set(${prefix}_WHY "${_why}" PARENT_SCOPE)
endfunction()

# cna_fna3d_select_source_pin(DEFAULT_REPOSITORY <url> DEFAULT_TAG <commit>)
#
# Leaves CNA_FNA3D_GIT_REPOSITORY and CNA_FNA3D_GIT_TAG in the cache (STRING, CNA's docstrings)
# holding the pair this configure uses, or stops the configure when the cache holds a pair that
# cannot be resolved without guessing.
function(cna_fna3d_select_source_pin)
    _cna_fna3d_pin_constants()
    cmake_parse_arguments(_P "" "DEFAULT_REPOSITORY;DEFAULT_TAG" "" ${ARGN})
    if(_P_DEFAULT_REPOSITORY STREQUAL "" OR _P_DEFAULT_TAG STREQUAL "")
        message(FATAL_ERROR "cna_fna3d_select_source_pin: DEFAULT_REPOSITORY and DEFAULT_TAG are required")
    endif()
    set(_members REPOSITORY TAG)
    set(_other_REPOSITORY TAG)
    set(_other_TAG REPOSITORY)
    foreach(_m IN LISTS _members)
        _cna_fna3d_pin_classify(_c_${_m} "${_m}" "${_P_DEFAULT_${_m}}")
        set(_new_${_m} "${_c_${_m}_VALUE}")
        set(_write_${_m} TRUE)
    endforeach()
    set(_source_override FALSE)
    if(FETCHCONTENT_SOURCE_DIR_FNA3D)
        set(_source_override TRUE)
    endif()

    set(_user_members "")
    foreach(_m IN LISTS _members)
        if(_c_${_m}_CLASS STREQUAL "USER")
            list(APPEND _user_members "${_m}")
        endif()
    endforeach()
    list(LENGTH _user_members _user_count)

    set(_problem "")
    if(_user_count EQUAL 0)
        foreach(_m IN LISTS _members)
            set(_new_${_m} "${_P_DEFAULT_${_m}}")
        endforeach()
    elseif(_user_count EQUAL 1)
        set(_u "${_user_members}")
        set(_o "${_other_${_u}}")
        if(_c_${_o}_CLASS STREQUAL "CNA_OBSOLETE"
           AND NOT _c_${_u}_VALUE STREQUAL _P_DEFAULT_${_u})
            set(_problem
"CNA_FNA3D_GIT_${_u}=${_c_${_u}_VALUE} is your override (${_c_${_u}_WHY}), but \
CNA_FNA3D_GIT_${_o}=${_c_${_o}_VALUE} is ${_c_${_o}_WHY}, and CNA's pin is now \
${_P_DEFAULT_REPOSITORY} at ${_P_DEFAULT_TAG}. Moving only CNA_FNA3D_GIT_${_o} to \
${_P_DEFAULT_${_o}} would pair CNA's new default with your ${_u}, and keeping \
${_c_${_o}_VALUE} would keep a default CNA no longer builds. Either name CNA_FNA3D_GIT_${_o} \
yourself, with a type, so the pair is yours:
  -DCNA_FNA3D_GIT_${_o}:STRING=<the ${_o} you want with ${_c_${_u}_VALUE}>
or drop your override and use CNA's pin:
  -U CNA_FNA3D_GIT_${_u}")
            if(_source_override)
                # Nothing is fetched: keep the cache exactly as it is and say so.
                set(_new_${_o} "${_c_${_o}_VALUE}")
                set(_write_${_o} FALSE)
            endif()
        else()
            set(_new_${_o} "${_P_DEFAULT_${_o}}")
        endif()
    endif()

    if(_problem STREQUAL "")
        _cna_fna3d_pin_is_upstream(_upstream "${_new_REPOSITORY}")
        _cna_fna3d_pin_is_upstream(_default_upstream "${_P_DEFAULT_REPOSITORY}")
        if(_upstream AND NOT _default_upstream AND _new_TAG STREQUAL _P_DEFAULT_TAG)
            set(_problem
"CNA_FNA3D_GIT_REPOSITORY=${_new_REPOSITORY} is upstream FNA3D, but \
CNA_FNA3D_GIT_TAG=${_new_TAG} is CNA's pin, a commit that exists only in \
${_P_DEFAULT_REPOSITORY}. To build upstream FNA3D (without CNA's FNA3D and MojoShader fixes), \
name an upstream revision as well:
  -DCNA_FNA3D_GIT_TAG:STRING=<an upstream FNA3D commit or release tag, e.g. 26.10>
or drop the repository override and use CNA's pin:
  -U CNA_FNA3D_GIT_REPOSITORY")
        endif()
    endif()

    if(NOT _problem STREQUAL "")
        if(_source_override)
            message(WARNING
                "CNA: ${_problem}\nFETCHCONTENT_SOURCE_DIR_FNA3D=${FETCHCONTENT_SOURCE_DIR_FNA3D} "
                "is used instead, so nothing is fetched and this configure continues; without it, "
                "the configure stops here.")
        else()
            message(FATAL_ERROR "CNA: ${_problem}")
        endif()
    endif()

    set(_moved "")
    foreach(_m IN LISTS _members)
        if(_c_${_m}_CLASS STREQUAL "CNA_OBSOLETE" AND _write_${_m}
           AND NOT _c_${_m}_VALUE STREQUAL _new_${_m})
            list(APPEND _moved "CNA_FNA3D_GIT_${_m} ${_c_${_m}_VALUE} -> ${_new_${_m}}")
        endif()
    endforeach()
    if(_moved)
        string(REPLACE ";" ", " _moved "${_moved}")
        message(STATUS "CNA: this build tree's FNA3D pin follows CNA's current default (${_moved})")
    endif()

    foreach(_m IN LISTS _members)
        if(NOT _write_${_m})
            continue()
        endif()
        set(CNA_FNA3D_GIT_${_m} "${_new_${_m}}" CACHE STRING "${_CNA_FNA3D_PIN_DOC_${_m}}" FORCE)
        if(_c_${_m}_CLASS STREQUAL "USER")
            set(_CNA_FNA3D_GIT_${_m}_ORIGIN user CACHE INTERNAL
                "Whether CNA_FNA3D_GIT_${_m} is CNA's default (cna) or the developer's (user)")
        else()
            set(_CNA_FNA3D_GIT_${_m}_ORIGIN cna CACHE INTERNAL
                "Whether CNA_FNA3D_GIT_${_m} is CNA's default (cna) or the developer's (user)")
        endif()
        set(_CNA_FNA3D_DEFAULT_GIT_${_m} "${_P_DEFAULT_${_m}}"
            CACHE INTERNAL "CNA's own default for CNA_FNA3D_GIT_${_m}")
    endforeach()

    if(NOT _new_REPOSITORY STREQUAL _P_DEFAULT_REPOSITORY OR NOT _new_TAG STREQUAL _P_DEFAULT_TAG)
        set(_whose "")
        foreach(_m IN LISTS _members)
            if(_c_${_m}_CLASS STREQUAL "USER")
                list(APPEND _whose "CNA_FNA3D_GIT_${_m} is your override (${_c_${_m}_WHY})")
            elseif(NOT _new_${_m} STREQUAL _P_DEFAULT_${_m})
                list(APPEND _whose "CNA_FNA3D_GIT_${_m} is ${_c_${_m}_WHY}")
            endif()
        endforeach()
        string(REPLACE ";" "; " _whose "${_whose}")
        if(_source_override)
            set(_use "FETCHCONTENT_SOURCE_DIR_FNA3D is checked against")
        else()
            set(_use "FNA3D is fetched from")
        endif()
        message(WARNING
            "CNA: ${_use} ${_new_REPOSITORY} at ${_new_TAG}, not CNA's pin "
            "${_P_DEFAULT_REPOSITORY} at ${_P_DEFAULT_TAG} (${_whose}). "
            "-U CNA_FNA3D_GIT_REPOSITORY -U CNA_FNA3D_GIT_TAG returns to the pin.")
    endif()
endfunction()
