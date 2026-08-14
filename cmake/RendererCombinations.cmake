# --- Renderer combination rules (plan_runtimerenderer.md design decision 11) ---
#
# Not every pair of renderers can be linked into one binary. Where they cannot, the build must say
# so AT CONFIGURE TIME with a reason -- never leave it to surface as a duplicate-symbol link error
# or, worse, as a runtime failure in a build that appeared to succeed.
#
# Every rule here is a real, demonstrated conflict, not a precaution. The reason is stated with each
# one so a user who hits it can tell whether their case is genuinely covered.
#
# Kept in lockstep with docs/runtime-renderer-selection.md by
# scripts/check_renderer_combinations.py.

# Real-OpenGL families. PORTABLEGL cannot join them: it is a single-header C library that DEFINES
# the global gl* symbols (glClear, glDrawArrays, ...) as ordinary functions, so linking it beside a
# real GL renderer is a duplicate-symbol error by construction.
set(CNA_RENDERER_REAL_GL_FAMILIES
    OPENGLES2 OPENGLES3 OPENGL33 WEBGL1 WEBGL2   # EasyGL
    OPENGL1 OPENGL2 OPENGL4 OPENGLES1            # native GL
    OPENVG                                        # ShivaVG on a real GL context
    MAGNUM                                        # Magnum's own GL wrappers
    SOKOL)                                        # sokol_gfx on GL

# Identities served by one implementation family. Two of them in one binary would mean compiling
# that family twice with different profile defines -- an ODR violation, not merely a name clash.
# Lifting this is plan_runtimerenderer.md phase P11 (EasyGL's profile becomes a runtime choice).
set(CNA_RENDERER_SHARED_EASYGL OPENGLES2 OPENGLES3 OPENGL33 WEBGL1 WEBGL2)

# Platform partitions. A combination spanning two of these can never build: the toolchain targets
# one of them at a time.
set(CNA_RENDERER_WINDOWS_ONLY
    DIRECTX1 DIRECTX2 DIRECTX3 DIRECTX5 DIRECTX6 DIRECTX7 DIRECTX8 DIRECTX9 DIRECTX10
    DIRECTX11 DIRECTX12 DIRECT2D GLIDE GDI)
set(CNA_RENDERER_EMSCRIPTEN_ONLY WEBGL1 WEBGL2 CANVAS HTML_DOM SVG_DOM)
set(CNA_RENDERER_MACOS_ONLY METAL)

# Rejects an unbuildable combination with a specific, actionable reason.
function(_cna_reject_combination first second reason)
    message(FATAL_ERROR
        "CNA: renderers ${first} and ${second} cannot be built into the same binary.\n"
        "  Reason: ${reason}\n"
        "  See docs/runtime-renderer-selection.md for the full combination table.")
endfunction()

# Returns the platform partition an identity belongs to, or "ANY".
function(_cna_renderer_platform identity out_var)
    if(identity IN_LIST CNA_RENDERER_WINDOWS_ONLY)
        set(${out_var} "Windows" PARENT_SCOPE)
    elseif(identity IN_LIST CNA_RENDERER_EMSCRIPTEN_ONLY)
        set(${out_var} "Emscripten" PARENT_SCOPE)
    elseif(identity IN_LIST CNA_RENDERER_MACOS_ONLY)
        set(${out_var} "macOS" PARENT_SCOPE)
    else()
        set(${out_var} "ANY" PARENT_SCOPE)
    endif()
endfunction()

# Validates a renderer identity list, failing the configure on any unbuildable pair.
# A single-entry list -- the default and recommended mode -- always passes.
function(cna_validate_renderer_combination)
    set(_identities ${ARGN})
    list(LENGTH _identities _count)
    if(_count LESS_EQUAL 1)
        return()
    endif()

    # GLIDE targets the native 32-bit x86 Glide ABI and pins the whole binary to it, so it cannot
    # share a build with anything else regardless of what that other renderer is.
    if("GLIDE" IN_LIST _identities)
        list(REMOVE_ITEM _identities "GLIDE")
        list(GET _identities 0 _other)
        _cna_reject_combination("GLIDE" "${_other}"
            "GLIDE pins the build to the native 32-bit (x86) Glide ABI, which every other renderer "
            "in this project builds against a 64-bit toolchain.")
    endif()

    foreach(_first IN LISTS _identities)
        foreach(_second IN LISTS _identities)
            if(_first STREQUAL _second)
                continue()
            endif()

            # PORTABLEGL's global gl* symbols vs any real GL renderer.
            if(_first STREQUAL "PORTABLEGL" AND _second IN_LIST CNA_RENDERER_REAL_GL_FAMILIES)
                _cna_reject_combination("${_first}" "${_second}"
                    "PORTABLEGL is a single-header C library that DEFINES the global gl* symbols "
                    "(glClear, glDrawArrays, ...). Linking it beside a renderer that calls the real "
                    "OpenGL of the same names is a duplicate-symbol error.")
            endif()

            # GDI re-compiles the SOFTWARE translation units with CNA_SOFTWARE_2D_ONLY.
            if(_first STREQUAL "GDI" AND _second STREQUAL "SOFTWARE")
                _cna_reject_combination("${_first}" "${_second}"
                    "GDI compiles the SOFTWARE module's own translation units a second time with "
                    "CNA_SOFTWARE_2D_ONLY. Having both in one binary would define the same "
                    "functions twice with different bodies -- an ODR violation, not just a clash.")
            endif()

            # Two identities served by the shared EasyGL implementation.
            if(_first IN_LIST CNA_RENDERER_SHARED_EASYGL AND _second IN_LIST CNA_RENDERER_SHARED_EASYGL)
                _cna_reject_combination("${_first}" "${_second}"
                    "both are served by the shared EasyGL implementation, whose GL profile is still "
                    "a compile-time choice. Making that profile a runtime choice is "
                    "plan_runtimerenderer.md phase P11.")
            endif()

            # Cross-platform pairs.
            _cna_renderer_platform("${_first}" _first_platform)
            _cna_renderer_platform("${_second}" _second_platform)
            if(NOT _first_platform STREQUAL "ANY" AND NOT _second_platform STREQUAL "ANY"
                    AND NOT _first_platform STREQUAL _second_platform)
                _cna_reject_combination("${_first}" "${_second}"
                    "${_first} builds only for ${_first_platform} and ${_second} only for "
                    "${_second_platform}; one toolchain cannot target both.")
            endif()
        endforeach()
    endforeach()
endfunction()
