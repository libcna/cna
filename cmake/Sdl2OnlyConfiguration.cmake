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

if(CNA_PLATFORM STREQUAL "SDL2" AND CNA_AUDIO_PLATFORM STREQUAL "SDL2")
    set(_cna_renderers_with_direct_sdl3 SDL_RENDERER SDL_GPU FNA3D FREEDIRECT)
    if(CNA_GRAPHICS_RENDERER IN_LIST _cna_renderers_with_direct_sdl3)
        message(FATAL_ERROR
            "CNA: CNA_PLATFORM=SDL2 and CNA_AUDIO_PLATFORM=SDL2 require a renderer without "
            "a direct SDL3 dependency, but CNA_GRAPHICS_RENDERER=${CNA_GRAPHICS_RENDERER} "
            "links SDL3 itself. Choose OPENGLES2/OPENGLES3/OPENGL33, VULKAN, SOFTWARE, "
            "HEADLESS or another SDL-independent renderer.")
    endif()
endif()
