# SPDX-License-Identifier: MS-PL
#
# plan_runtimerenderer.md RTR-P6-1: one case of the CNA_GRAPHICS_RENDERER / CNA_GRAPHICS_RENDERERS
# contract, run for real.
#
# Runs cmake/RendererDefaultSelection.cmake in `cmake -P` script mode with one pair of option
# values and asserts the outcome -- the same shape as cmake/Tests/AudioPlatformSelectionCase.cmake,
# and for the same reason: "the default must be a member of the set" is a decision the build makes,
# and a decision is worth a test that executes it. Script mode keeps that test in milliseconds
# rather than in three full project configures.
#
# Inputs:
#   CNA_RENDERER_DEFAULT_FILE      path to cmake/RendererDefaultSelection.cmake
#   CNA_RENDERER_DEFAULT_RENDERER  value for CNA_GRAPHICS_RENDERER
#   CNA_RENDERER_DEFAULT_SET       value for CNA_GRAPHICS_RENDERERS, or EMPTY for single-renderer
#   CNA_RENDERER_DEFAULT_OUTCOME   ACCEPT or REJECT
#   CNA_RENDERER_DEFAULT_EXPECTED  text the output must contain either way

if(NOT DEFINED CNA_RENDERER_DEFAULT_FILE OR NOT DEFINED CNA_RENDERER_DEFAULT_RENDERER OR
   NOT DEFINED CNA_RENDERER_DEFAULT_SET OR NOT DEFINED CNA_RENDERER_DEFAULT_OUTCOME OR
   NOT DEFINED CNA_RENDERER_DEFAULT_EXPECTED)
    message(FATAL_ERROR
        "renderer default-selection test requires file, renderer, set, outcome and expected text")
endif()

# The caller writes the renderer list with COMMAS, for two reasons that both bite here: a
# semicolon inside an add_test() argument would split it into separate arguments, and a semicolon
# inside a CMake variable expanded unquoted into execute_process(COMMAND ...) would split it again.
# The escaped `\;` below is what survives that second expansion and reaches the child cmake as one
# argument holding a real list.
set(_cna_case_renderers_argument "")
if(NOT CNA_RENDERER_DEFAULT_SET STREQUAL "EMPTY")
    string(REPLACE "," "\\;" _cna_case_set "${CNA_RENDERER_DEFAULT_SET}")
    set(_cna_case_renderers_argument "-DCNA_GRAPHICS_RENDERERS=${_cna_case_set}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DCNA_GRAPHICS_RENDERER=${CNA_RENDERER_DEFAULT_RENDERER}"
            ${_cna_case_renderers_argument}
            -P "${CNA_RENDERER_DEFAULT_FILE}"
    RESULT_VARIABLE _cna_case_result
    OUTPUT_VARIABLE _cna_case_stdout
    ERROR_VARIABLE _cna_case_stderr
)
set(_cna_case_output "${_cna_case_stdout}${_cna_case_stderr}")

if(CNA_RENDERER_DEFAULT_OUTCOME STREQUAL "ACCEPT")
    if(NOT _cna_case_result EQUAL 0)
        message(FATAL_ERROR
            "CNA_GRAPHICS_RENDERER=${CNA_RENDERER_DEFAULT_RENDERER} with "
            "CNA_GRAPHICS_RENDERERS=${CNA_RENDERER_DEFAULT_SET} was rejected but should be "
            "accepted:\n${_cna_case_output}")
    endif()
elseif(CNA_RENDERER_DEFAULT_OUTCOME STREQUAL "REJECT")
    if(_cna_case_result EQUAL 0)
        message(FATAL_ERROR
            "CNA_GRAPHICS_RENDERER=${CNA_RENDERER_DEFAULT_RENDERER} is not a member of "
            "CNA_GRAPHICS_RENDERERS=${CNA_RENDERER_DEFAULT_SET} and was accepted anyway. "
            "Silently adopting another default is the behaviour RTR-P6-1 removed.\n"
            "${_cna_case_output}")
    endif()
else()
    message(FATAL_ERROR "unknown outcome '${CNA_RENDERER_DEFAULT_OUTCOME}'; use ACCEPT or REJECT")
endif()

string(FIND "${_cna_case_output}" "${CNA_RENDERER_DEFAULT_EXPECTED}" _cna_case_expected_at)
if(_cna_case_expected_at EQUAL -1)
    message(FATAL_ERROR
        "output did not contain '${CNA_RENDERER_DEFAULT_EXPECTED}':\n${_cna_case_output}")
endif()
