cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE Vulkan::Vulkan)

# plans/plan_fx.md FX-065: compiled XNA effects go through MojoShader's portable SPIR-V profile plus the
# translation shared with every other compiled-effect backend. Opt-in, because MojoShader is a
# fetched dependency this renderer does not otherwise need. Unlike the SDL_GPU and EasyGL
# families there is no MojoShader-provided adapter to link -- the effect backend is CNA's own.
if(CNA_VULKAN_COMPILED_EFFECTS)
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
