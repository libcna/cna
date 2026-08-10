# The EasyGL family implements the four public GL-profile identities
# (OPENGLES3/OPENGL33/WEBGL1/WEBGL2) on top of the sibling easy-gl -> meta-gl chain
# (plan_glbackends.md); the profile define CNA_GL_PROFILE_* is applied by
# RendererSelection.cmake at identity-selection time.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE easy-gl SDL3::SDL3)
