# =====================================================================================
# CNA platform selection (plans/plan_platform.md Task PLAT-11)
#
# Chooses which CNA::Platform implementation is compiled. Exactly one is built into a
# binary; the interfaces stay virtual anyway, because the conformance suite (PLAT-116)
# needs a second implementation available in one process.
#
# Renderer and platform are SEPARATE axes. CNA_GRAPHICS_RENDERER picks how pixels are
# produced; CNA_PLATFORM picks where the window, events and input come from. Some
# combinations are invalid (a GPU renderer needs a native window handle, which the
# headless and terminal platforms do not provide), and those are rejected where the
# capability is known -- not silently tolerated until something dereferences null.
#
# SDL3 is the one windowing implementation. Windows, X11, Wayland, macOS, iOS, Android and the
# browser are reached through SDL's own video drivers, and the native handle a renderer needs
# (HWND, Display* + XID, wl_display* + wl_surface*, ...) comes back through
# IPlatformWindow::GetNativeHandle() as NativeWindowSystem::Win32/X11/Wayland/....
# =====================================================================================

# `cmake -P` does not inherit the project's cmake_minimum_required() policy set. Keep the same
# IN_LIST semantics in the lightweight selection contract tests and in a normal project include.
cmake_policy(SET CMP0057 NEW)

set(CNA_PLATFORM "SDL3" CACHE STRING
        "Platform implementation (SDL3 | HEADLESS | TERMINAL)")

# Implemented today. TERMINAL is host-conditional: it is built on termios, and there is no
# Windows console path for it (plans/plan_platform.md Phase 10). Offering it on Windows would
# produce a configure that succeeds and a build that does not.
set(_cna_platforms_available SDL3 HEADLESS)
if(NOT WIN32)
    list(APPEND _cna_platforms_available TERMINAL)
endif()

# Reserved-but-unimplemented. These are recognised so that configuring with one fails
# LOUDLY, naming the plan, instead of silently falling back to SDL3 and producing a
# binary that is not what the user asked for. plans/plan_platform.md §12 records why each is
# out of scope; PLAT-11 is explicit that a reserved identifier must never degrade
# quietly.
#
# A host-conditional implementation is *reserved on the hosts it does not support*, so asking for
# it there produces the same loud refusal rather than an "unknown platform" that reads as a typo.
set(_cna_platforms_reserved SDL12 EMSCRIPTEN)
if(WIN32)
    list(APPEND _cna_platforms_reserved TERMINAL)
endif()

# Retired on 2026-10-06: CNA's own direct Win32, X11 and Wayland implementations. The window
# systems are still supported -- through SDL3 -- so the refusal says which value to use instead.
# Deliberately not an alias: CNA_PLATFORM=X11 meaning SDL3 would build something other than what
# the value names.
set(_cna_platforms_retired WIN32 X11 WAYLAND)

set_property(CACHE CNA_PLATFORM PROPERTY STRINGS ${_cna_platforms_available})

if(CNA_PLATFORM IN_LIST _cna_platforms_retired)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=${CNA_PLATFORM} was removed on 2026-10-06.\n"
        "CNA no longer carries its own direct Win32, X11 or Wayland platform implementation. "
        "Those window systems are supported through SDL3: configure with -DCNA_PLATFORM=SDL3 and "
        "SDL picks its windows, x11 or wayland video driver at run time (SDL_VIDEODRIVER selects "
        "one explicitly). Renderers still receive the native HWND, Display*/Window or "
        "wl_display*/wl_surface* through IPlatformWindow::GetNativeHandle().\n"
        "Available: ${_cna_platforms_available}\n"
        "See docs/platform-abstraction.md.")
endif()

if(CNA_PLATFORM IN_LIST _cna_platforms_reserved)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=${CNA_PLATFORM} is a reserved identifier that is NOT implemented.\n"
        "Only ${_cna_platforms_available} are available today.\n"
        "See plans/plan_platform.md -- SDL 1.2 and Emscripten are recorded in "
        "\"Possible future implementations (NOT in scope)\".\n"
        "TERMINAL exists (plans/plan_platform.md Phase 10) but is POSIX-only: it is built on "
        "termios and has no Windows console path.\n"
        "This is a hard error on purpose: falling back to SDL3 would build something other "
        "than what you asked for.")
endif()

if(NOT CNA_PLATFORM IN_LIST _cna_platforms_available)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=${CNA_PLATFORM} is not a known platform.\n"
        "Available: ${_cna_platforms_available}\n"
        "Reserved but unimplemented: ${_cna_platforms_reserved}")
endif()

message(STATUS "CNA: Using ${CNA_PLATFORM} platform implementation")

# The compile definition an implementation's own sources and the entrypoint header key
# off. Named CNA_PLATFORM_<NAME> to match the CNA_RENDERER_<NAME> convention. The directory
# definition reaches only this source tree; CNA::BuildConfig exports CNA_PLATFORM_DEFINE to
# consumers too, because a game's own main() includes CNA/Platform/Entrypoint.hpp (without it,
# a game that adds CNA as a subdirectory got no SDL_main rename on Android -- House Simulator's
# BL-13).
add_compile_definitions(CNA_PLATFORM_${CNA_PLATFORM})
set(CNA_PLATFORM_DEFINE "CNA_PLATFORM_${CNA_PLATFORM}")
