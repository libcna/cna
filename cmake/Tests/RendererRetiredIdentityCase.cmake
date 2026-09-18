# SPDX-License-Identifier: MS-PL
#
# plans/plan_renderer_cleanup.md RRC-006: one case of "a retired renderer selector is refused, by
# name, with no silent fallback" -- run for real.
#
# Runs cmake/RendererIdentities.cmake in `cmake -P` script mode with one selection and asserts the
# outcome. Same shape and same reason as cmake/Tests/RendererDefaultCase.cmake: the refusal is a
# decision the build makes, and a decision is worth a test that executes it rather than a paragraph
# claiming it. Script mode keeps each case in milliseconds instead of a full project configure, so
# all twenty-five retired selectors can be covered instead of a representative handful.
#
# What makes this worth testing at all: a retired identity that were merely *forgotten* would leave
# `-DCNA_GRAPHICS_RENDERER=BGFX` as an unread cache entry, and the configure would quietly succeed
# against the host's default renderer. The user would get a build that runs -- with the wrong
# renderer, and no diagnostic anywhere. That is the failure mode this file exists to keep closed,
# which is why a REFUSE case asserts on the message text and not only on the exit status.
#
# Inputs:
#   CNA_RETIRED_CASE_FILE       path to cmake/RendererIdentities.cmake
#   CNA_RETIRED_CASE_ROUTE      SELECTOR, SET, or OPTION -- which route names the renderer
#   CNA_RETIRED_CASE_IDENTITY   the identity to name on that route
#   CNA_RETIRED_CASE_OUTCOME    ACCEPT or REFUSE
#   CNA_RETIRED_CASE_EXPECTED   text the output must contain either way

if(NOT DEFINED CNA_RETIRED_CASE_FILE OR NOT DEFINED CNA_RETIRED_CASE_ROUTE OR
   NOT DEFINED CNA_RETIRED_CASE_IDENTITY OR NOT DEFINED CNA_RETIRED_CASE_OUTCOME OR
   NOT DEFINED CNA_RETIRED_CASE_EXPECTED)
    message(FATAL_ERROR
        "retired-identity test requires file, route, identity, outcome and expected text")
endif()

# Each route is a genuinely different way for a name to reach the build, and every one of them had
# to be checked: the cache variable, a member of the multi-renderer list, and the per-family
# CNA_RENDERER_<X>=ON option. The third is the one that is easy to miss, because nothing else reads
# it when the identity no longer exists.
if(CNA_RETIRED_CASE_ROUTE STREQUAL "SELECTOR")
    set(_cna_case_arguments "-DCNA_GRAPHICS_RENDERER=${CNA_RETIRED_CASE_IDENTITY}")
elseif(CNA_RETIRED_CASE_ROUTE STREQUAL "SET")
    # A real list has to survive two expansions to reach the child cmake as one argument; the
    # escaped `\;` is what does it. Same trap as cmake/Tests/RendererDefaultCase.cmake documents.
    set(_cna_case_arguments
        "-DCNA_GRAPHICS_RENDERER=HEADLESS"
        "-DCNA_GRAPHICS_RENDERERS=HEADLESS\\;${CNA_RETIRED_CASE_IDENTITY}\\;STUB")
elseif(CNA_RETIRED_CASE_ROUTE STREQUAL "OPTION")
    set(_cna_case_arguments "-DCNA_RENDERER_${CNA_RETIRED_CASE_IDENTITY}=ON")
else()
    message(FATAL_ERROR
        "unknown route '${CNA_RETIRED_CASE_ROUTE}'; use SELECTOR, SET or OPTION")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_cna_case_arguments} -P "${CNA_RETIRED_CASE_FILE}"
    RESULT_VARIABLE _cna_case_result
    OUTPUT_VARIABLE _cna_case_stdout
    ERROR_VARIABLE _cna_case_stderr
)
set(_cna_case_output "${_cna_case_stdout}${_cna_case_stderr}")

if(CNA_RETIRED_CASE_OUTCOME STREQUAL "REFUSE")
    if(_cna_case_result EQUAL 0)
        message(FATAL_ERROR
            "${CNA_RETIRED_CASE_IDENTITY} is a retired renderer identity, named through the "
            "${CNA_RETIRED_CASE_ROUTE} route, and the configure accepted it. A retired selector "
            "must fail by name -- accepting it silently configures some other renderer instead, "
            "which is the exact silent fallback plans/plan_renderer_cleanup.md forbids.\n"
            "${_cna_case_output}")
    endif()
elseif(CNA_RETIRED_CASE_OUTCOME STREQUAL "ACCEPT")
    if(NOT _cna_case_result EQUAL 0)
        message(FATAL_ERROR
            "${CNA_RETIRED_CASE_IDENTITY} is a live public renderer identity and was refused:\n"
            "${_cna_case_output}")
    endif()
else()
    message(FATAL_ERROR "unknown outcome '${CNA_RETIRED_CASE_OUTCOME}'; use ACCEPT or REFUSE")
endif()

# The message text is half the contract. "Unknown renderer" would be a true but much worse
# diagnostic for a name that CNA deliberately used to have: the reserved value and the pointer to
# docs/removed-renderers.md are what tell a reader this was retired on purpose rather than
# misspelled, so a REFUSE case asserts on them.
string(FIND "${_cna_case_output}" "${CNA_RETIRED_CASE_EXPECTED}" _cna_case_expected_at)
if(_cna_case_expected_at EQUAL -1)
    message(FATAL_ERROR
        "output did not contain '${CNA_RETIRED_CASE_EXPECTED}':\n${_cna_case_output}")
endif()
