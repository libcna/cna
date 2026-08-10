cna_add_renderer()
find_package(OpenGL REQUIRED)
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)
