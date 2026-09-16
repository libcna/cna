# =====================================================================================
# SDL2-only configuration guard
#
# A renderer is permitted to use the selected CNA platform services, but an implementation
# which links SDL3 itself would defeat an otherwise SDL2-only platform/audio configuration.
# Keep this selection-time check independent of target construction so it fails before a large
# dependency graph is generated.
# =====================================================================================

# This file also runs through `cmake -P` in the lightweight gate test.
cmake_policy(SET CMP0057 NEW)

# Two SDL major versions in one process is the hazard this whole file exists for (see the
# comment below), so the two selections that produce one are refused before anything is
# generated. Found by plans/plan_wayland.md WAYLAND-0128's regression matrix: `CNA_PLATFORM=SDL2`
# with the *default* audio platform built and linked both libraries and then died with a jump
# through a null vtable on the first window, and the reverse pair failed with CMake's own
# "SDL2::SDL2 target not found" rather than a diagnostic anyone could act on.
if(CNA_PLATFORM STREQUAL "SDL2" AND CNA_AUDIO_PLATFORM STREQUAL "SDL3")
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=SDL2 with CNA_AUDIO_PLATFORM=SDL3 puts SDL2 and SDL3 in one process. "
        "They export identically named entry points (SDL_Init, SDL_GetError, SDL_PollEvent and "
        "many more), so each backend's calls bind to whichever library the loader reached first "
        "and neither implementation is the one running.\n"
        "Choose an audio platform this platform can share a process with: "
        "-DCNA_AUDIO_PLATFORM=SDL2 (the same library), NULL (silent) or ALSA (CNA's own mixer).")
endif()
if(CNA_PLATFORM STREQUAL "SDL3" AND CNA_AUDIO_PLATFORM STREQUAL "SDL2")
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=SDL3 with CNA_AUDIO_PLATFORM=SDL2 puts SDL2 and SDL3 in one process, "
        "which cannot work: they export identically named entry points and each backend's calls "
        "would bind to whichever library the loader reached first.\n"
        "Choose -DCNA_AUDIO_PLATFORM=SDL3 (the same library), NULL (silent) or ALSA (CNA's own "
        "mixer), or select -DCNA_PLATFORM=SDL2 to keep SDL2 audio.")
endif()

if(CNA_PLATFORM STREQUAL "SDL2" AND CNA_AUDIO_PLATFORM STREQUAL "SDL2")
    set(_cna_renderers_with_direct_sdl3 SDL_RENDERER SDL_GPU FNA3D FREEDIRECT)
    if(CNA_GRAPHICS_RENDERER IN_LIST _cna_renderers_with_direct_sdl3)
        message(FATAL_ERROR
            "CNA: CNA_PLATFORM=SDL2 and CNA_AUDIO_PLATFORM=SDL2 require a renderer without "
            "a direct SDL3 dependency, but CNA_GRAPHICS_RENDERER=${CNA_GRAPHICS_RENDERER} "
            "links SDL3 itself. Choose OPENGLES2/OPENGLES3/OPENGL33, VULKAN, SOFTWARE, "
            "HEADLESS or another SDL-independent renderer.")
    endif()
    # plans/plan_platform.md PLAT-SDL2-6: once the check above has passed, no production target in this
    # selection needs SDL3, and the test and harness layer must not quietly put it back. SDL2 and
    # SDL3 export identically named entry points -- SDL_Init, SDL_GetError, SDL_PollEvent and many
    # more -- so a binary linking both leaves the SDL2 backend's own calls bound to whichever
    # library the loader reached first. A conformance run over such a binary would be testing
    # neither implementation while reporting success, which is worse than not running it.
    #
    # Published as a normal variable rather than a cache entry: it is derived from two selections
    # that are themselves cached, so caching it as well would let a stale copy outlive a change to
    # either one.
    set(CNA_SDL2_ONLY_CONFIGURATION ON)
else()
    set(CNA_SDL2_ONLY_CONFIGURATION OFF)
endif()
