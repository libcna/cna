cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE Vulkan::Vulkan)

# plans/plan_fx.md FX-065: compiled XNA effects go through MojoShader's portable SPIR-V profile plus the
# translation shared with every other compiled-effect backend. Opt-in, because MojoShader is a
# fetched dependency this renderer does not otherwise need. Unlike the SDL_GPU and EasyGL
# families there is no MojoShader-provided adapter to link -- the effect backend is CNA's own.
if(CNA_VULKAN_COMPILED_EFFECTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_VULKAN_STOCK_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_VULKAN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_VULKAN_STOCK_EFFECT_HEADER
        "${CNA_VULKAN_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_VULKAN_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_VULKAN_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_VULKAN_STOCK_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_VULKAN_STOCK_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for Vulkan compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_vulkan_sprite_effect
        DEPENDS "${CNA_VULKAN_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_vulkan_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_VULKAN_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_VULKAN_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# plans/plan_vulkan.md VULKAN-020: VulkanRenderer::SupportsCapability answers every member of
# CNA::GraphicsCapability explicitly and has no `default:` arm, so that appending a member to that
# enum cannot silently be claimed by this renderer. That guarantee is only worth anything if the
# resulting -Wswitch is fatal -- otherwise it is a warning in a build that emits many. Scoped to
# this target and to that one warning, following cmake/Harnesses.cmake's own precedent of putting
# -Werror=deprecated-declarations on the target whose job is to catch the thing.
#
# All three of this family's translation units are -Wswitch clean today (measured 2026-09-04), so
# this adds no pre-existing noise; a new incomplete switch anywhere in the family will now stop the
# build, which is the intended cost.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${RENDERER_TARGET} PRIVATE -Werror=switch)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
