cna_add_renderer()
find_package(OpenGL REQUIRED)
target_link_libraries(${RENDERER_TARGET} PRIVATE OpenGL::GL)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
