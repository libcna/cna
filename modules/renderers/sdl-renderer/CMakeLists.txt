if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
    cna_add_renderer()
    target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
