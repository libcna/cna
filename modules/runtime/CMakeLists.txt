file(GLOB CNA_RUNTIME_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_runtime Runtime ${CNA_RUNTIME_SOURCES})
target_link_libraries(cna_runtime PUBLIC
        cna_graphics_core cna_input cna_content cna_audio cna_media cna_core cna_math)
cna_link_sharp_runtime(cna_runtime PUBLIC Core.Base IO)
# plan_platform.md: cna_platform for the contract TitleContainer/TitleLocation now use.
# SDL is still linked for Game.cpp/GameWindow.cpp/GraphicsDeviceManager.cpp, which Phase 3
# (PLAT-46..52) migrates; it goes when those do.
target_link_libraries(cna_runtime PUBLIC cna_platform)
target_link_libraries(cna_runtime PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
