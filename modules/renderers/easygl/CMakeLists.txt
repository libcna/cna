# The EasyGL family implements the five public GL-profile identities
# (OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2) on top of the sibling easy-gl -> meta-gl chain
# (plans/plan_glbackends.md); the profile define CNA_GL_PROFILE_* is applied by
# RendererSelection.cmake at identity-selection time.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE easy-gl)

# plans/plan_fx.md FX-062: compiled XNA effects go through MojoShader's own OpenGL adapter plus the
# translation shared with every other compiled-effect backend. Opt-in, because MojoShader is a
# fetched dependency this renderer does not otherwise need.
if(CNA_EASYGL_COMPILED_EFFECTS)
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
