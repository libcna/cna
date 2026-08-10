cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PUBLIC bgfx bx)
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
target_include_directories(${RENDERER_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
