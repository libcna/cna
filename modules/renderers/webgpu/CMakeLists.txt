cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 WebGPU::WebGPU)
if(CNA_WEBGPU_RUNTIME_LIBRARY)
    add_custom_command(TARGET ${RENDERER_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${RENDERER_TARGET}>"
        VERBATIM)
endif()
