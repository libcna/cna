cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PUBLIC bgfx bx)
target_include_directories(${RENDERER_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
