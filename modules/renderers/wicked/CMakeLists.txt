# plan_wicked.md WICKED-2: Wicked Engine vendors its own Vulkan headers and loads the loader
# through volk at runtime, so no find_package(Vulkan) is needed here (unlike the VULKAN backend).
# WickedEngine is PUBLIC because WickedGraphicsBackend.hpp includes wiGraphicsDevice.h -- every
# target compiling against this backend's header needs the same include directory and the same
# SDL3/WI_UNORDERED_MAP_TYPE compile definitions.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PUBLIC WickedEngine)
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
if(CNA_WICKED_DXCOMPILER)
    add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WICKED_DXCOMPILER}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
        VERBATIM)
endif()
