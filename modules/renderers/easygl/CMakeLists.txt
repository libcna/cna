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
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_EASYGL_STOCK_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_EASYGL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_EASYGL_STOCK_EFFECT_HEADER
        "${CNA_EASYGL_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_EASYGL_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_EASYGL_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_EASYGL_STOCK_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_EASYGL_STOCK_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for EasyGL compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_easygl_sprite_effect
        DEPENDS "${CNA_EASYGL_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_easygl_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_EASYGL_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_EASYGL_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
