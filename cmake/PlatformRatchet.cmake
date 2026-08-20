# =====================================================================================
# CNA platform ratchet (plans/plan_platform.md Task PLAT-8)
#
# Reports, at configure time, how much of CNA calls SDL directly outside the PLAT-3
# production allowlist, and audits every surviving test/example use against PLAT-123's
# per-file categorized manifest.
#
# Why this runs from the very start of the migration rather than at the end: the SDL
# separation spans nine phases and ~250 production files. A check that only fires once
# the work is finished catches neither of the two failure modes that actually matter --
# a new SDL call site added to an already-migrated module, and a migration that stalls
# while still looking finished. Warning continuously makes the trend visible from day one.
#
# PLAT-121 reached the production floor and made it a hard error by default. PLAT-123's
# non-production audit is always strict: new native test scaffolding must be classified at
# its owning edge rather than entering through an open-ended tests/ exemption.
# =====================================================================================

option(CNA_PLATFORM_RATCHET "Report remaining direct SDL coupling at configure time (PLAT-8)" ON)
option(CNA_PLATFORM_RATCHET_STRICT "Make the platform ratchet a hard error instead of a warning (PLAT-121)" ON)

if(NOT CNA_PLATFORM_RATCHET)
    return()
endif()

# QUIET: the ratchet is a development aid, not a build dependency. A machine without a
# Python 3 interpreter must still be able to configure and build CNA -- skipping the
# report is the correct degradation, and saying so beats failing or staying silent.
find_package(Python3 QUIET COMPONENTS Interpreter)

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "CNA: platform ratchet skipped (no Python 3 interpreter found)")
    return()
endif()

set(_ratchet_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/platform/sdl_ratchet.py")
if(NOT EXISTS "${_ratchet_script}")
    message(STATUS "CNA: platform ratchet skipped (${_ratchet_script} not found)")
    return()
endif()

set(_ratchet_args "--repo" "${CMAKE_CURRENT_SOURCE_DIR}" "--check")
if(CNA_PLATFORM_RATCHET_STRICT)
    list(APPEND _ratchet_args "--strict")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${_ratchet_script}" ${_ratchet_args}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _ratchet_result
    OUTPUT_VARIABLE _ratchet_stdout
    ERROR_VARIABLE _ratchet_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)

if(_ratchet_stdout)
    message(STATUS "CNA: ${_ratchet_stdout}")
endif()

if(NOT _ratchet_result EQUAL 0)
    # Strict mode, or the script itself failed. Either way the text is on stderr.
    message(FATAL_ERROR "CNA platform ratchet failed:\n${_ratchet_stderr}")
elseif(_ratchet_stderr)
    # Warning mode with the budget exceeded: the script exits 0 and explains on stderr.
    message(WARNING "${_ratchet_stderr}")
endif()

# Re-run the check whenever a source file could have changed the count. Without this the
# report is only as fresh as the last configure, which would quietly defeat the purpose.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_ratchet_script}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/platform/sdl_budget.json")

set(_nonproduction_audit_script
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/platform/nonproduction_sdl_audit.py")
if(EXISTS "${_nonproduction_audit_script}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_nonproduction_audit_script}"
                --repo "${CMAKE_CURRENT_SOURCE_DIR}" --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _nonproduction_audit_result
        OUTPUT_VARIABLE _nonproduction_audit_stdout
        ERROR_VARIABLE _nonproduction_audit_stderr
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(_nonproduction_audit_stdout)
        message(STATUS "CNA: ${_nonproduction_audit_stdout}")
    endif()
    if(NOT _nonproduction_audit_result EQUAL 0)
        message(FATAL_ERROR
            "CNA non-production SDL audit failed:\n${_nonproduction_audit_stderr}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_nonproduction_audit_script}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/platform/nonproduction_sdl_budget.json")
endif()

# On-demand target for the full per-module breakdown, so a developer working through a
# migration task can see what is left without re-configuring.
add_custom_target(cna_platform_ratchet
    COMMAND "${Python3_EXECUTABLE}" "${_ratchet_script}" --repo "${CMAKE_CURRENT_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Reporting remaining direct SDL coupling (plans/plan_platform.md PLAT-8)"
    VERBATIM)

if(EXISTS "${_nonproduction_audit_script}")
    add_custom_target(cna_platform_nonproduction_sdl_audit
        COMMAND "${Python3_EXECUTABLE}" "${_nonproduction_audit_script}"
                --repo "${CMAKE_CURRENT_SOURCE_DIR}" --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Auditing justified SDL use in tests/examples (plans/plan_platform.md PLAT-123)"
        VERBATIM)
endif()
