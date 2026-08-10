cna_add_renderer_backend()
find_package(OpenGL REQUIRED)
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)
