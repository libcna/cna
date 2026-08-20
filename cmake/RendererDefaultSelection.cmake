# =====================================================================================
# CNA renderer set and default (plans/plan_runtimerenderer.md design decision 1, phase P6, RTR-P6-1)
#
# Resolves the two renderer options into the ordered identity list the rest of the build uses:
#
#   CNA_GRAPHICS_RENDERER   the DEFAULT renderer -- the one used when nothing selects another at
#                           runtime, and the one whose CNA_RENDERER_<X> macro is defined
#                           project-wide. Single-valued, and unchanged in meaning since before
#                           multi-renderer builds existed.
#   CNA_GRAPHICS_RENDERERS  the opt-in list of renderers to compile in. Empty means
#                           single-renderer mode, in which every path below runs exactly as it
#                           did before this option existed.
#
# Split out of RendererSelection.cmake so this contract can be exercised on its own with
# `cmake -P` -- the same shape as cmake/AudioPlatformSelection.cmake, and for the same reason:
# the rule "the default must be a member of the set" is a decision, and a decision deserves a
# test that runs it rather than a paragraph asserting it. See cmake/Tests/RendererDefaultCase.cmake
# and the CnaRendererDefaultSelection_* tests.
#
# Inputs:  CNA_GRAPHICS_RENDERER, CNA_GRAPHICS_RENDERERS
# Outputs: CNA_RENDERER_IDENTITIES        every identity to compile in, DEFAULT FIRST
#          _cna_default_renderer_identity the default, restated for the includer's own use
#          CNA_GRAPHICS_RENDERER          unchanged -- this file never rewrites the caller's choice
# =====================================================================================

# `cmake -P` does not inherit the project's cmake_minimum_required() policy set. Keep IN_LIST
# meaning the same in the lightweight selection tests and in a normal project include.
cmake_policy(SET CMP0057 NEW)

set(CNA_GRAPHICS_RENDERERS "" CACHE STRING
    "Semicolon-separated list of graphics renderers to compile in (opt-in multi-renderer mode). \
Empty means single-renderer mode using CNA_GRAPHICS_RENDERER.")

if(CNA_GRAPHICS_RENDERERS STREQUAL "")
    set(_cna_renderer_identities "${CNA_GRAPHICS_RENDERER}")
else()
    set(_cna_renderer_identities ${CNA_GRAPHICS_RENDERERS})
    # CNA_GRAPHICS_RENDERER names the default WITHIN the list, so it must be a member of it
    # (RTR-P6-1, and the contract docs/runtime-renderer-selection.md states). A non-member is a
    # configuration error and is refused here.
    #
    # This used to substitute the list's first entry and print a STATUS line. That was silent
    # substitution of the one decision this option exists to express: the whole project-wide macro
    # environment, every `CNA_GRAPHICS_RENDERER STREQUAL` gate, which example targets exist and
    # which renderer the binary starts on all follow the default -- so a build asking for SOFTWARE
    # and quietly getting OPENGL4 is a build whose every later renderer question answers about a
    # renderer nobody asked for, and a STATUS line is not read in a CI log that scrolls. It is also
    # exactly the failure mode design decision 6 refuses at RUNTIME ("CNA never silently
    # substitutes a different renderer"); configure time may not be laxer than run time about the
    # same question.
    if(NOT CNA_GRAPHICS_RENDERER IN_LIST _cna_renderer_identities)
        list(GET _cna_renderer_identities 0 _cna_first_renderer_identity)
        list(JOIN _cna_renderer_identities ";" _cna_renderer_identities_text)
        message(FATAL_ERROR
            "CNA: CNA_GRAPHICS_RENDERER=${CNA_GRAPHICS_RENDERER} is not a member of "
            "CNA_GRAPHICS_RENDERERS=\"${_cna_renderer_identities_text}\".\n"
            "  CNA_GRAPHICS_RENDERER names the DEFAULT renderer chosen from the compiled-in set, "
            "so it has to be one of them.\n"
            "  Either add ${CNA_GRAPHICS_RENDERER} to CNA_GRAPHICS_RENDERERS, or set "
            "-DCNA_GRAPHICS_RENDERER=${_cna_first_renderer_identity} to make the list's first "
            "entry the default.\n"
            "  See docs/runtime-renderer-selection.md, 'Building with several renderers'.")
    endif()
    # The default must be attempted first, so the generated registry lists it first.
    list(REMOVE_ITEM _cna_renderer_identities "${CNA_GRAPHICS_RENDERER}")
    list(INSERT _cna_renderer_identities 0 "${CNA_GRAPHICS_RENDERER}")
endif()

list(REMOVE_DUPLICATES _cna_renderer_identities)
set(_cna_default_renderer_identity "${CNA_GRAPHICS_RENDERER}")
# Visible to modules/renderers/CMakeLists.txt, which has to re-establish the per-family identity
# before entering each family's own subdirectory.
set(CNA_RENDERER_IDENTITIES "${_cna_renderer_identities}")

message(STATUS "CNA: renderer set -- ${CNA_RENDERER_IDENTITIES} (default: ${CNA_GRAPHICS_RENDERER})")
