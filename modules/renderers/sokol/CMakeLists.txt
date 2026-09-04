# plans/plan_sokol.md SOKOL-2/SOKOL-3: cna_sokol_headers (cmake/ThirdPartySokol.cmake) carries the
# fetched sokol include directory, the CNA_SOKOL_API define sokol_gfx.h dispatches on, and --
# for the GL APIs -- OpenGL::GL, since sokol_gfx.h has no GL loader outside Windows. CNA's platform
# service supplies the current context; CNA keeps owning the window and game loop, so sokol_app is
# not used.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE cna_sokol_headers)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
