# plan-level note: the `tinygl-static` target (cmake/ThirdPartyTinyGL.cmake) carries the fetched
# TinyGL archive and its public include directory -- fetched by cmake/RendererSelection.cmake's own
# cna_configure_tinygl() call before this directory is entered. TinyGL is a CPU implementation of a
# fixed-function OpenGL 1.x subset, so, like PortableGL, it never talks to a native GPU API and no
# system graphics SDK is linked here.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE tinygl-static)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local.
add_subdirectory(examples)
