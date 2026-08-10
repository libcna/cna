if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    cna_add_renderer_backend()
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    # plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
    # SdlGraphicsBackend (composition, not reimplementation) -- it needs SdlGraphicsBackend.cpp's
    # real implementation compiled in even though CNA_GRAPHICS_BACKEND=SDL_RENDERER was not itself
    # selected. Mirrors the D3DCommon shared-core pattern: a small static lib, built only when
    # actually needed. CNA_BACKEND_SDL_RENDERER is deliberately NOT defined for this build, so
    # SdlGraphicsBackend.cpp's own #ifdef CNA_BACKEND_SDL_RENDERER-guarded CreateGraphicsBackend()
    # is compiled out here, leaving Ascii's own CreateGraphicsBackend() (guarded by
    # CNA_BACKEND_ASCII) as the only factory definition -- no duplicate-symbol clash.
    file(GLOB CNA_ASCII_SDLRENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
    add_library(cna_backend_graphics_sdl_renderer_core STATIC ${CNA_ASCII_SDLRENDERER_SOURCES})
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PUBLIC cna_backend_graphics_common)
    cna_link_sharp_runtime(cna_backend_graphics_sdl_renderer_core PUBLIC)
    target_include_directories(cna_backend_graphics_sdl_renderer_core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PRIVATE SDL3::SDL3)
    # Same reverse edges as every backend target: the shared core calls CNA-owned symbols and
    # compiles against the graphics/math/core public headers.
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PRIVATE cna_graphics_core cna_core cna_math)
endif()
