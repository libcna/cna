file(GLOB CNA_RUNTIME_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_runtime Runtime ${CNA_RUNTIME_SOURCES})
target_link_libraries(cna_runtime PUBLIC
        cna_graphics_core cna_input cna_content cna_audio cna_media cna_core cna_math)
cna_link_sharp_runtime(cna_runtime PUBLIC Core.Base IO)
# plan_platform.md: cna_platform owns every native service used by Runtime.
target_link_libraries(cna_runtime PUBLIC cna_platform)
