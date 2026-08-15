# plan_opengl4.md GL4-1: real desktop OpenGL 4.x core profile via the platform's own GL
# library (libGL/opengl32/OpenGL.framework, resolved by find_package(OpenGL) in
# RendererSelection.cmake); window/context management comes from IPlatformGlContext -- no other new
# third-party dependency (GL4Loader.hpp/.cpp is this renderer's own hand-rolled loader for
# the GL 1.2+ entry points a core profile needs).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE OpenGL::GL)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
