# plan_opengl4.md GL4-1: real desktop OpenGL 4.x core profile via the platform's own GL
# library (libGL/opengl32/OpenGL.framework, resolved by find_package(OpenGL) in
# RendererSelection.cmake) plus SDL3 for window/context management -- no other new
# third-party dependency (GL4Loader.hpp/.cpp is this renderer's own hand-rolled loader for
# the GL 1.2+ entry points a core profile needs).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)
