# =====================================================================================
# CNA renderer identity default (extracted from cmake/RendererSelection.cmake, plans/plan_x11.md
# X11-0090)
#
# Settles `_cna_default_renderer` -- which renderer a build uses when the user names none -- and
# nothing else. It is its own file because two independent consumers need the answer at two
# different points in the configure:
#
#   cmake/RendererSelection.cmake   uses it as the cache default for CNA_GRAPHICS_RENDERER.
#   cmake/SdlAvailability.cmake     needs to know whether the selected renderer requires SDL
#                                   BEFORE cna_configure_vendored_sdl() runs, which is earlier
#                                   than renderer selection.
#
# Restating the rule in the second file would be one line of duplication and one future evening
# spent working out why an SDL-free configuration stopped being SDL-free when the default moved.
#
# plans/plan_glbackends.md: EasyGL is an internal implementation family, not a public renderer
# name. It is selected publicly via one of 5 GL-profile names -- OPENGLES2/OPENGLES3/OPENGL33
# (desktop/mobile, non-Emscripten) and WEBGL1/WEBGL2 (Emscripten only). OPENGLES3 on Linux is the
# default GL-family choice; WEBGL2 is the default under Emscripten. Other platforms default to
# SDL_RENDERER.
# =====================================================================================

include_guard(GLOBAL)

if(EMSCRIPTEN)
    set(_cna_default_renderer "WEBGL2")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_cna_default_renderer "OPENGLES3")
else()
    set(_cna_default_renderer "SDL_RENDERER")
endif()
