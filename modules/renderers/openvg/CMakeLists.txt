# cna_thirdparty_shivavg (cmake/ThirdPartyOpenVG.cmake) is configured by RendererSelection.cmake's
# own OPENVG dispatch arm before this module is entered -- same "third-party target already exists
# by the time this file runs" shape as SOKOL's cna_sokol_headers.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE cna_thirdparty_shivavg)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
