# SPDX-License-Identifier: MS-PL
# =====================================================================================
# CNA renderer identities: the public set, the retired set, and the early refusal of both
# unknown and retired selections (plans/plan_renderer_cleanup.md RRC-002).
#
# CNA intentionally maintains a curated renderer set. This file is the CMake side of the identity
# registry: GraphicsRendererType.hpp is the C++ side, CNA/C/graphics.h the C ABI side, and
# scripts/check_renderer_identities.py holds all three to one canonical table.
#
# Included from the top-level CMakeLists.txt before anything reads CNA_GRAPHICS_RENDERER (the SDL2
# gate and the SDL availability gate both do, well before cmake/RendererSelection.cmake), so a
# refused selection fails before the vendored SDL sub-build is configured. It also runs standalone
# under `cmake -P` (cmake/Tests/RendererRetiredIdentityCase.cmake).
#
# Why a retired identity is refused BY NAME rather than simply forgotten: forgetting it would make
# `-DCNA_RENDERER_BGFX=ON` an unread cache entry, and the build would quietly configure the host's
# default renderer instead -- a silent fallback. Every route that can name a renderer is checked.
# =====================================================================================

include_guard(GLOBAL)

# `cmake -P` does not inherit the project's cmake_minimum_required() policy set.
cmake_policy(SET CMP0057 NEW)

# The 25 public renderer identities, in C ABI value order.
set(CNA_RENDERER_PUBLIC_IDENTITIES
    SDL_RENDERER OPENGLES2 OPENGLES3 OPENGL33 WEBGL1 WEBGL2 VULKAN WEBGPU HEADLESS SOFTWARE STUB
    DIRECTX11 DIRECTX12 DIRECT2D CANVAS HTML_DOM FREEDIRECT DIRECTX9 SDL_GPU OPENGL4 GDI METAL FNA3D
    SVG_DOM PORTABLEGL)

# Retired identities as <NAME>=<C ABI value>. A retired value stays reserved forever: it is never
# assigned to another renderer, and the next new identity takes value 52. See
# docs/removed-renderers.md for what each one was and why it was retired.
set(CNA_RENDERER_RETIRED_IDENTITIES
    BGFX=7 MAGNUM=10 SKIA=19 BLEND2D=20
    DIRECTX1=23 DIRECTX2=24 DIRECTX3=25 DIRECTX5=26 DIRECTX6=27 DIRECTX7=28 DIRECTX8=29 DIRECTX10=30
    OPENGLES1=32 OPENGL1=34 OPENGL2=35 WICKED=36 SOKOL=37 DILIGENT=38 GLIDE=39 LLGL=41 OPENVG=45
    TINYGL=47 IGL=48 PIXIJS=49 NANOVG=50 RLGL=51)

# Returns the retired C ABI value of an identity (compared case-insensitively), or an empty string
# when the identity is not retired.
#
# @param identity The renderer name to look up.
# @param out_var Variable to receive the retired value, in the caller's scope.
function(cna_renderer_retired_value identity out_var)
    string(TOUPPER "${identity}" _cna_upper)
    set(_cna_value "")
    foreach(_cna_entry IN LISTS CNA_RENDERER_RETIRED_IDENTITIES)
        string(REPLACE "=" ";" _cna_pair "${_cna_entry}")
        list(GET _cna_pair 0 _cna_name)
        list(GET _cna_pair 1 _cna_number)
        if(_cna_upper STREQUAL _cna_name)
            set(_cna_value "${_cna_number}")
            break()
        endif()
    endforeach()
    set(${out_var} "${_cna_value}" PARENT_SCOPE)
endfunction()

# Fails the configure when `identity` is not one of the public identities, naming the route it
# came through. A retired identity gets its own message: its retired value and where it is recorded.
#
# @param identity The requested renderer name.
# @param route The option that carried it, for the message.
function(cna_require_public_renderer_identity identity route)
    if(identity IN_LIST CNA_RENDERER_PUBLIC_IDENTITIES)
        return()
    endif()
    list(JOIN CNA_RENDERER_PUBLIC_IDENTITIES ", " _cna_public_text)
    cna_renderer_retired_value("${identity}" _cna_retired)
    if(NOT _cna_retired STREQUAL "")
        string(TOUPPER "${identity}" _cna_upper)
        message(FATAL_ERROR
            "CNA: renderer ${_cna_upper} (requested through ${route}) has been retired and is no "
            "longer supported. It is not replaced by another renderer; its C ABI value "
            "${_cna_retired} stays reserved and is never reused.\n"
            "  See docs/removed-renderers.md for what it was and why it was removed.\n"
            "  Supported renderers: ${_cna_public_text}.")
    endif()
    message(FATAL_ERROR
        "CNA: unknown graphics renderer '${identity}' (requested through ${route}).\n"
        "  Supported renderers: ${_cna_public_text}.")
endfunction()

# Checks every route that can name a renderer: CNA_GRAPHICS_RENDERER, each member of
# CNA_GRAPHICS_RENDERERS, and the per-identity CNA_RENDERER_<X> switches. An empty
# CNA_GRAPHICS_RENDERER means the per-host default applies (cmake/RendererIdentityDefault.cmake).
function(cna_validate_renderer_identity_selection)
    foreach(_cna_entry IN LISTS CNA_RENDERER_RETIRED_IDENTITIES)
        string(REPLACE "=" ";" _cna_pair "${_cna_entry}")
        list(GET _cna_pair 0 _cna_name)
        if(CNA_RENDERER_${_cna_name})
            cna_require_public_renderer_identity("${_cna_name}" "CNA_RENDERER_${_cna_name}=ON")
        endif()
    endforeach()

    if(DEFINED CNA_GRAPHICS_RENDERER AND NOT CNA_GRAPHICS_RENDERER STREQUAL "")
        cna_require_public_renderer_identity("${CNA_GRAPHICS_RENDERER}" "CNA_GRAPHICS_RENDERER")
    endif()

    foreach(_cna_identity IN LISTS CNA_GRAPHICS_RENDERERS)
        cna_require_public_renderer_identity("${_cna_identity}" "CNA_GRAPHICS_RENDERERS")
    endforeach()
endfunction()

cna_validate_renderer_identity_selection()
