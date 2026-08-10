cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
# PUBLIC (not PRIVATE): AsciiGraphicsBackend.hpp names the SdlRenderer decorated core through
# its canonical CNA/Internal/Backends/SdlRenderer spelling, so every consumer compiling this
# backend's header needs the sdl-renderer module's include root; the archive was already in
# every consumer link closure through the former private edge.
target_link_libraries(${BACKEND_TARGET} PUBLIC cna_backend_graphics_sdl_renderer_core)
