cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE Vulkan::Vulkan)

# plans/plan_fx.md FX-065: compiled XNA effects go through MojoShader's portable SPIR-V profile plus the
# translation shared with every other compiled-effect backend. Opt-in, because MojoShader is a
# fetched dependency this renderer does not otherwise need. Unlike the SDL_GPU and EasyGL
# families there is no MojoShader-provided adapter to link -- the effect backend is CNA's own.
if(CNA_VULKAN_COMPILED_EFFECTS)
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
