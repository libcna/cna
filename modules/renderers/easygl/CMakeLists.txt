# The EasyGL family implements the four public GL-profile identities
# (OPENGLES/OPENGL33/WEBGL1/WEBGL2) on top of the sibling easy-gl -> meta-gl chain
# (plan_glbackends.md); the profile define CNA_GL_PROFILE_* is applied by
# BackendSelection.cmake at identity-selection time.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE easy-gl SDL3::SDL3)
