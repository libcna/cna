# sharp-runtime consumption seam (plans/MODULARIZATION_PLAN.md §3).
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
# SharpRuntime/... includes; plans/MODULARIZATION_PLAN.md §1.6): used when a target does not name
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
    ComponentModel
    Storage
    Security.Cryptography
    Xml
    # XNA-MISSING-013: ResourceContentManager's documented constructor names
    # System::Resources::ResourceManager, so Resources joins the closure. Unlike Uri and
    # ComponentModel it is not a transitive PUBLIC dependency of anything already here, so it has to
    # be selected explicitly or SharpRuntime::Resources is never instantiated.
    Resources
)

# SAMPLE-066: modules/math's XmlSerializationEXT.hpp includes
# System/Xml/Serialization/detail/XmlMember.hpp, so the component has to be among the selected
# ones for its target to exist at all. Header-only (INTERFACE), so selecting it costs a consumer
# nothing until it includes that header.
#
# plans/plan_dx.md DX-250: it cannot be selected on a Windows target. SharpRuntime's
# Xml.Serialization has a PUBLIC dependency on Xml, which has a PRIVATE dependency on
# Diagnostics, whose System/Diagnostics/Process.cpp includes <poll.h> unconditionally -- outside
# its own `#if !defined(_WIN32)` POSIX block. Neither MinGW-w64 nor the MSVC Windows SDK ships
# that header, so selecting this component makes EVERY Windows build of CNA (the MinGW cross-build
# this project's D3D11/D3D12 dev loop uses, and .github/workflows/d3d-windows-ci.yml's native-MSVC
# job) fail to compile a dependency it never asked for. Excluding it here is the honest boundary:
# the target does not exist on Windows, so the one CNA consumer of it (the math unit-test group,
# see cmake/UnitTests.cmake) is Windows-excluded too, and nothing silently links a half-built
# component. Remove this condition once sharp-runtime guards that include.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CNA_SHARP_RUNTIME_HAS_XML_SERIALIZATION OFF)
else()
    set(CNA_SHARP_RUNTIME_HAS_XML_SERIALIZATION ON)
    list(APPEND CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS Xml.Serialization)
endif()

# The System::Xml stack is in the closure above so that it is instantiated: the build-time content
# pipeline needs it (XNAPP-071, IntermediateSerializer) and a game may use XmlSerializer. No runtime
# module and no renderer includes a System/Xml header, though. A renderer family links the closure
# PUBLIC, and it used to link all of it, so Xml -- and sharp-runtime's vendored tinyxml2 with it --
# reached every program that links a renderer, including one that picks CNA modules one by one and
# brings an XML parser of its own (MeshCraft, whose Mc3 links its own tinyxml2). Renderers link
# CNA_SHARP_RUNTIME_RENDERER_COMPONENTS instead; the `CNA` umbrella adds the XML components back for
# games (modules/CMakeLists.txt), and cna_content_pipeline names Xml itself.
set(CNA_SHARP_RUNTIME_XML_COMPONENTS Xml)
if(CNA_SHARP_RUNTIME_HAS_XML_SERIALIZATION)
    list(APPEND CNA_SHARP_RUNTIME_XML_COMPONENTS Xml.Serialization)
endif()
set(CNA_SHARP_RUNTIME_RENDERER_COMPONENTS ${CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS})
list(REMOVE_ITEM CNA_SHARP_RUNTIME_RENDERER_COMPONENTS ${CNA_SHARP_RUNTIME_XML_COMPONENTS})

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
