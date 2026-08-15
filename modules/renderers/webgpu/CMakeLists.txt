if(APPLE)
    file(GLOB CNA_WEBGPU_OBJCXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
endif()
cna_add_renderer(${CNA_WEBGPU_OBJCXX_SOURCES})
target_link_libraries(${RENDERER_TARGET} PRIVATE WebGPU::WebGPU)
if(APPLE)
    target_link_libraries(${RENDERER_TARGET} PRIVATE "-framework AppKit" "-framework QuartzCore")
endif()
if(CNA_WEBGPU_RUNTIME_LIBRARY)
    add_custom_command(TARGET ${RENDERER_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${RENDERER_TARGET}>"
        VERBATIM)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
