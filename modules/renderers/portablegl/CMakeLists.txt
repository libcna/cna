# plan-level note: cna_portablegl_headers (cmake/ThirdPartyPortableGL.cmake) carries the fetched
# PortableGL include directory -- fetched by cmake/RendererSelection.cmake's own
# cna_configure_portablegl() call (mirroring cna_configure_sokol()'s identical call site) before
# this directory is entered. PortableGL is a CPU software OpenGL 3.x-ish implementation -- unlike
# sokol/bgfx/wicked it never talks to a native GPU API, so no system SDK/library needs to be
# linked here, only the header include path.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE cna_portablegl_headers)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
