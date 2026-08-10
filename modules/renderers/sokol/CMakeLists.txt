# plan_sokol.md SOKOL-2/SOKOL-3: cna_sokol_headers (cmake/ThirdPartySokol.cmake) carries the
# fetched sokol include directory, the CNA_SOKOL_API define sokol_gfx.h dispatches on, and --
# for the GL APIs -- OpenGL::GL, since sokol_gfx.h has no GL loader outside Windows. SDL3
# supplies the window and, via SDL_GL_CreateContext, the GL context sokol_gfx renders into
# (design decision 1: CNA keeps owning the window and the game loop, so sokol_app is not used).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 cna_sokol_headers)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
