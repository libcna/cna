cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
# PUBLIC (not PRIVATE): AsciiRenderer.hpp names the SdlRenderer decorated core through
# its canonical CNA/Internal/Renderers/SdlRenderer spelling, so every consumer compiling this
# renderer's header needs the sdl-renderer module's include root; the archive was already in
# every consumer link closure through the former private edge.
target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_sdl_renderer_core)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
