cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PUBLIC bgfx bx)
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
target_include_directories(${BACKEND_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
