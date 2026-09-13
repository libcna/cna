# plans/plan_rlgl.md RLGL-005: standalone rlgl is a header implementation. This target receives
# only rlgl's src/ include root and GRAPHICS_API_OPENGL_33; it does not link the raylib framework.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE cna_rlgl_headers)

if(CNA_RLGL_COMPILED_EFFECTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_RLGL_STOCK_SPRITE_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_RLGL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_RLGL_STOCK_EFFECT_HEADER
        "${CNA_RLGL_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_RLGL_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_RLGL_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_RLGL_STOCK_SPRITE_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_RLGL_STOCK_SPRITE_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for RLGL compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_rlgl_sprite_effect
        DEPENDS "${CNA_RLGL_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_rlgl_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_RLGL_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_RLGL_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

add_subdirectory(examples)
