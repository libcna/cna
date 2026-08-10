cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 WebGPU::WebGPU)
if(CNA_WEBGPU_RUNTIME_LIBRARY)
    add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
        VERBATIM)
endif()
