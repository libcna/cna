if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
    cna_add_renderer()
    target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_RENDERER STREQUAL "ASCII")
    # plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
    # SdlRenderer (composition, not reimplementation) -- it needs SdlRenderer.cpp's
    # real implementation compiled in even though CNA_GRAPHICS_RENDERER=SDL_RENDERER was not itself
    # selected. Mirrors the D3DCommon shared-core pattern: a small static lib, built only when
    # actually needed. CNA_RENDERER_SDL_RENDERER is deliberately NOT defined for this build, so
    # SdlRenderer.cpp's own #ifdef CNA_RENDERER_SDL_RENDERER-guarded CreateGraphicsRenderer()
    # is compiled out here, leaving Ascii's own CreateGraphicsRenderer() (guarded by
    # CNA_RENDERER_ASCII) as the only factory definition -- no duplicate-symbol clash.
    file(GLOB CNA_ASCII_SDLRENDERER_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
    add_library(cna_renderer_sdl_renderer_core STATIC ${CNA_ASCII_SDLRENDERER_SOURCES})
    target_link_libraries(cna_renderer_sdl_renderer_core PUBLIC cna_renderer_common)
    cna_link_sharp_runtime(cna_renderer_sdl_renderer_core PUBLIC)
    target_include_directories(cna_renderer_sdl_renderer_core PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cna_renderer_sdl_renderer_core PRIVATE SDL3::SDL3)
    # Same reverse edges as every renderer target: the shared core calls CNA-owned symbols and
    # compiles against the graphics/math/core public headers.
    target_link_libraries(cna_renderer_sdl_renderer_core PRIVATE cna_graphics_core cna_core cna_math)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
