cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE Vulkan::Vulkan)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
