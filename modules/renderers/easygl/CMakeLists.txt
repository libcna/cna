# The EasyGL family implements the five public GL-profile identities
# (OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2) on top of the sibling easy-gl -> meta-gl chain
# (plan_glbackends.md); the profile define CNA_GL_PROFILE_* is applied by
# RendererSelection.cmake at identity-selection time.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE easy-gl)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
