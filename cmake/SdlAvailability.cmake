# =====================================================================================
# CNA SDL availability gate (plans/plan_x11.md Task X11-0090)
#
# Until this file existed, `cna_configure_vendored_sdl()` ran unconditionally from the root
# CMakeLists.txt, before platform, audio and renderer selection. That made SDL an
# *existential* dependency of CNA: `-DCNA_PLATFORM=HEADLESS -DCNA_AUDIO_PLATFORM=NULL
# -DCNA_GRAPHICS_RENDERER=HEADLESS` -- a configuration that references no SDL symbol
# anywhere -- still failed to configure without the SDL submodules, and still spent several
# minutes building SDL3, SDL3_image and SDL3_mixer that nothing would link.
#
# CNA_ENABLE_SDL decides whether SDL is configured at all:
#
#   AUTO (default)  SDL is configured exactly as before. Byte-for-byte today's behaviour, so
#                   no existing build changes in any way.
#   ON              The same, stated explicitly.
#   OFF             SDL is not downloaded, not built, not found and not linked. Any selection
#                   that genuinely needs it is rejected at configure time, naming which one --
#                   never silently substituted.
#
# The point is not to remove SDL. SDL3 remains CNA's default platform and an excellent one.
# The point is that "CNA without SDL" must be a configuration that exists, because a framework
# whose abstraction layer cannot be built without the thing it abstracts has not abstracted it.
# =====================================================================================

include_guard(GLOBAL)

set(CNA_ENABLE_SDL "AUTO" CACHE STRING
    "Configure the vendored SDL3 dependency (OFF, AUTO, or ON)")
set_property(CACHE CNA_ENABLE_SDL PROPERTY STRINGS OFF AUTO ON)

string(TOUPPER "${CNA_ENABLE_SDL}" _cna_enable_sdl_normalized)
if(_cna_enable_sdl_normalized STREQUAL "TRUE" OR _cna_enable_sdl_normalized STREQUAL "1")
    set(_cna_enable_sdl_normalized "ON")
elseif(_cna_enable_sdl_normalized STREQUAL "FALSE" OR _cna_enable_sdl_normalized STREQUAL "0"
        OR _cna_enable_sdl_normalized STREQUAL "NO")
    set(_cna_enable_sdl_normalized "OFF")
endif()
if(NOT _cna_enable_sdl_normalized MATCHES "^(OFF|AUTO|ON)$")
    message(FATAL_ERROR
        "CNA_ENABLE_SDL must be OFF, AUTO, or ON (received '${CNA_ENABLE_SDL}').")
endif()

# --- which selections genuinely need SDL ---------------------------------------------------------
#
# Read from the selections themselves rather than from a maintained list of "SDL-ish" things:
# the platform and audio axes name SDL directly, and the renderer allowlist is the one
# `tools/platform/renderer_sdl_audit.py` already enforces (docs/platform-renderer-sdl-audit.md).
# A renderer family that starts using SDL without joining that allowlist fails that gate, so
# this list cannot silently drift out of date.
# The renderer this build will use, resolved the same way renderer selection itself resolves it.
# Including the shared default rule rather than restating it is what keeps the two from drifting:
# an SDL-free configuration that silently stopped being SDL-free when the per-host default moved
# would be the exact failure this gate exists to prevent.
include("${CMAKE_CURRENT_LIST_DIR}/RendererIdentityDefault.cmake")
if(NOT DEFINED CNA_GRAPHICS_RENDERER OR CNA_GRAPHICS_RENDERER STREQUAL "")
    set(_cna_sdl_effective_renderer "${_cna_default_renderer}")
else()
    set(_cna_sdl_effective_renderer "${CNA_GRAPHICS_RENDERER}")
endif()

set(_cna_sdl_reasons "")
if(CNA_PLATFORM STREQUAL "SDL3" OR CNA_PLATFORM STREQUAL "SDL2")
    list(APPEND _cna_sdl_reasons "CNA_PLATFORM=${CNA_PLATFORM}")
endif()
if(CNA_AUDIO_PLATFORM STREQUAL "SDL3" OR CNA_AUDIO_PLATFORM STREQUAL "SDL2")
    list(APPEND _cna_sdl_reasons "CNA_AUDIO_PLATFORM=${CNA_AUDIO_PLATFORM}")
endif()
foreach(_cna_sdl_renderer IN ITEMS SDL_RENDERER SDL_GPU FNA3D FREEDIRECT)
    if(_cna_sdl_effective_renderer STREQUAL "${_cna_sdl_renderer}"
            OR "${_cna_sdl_renderer}" IN_LIST CNA_GRAPHICS_RENDERERS)
        list(APPEND _cna_sdl_reasons "CNA_GRAPHICS_RENDERER=${_cna_sdl_renderer}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _cna_sdl_reasons)

if(_cna_enable_sdl_normalized STREQUAL "OFF" AND _cna_sdl_reasons)
    list(JOIN _cna_sdl_reasons ", " _cna_sdl_reason_text)
    message(FATAL_ERROR
        "CNA: CNA_ENABLE_SDL=OFF, but this configuration genuinely requires SDL: "
        "${_cna_sdl_reason_text}.\n"
        "Nothing is substituted for it, deliberately -- silently swapping in another backend "
        "would build something other than what you asked for.\n"
        "For an SDL-free build select a native platform, audio and renderer, for example:\n"
        "  -DCNA_PLATFORM=X11 -DCNA_AUDIO_PLATFORM=NULL -DCNA_GRAPHICS_RENDERER=HEADLESS\n"
        "See docs/platform-x11.md.")
endif()

if(_cna_enable_sdl_normalized STREQUAL "OFF")
    set(CNA_ENABLE_SDL OFF CACHE STRING "Configure the vendored SDL3 dependency (OFF, AUTO, or ON)" FORCE)
    # "Not found" has to hold for every project this build adds, not only for CNA's own CMake:
    # plans/plan_native_platform_validation.md NPV-0114 found the sibling easy-gl's example
    # directory running find_package(SDL3 QUIET), picking up an SDL3 installed in /usr/local and
    # adding an SDL-linked executable to the default target of a CNA_ENABLE_SDL=OFF build. With
    # these set, an optional lookup anywhere in the tree finds nothing and a REQUIRED one fails
    # configure, naming the package.
    foreach(_cna_sdl_package IN ITEMS SDL3 SDL3_image SDL3_mixer SDL3_ttf SDL3_net
                                      SDL2 SDL2_image SDL2_mixer SDL2_ttf SDL2_net SDL)
        set(CMAKE_DISABLE_FIND_PACKAGE_${_cna_sdl_package} TRUE)
    endforeach()
    unset(_cna_sdl_package)
    message(STATUS
        "CNA: SDL is NOT configured (CNA_ENABLE_SDL=OFF). No SDL source is fetched, built, "
        "found or linked by this configuration.")
else()
    set(CNA_ENABLE_SDL ON CACHE STRING "Configure the vendored SDL3 dependency (OFF, AUTO, or ON)" FORCE)
endif()
unset(_cna_enable_sdl_normalized)
unset(_cna_sdl_reasons)
unset(_cna_sdl_reason_text)
unset(_cna_sdl_effective_renderer)
