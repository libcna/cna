# =====================================================================================
# CNA hot-path lint (plan_platform.md Task PLAT-9)
#
# Enforces design decision 4 -- no platform call inside a per-pixel, per-vertex,
# per-fragment, per-sample or per-event loop -- at configure time.
#
# Unlike the SDL ratchet next to it, this one is a hard ERROR from the first day. The
# ratchet warns because the whole tree is over budget by design and failing would block
# every task; this lint starts at zero violations and must stay there. A regression here
# is one line, always introduced deliberately, and always has either a correct fix
# (hoist the call) or a correct annotation (`// CNA_PLATFORM_HOT_PATH_OK: <reason>`).
# Warning about it would mean the count only ever goes up.
# =====================================================================================

option(CNA_PLATFORM_HOT_PATH_LINT "Reject platform calls inside hot loops at configure time (PLAT-9)" ON)

if(NOT CNA_PLATFORM_HOT_PATH_LINT)
    return()
endif()

# QUIET, and skipped when absent, for the same reason as the ratchet: a machine with no
# Python 3 must still be able to configure and build CNA.
find_package(Python3 QUIET COMPONENTS Interpreter)

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "CNA: hot-path lint skipped (no Python 3 interpreter found)")
    return()
endif()

set(_hot_path_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/platform/hot_path_lint.py")
if(NOT EXISTS "${_hot_path_script}")
    message(STATUS "CNA: hot-path lint skipped (${_hot_path_script} not found)")
    return()
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${_hot_path_script}" --repo "${CMAKE_CURRENT_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _hot_path_result
    OUTPUT_VARIABLE _hot_path_stdout
    ERROR_VARIABLE _hot_path_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)

if(_hot_path_stdout)
    message(STATUS "CNA: ${_hot_path_stdout}")
endif()

if(NOT _hot_path_result EQUAL 0)
    message(FATAL_ERROR "CNA hot-path lint failed:\n${_hot_path_stderr}")
endif()

# The lint reads the contract headers to decide what counts as a platform call, so a new
# method there changes its answer just as much as a new call site does. Both are listed.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_hot_path_script}")

# On-demand target, so a developer can re-check after an edit without re-configuring.
add_custom_target(cna_platform_hot_path_lint
    COMMAND "${Python3_EXECUTABLE}" "${_hot_path_script}" --repo "${CMAKE_CURRENT_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking for platform calls inside hot loops (plan_platform.md PLAT-9)"
    VERBATIM)
