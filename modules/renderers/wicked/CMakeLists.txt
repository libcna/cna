# plans/plan_wicked.md WICKED-2: Wicked Engine vendors its own Vulkan headers and loads the loader
# through volk at runtime, so no find_package(Vulkan) is needed here (unlike the VULKAN renderer).
# WickedEngine is PUBLIC because WickedRenderer.hpp includes wiGraphicsDevice.h -- every
# target compiling against this renderer's header needs the same include directory and the same
# native-platform/WI_UNORDERED_MAP_TYPE compile definitions.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PUBLIC WickedEngine)
if(CNA_WICKED_DXCOMPILER)
    add_custom_command(TARGET ${RENDERER_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WICKED_DXCOMPILER}" "$<TARGET_FILE_DIR:${RENDERER_TARGET}>"
        VERBATIM)
endif()
