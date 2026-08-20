# --- Renderer descriptor compilation gate (plans/plan_runtimerenderer.md) ---
#
# A GraphicsRendererDescriptor translation unit is the one file per renderer family that a build
# needs even when that family is not selected: it is what GraphicsRendererRegistry hands
# GraphicsDevice, and it answers the window/context questions BEFORE any renderer exists. It is
# also the one file per family that no ordinary configuration compiles -- a single-renderer build
# enters exactly one family's directory, so 44 of the 45 descriptors are never seen by a compiler.
#
# That is how three descriptors reached the integration branch with unbalanced braces and
# statements sitting directly inside a namespace body: the families they belong to (BGFX, LLGL,
# OPENGL1) were simply never configured after the file was written, and every gate the project
# had reads the sources as TEXT rather than compiling them.
#
# This target closes that hole with the real build system: every registered family's descriptor is
# compiled by SOMETHING in every configuration -- by its own family target when the family is part
# of this build, and by cna_renderer_descriptor_gate otherwise. The inventory comes from
# cna_all_renderer_identities(), i.e. from the registry map itself, so a family added there is
# covered on the same commit rather than waiting for someone to remember a second list.
#
# The gate is an OBJECT library that nothing links: the descriptors it compiles here would be
# duplicate definitions of symbols the real archives own. Compiling them is the entire point.

option(CNA_BUILD_RENDERER_DESCRIPTOR_GATE
    "Compile every registered renderer family's descriptor, not only the selected families'" ON)

# The families whose descriptor legitimately cannot be compiled outside its own family target,
# because it reaches into that family's third-party SDK to answer a question honestly:
#
#   bgfx      -- ResolvedWindowKind() asks bgfx itself which native API it will pick
#                (bgfx::RendererType, from <bgfx/bgfx.h>).
#   directx9  -- design decision 9's adapterQueries answer from real D3DCAPS9 / D3DFORMAT values
#                (<d3d9.h>).
#
# Neither can be made self-contained without dropping behaviour, so for these two the "compiled at
# least once in a configuration where the family is available" rule is met by the family's OWN
# target: a BGFX build and a DIRECTX9 build each compile theirs for real. Every other family's
# descriptor needs nothing but CNA's own headers, and this list is deliberately hard to grow --
# a new family is gated by default, and excluding it takes a deliberate edit with a reason.
set(CNA_DESCRIPTOR_GATE_SDK_BOUND_FAMILIES bgfx directx9)

# Defines the descriptor gate target. Must be called from modules/renderers/CMakeLists.txt, after
# CNA_SELECTED_RENDERER_FAMILIES has been established.
function(cna_add_renderer_descriptor_gate)
    if(NOT CNA_BUILD_RENDERER_DESCRIPTOR_GATE)
        return()
    endif()

    set(_renderers_dir "${CNA_SOURCE_DIR}/modules/renderers")
    file(GLOB _descriptor_sources CONFIGURE_DEPENDS
        "${_renderers_dir}/*/src/*RendererDescriptor.cpp")

    # Every registered identity must resolve to a descriptor that declares the namespace the
    # generated registry will call into. This is the half of the gate that catches a family with
    # no descriptor at all, or one whose namespace no longer matches its registry entry -- neither
    # of which a compile of the existing files could notice.
    cna_all_renderer_identities(_identities)
    set(_gate_sources)
    set(_gate_families)
    set(_deferred_families)
    foreach(_identity IN LISTS _identities)
        cna_renderer_identity_to_namespace("${_identity}" _entry)
        _cna_split_renderer_entry("${_entry}" _namespace _accessor)

        set(_found "")
        foreach(_source IN LISTS _descriptor_sources)
            file(STRINGS "${_source}" _match
                REGEX "^namespace[ \t]+CNA::Internal::Renderers::${_namespace}[ \t]*$")
            if(_match)
                set(_found "${_source}")
                break()
            endif()
        endforeach()

        if(NOT _found)
            message(FATAL_ERROR
                "CNA: renderer identity '${_identity}' is registered in cmake/RendererRegistry.cmake "
                "as family '${_namespace}', but no modules/renderers/*/src/*RendererDescriptor.cpp "
                "declares namespace CNA::Internal::Renderers::${_namespace}. The generated registry "
                "would reference a descriptor accessor that does not exist.")
        endif()

        get_filename_component(_family_dir "${_found}" DIRECTORY)
        get_filename_component(_family_dir "${_family_dir}" DIRECTORY)
        get_filename_component(_family "${_family_dir}" NAME)

        if(_family IN_LIST _gate_families OR _family IN_LIST _deferred_families)
            continue()   # EasyGL serves five identities from one descriptor file per accessor.
        endif()

        if(_family IN_LIST CNA_SELECTED_RENDERER_FAMILIES)
            # Already compiled for real, with that family's own flags and dependencies.
            list(APPEND _deferred_families "${_family}")
        elseif(_family IN_LIST CNA_DESCRIPTOR_GATE_SDK_BOUND_FAMILIES)
            list(APPEND _deferred_families "${_family}")
        else()
            list(APPEND _gate_families "${_family}")
            list(APPEND _gate_sources "${_found}")
        endif()
    endforeach()

    if(NOT _gate_sources)
        return()
    endif()

    add_library(cna_renderer_descriptor_gate OBJECT ${_gate_sources})
    target_link_libraries(cna_renderer_descriptor_gate PRIVATE
        cna_renderer_common cna_graphics_core cna_core cna_math)
    cna_link_sharp_runtime(cna_renderer_descriptor_gate PRIVATE)

    # A descriptor may consult its own family's pre-construction helpers -- IGL's backend
    # resolution, LLGL's, Diligent's device selection, EasyGL's GL profile, FNA3D's window flags --
    # and those headers live on a family target this configuration does not build. Every family's
    # public root is namespaced CNA/Internal/Renderers/<Family>/, so making them all reachable here
    # cannot shadow anything.
    file(GLOB _family_include_roots "${_renderers_dir}/*/include")
    foreach(_root IN LISTS _family_include_roots)
        if(IS_DIRECTORY "${_root}")
            target_include_directories(cna_renderer_descriptor_gate PRIVATE "${_root}")
        endif()
    endforeach()

    list(LENGTH _gate_sources _gate_count)
    list(LENGTH _deferred_families _deferred_count)
    message(STATUS
        "CNA: renderer descriptor gate -- ${_gate_count} compiled here, "
        "${_deferred_count} by their own family target (${_deferred_families})")
endfunction()
