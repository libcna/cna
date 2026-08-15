# sharp-runtime consumption seam (MODULARIZATION_PLAN.md §3).
#
# sharp-runtime exists in two shapes:
#
#   - modular (sharp-runtime develop since 2026-08-10, commit 81624983): the
#     SharpRuntime::<Component> targets plus a compatibility SHARP_RUNTIME INTERFACE that
#     exists only under the default "All" selection — the AUTHORITATIVE shape for the
#     sibling-develop combination;
#   - monolithic (pre-modularization checkouts): one SHARP_RUNTIME static archive — kept as
#     a cheap fallback for old checkouts.
#
# Every CNA module declares the specific sharp-runtime components it actually needs through
# cna_link_sharp_runtime(); against a modular checkout it links exactly those component
# targets (their PUBLIC dependencies close over the rest), against the monolith it links the
# single archive.

# The complete component closure CNA needs anywhere (derived from CNA's System/... and
# SharpRuntime/... includes; MODULARIZATION_PLAN.md §1.6): used when a target does not name
# its own narrower set. Transitive PUBLIC component dependencies (Uri, TimeZone,
# ComponentModel, Buffers, ...) arrive through these.
set(CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS
    Core.Base
    IO
    Collections.Core
    Collections.ObjectModel
    Runtime
    Threading
    Text
    Globalization
    Storage
    Security.Cryptography
)

function(cna_detect_sharp_runtime_shape)
    if(TARGET SharpRuntime::Core.Base)
        set(CNA_SHARP_RUNTIME_IS_MODULAR ON PARENT_SCOPE)
        message(STATUS "CNA: sharp-runtime is MODULAR -- CNA modules link specific SharpRuntime:: components")
    else()
        set(CNA_SHARP_RUNTIME_IS_MODULAR OFF PARENT_SCOPE)
        message(STATUS "CNA: sharp-runtime is monolithic -- CNA modules link the SHARP_RUNTIME archive")
    endif()
endfunction()

# cna_link_sharp_runtime(<target> <PUBLIC|PRIVATE|INTERFACE> [<Component>...])
# With no components: the CNA-wide default closure above.
function(cna_link_sharp_runtime target visibility)
    set(_components ${ARGN})
    if(NOT _components)
        set(_components ${CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS})
    endif()
    if(CNA_SHARP_RUNTIME_IS_MODULAR)
        set(_libs)
        foreach(_component IN LISTS _components)
            list(APPEND _libs "SharpRuntime::${_component}")
        endforeach()
        target_link_libraries(${target} ${visibility} ${_libs})
    else()
        target_link_libraries(${target} ${visibility} SHARP_RUNTIME)
    endif()
endfunction()
