# plan_magnum.md MAGNUM-2: Magnum::GL is the whole rendering surface this renderer uses, and
# Magnum::<platform>Context supplies the OpenGL function loader Platform::GLContext needs for a
# context CNA creates through IPlatformGlContext. CNA_MAGNUM_CONTEXT_COMPONENT is resolved per platform in
# cmake/ThirdPartyMagnum.cmake.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE
    Magnum::GL Magnum::${CNA_MAGNUM_CONTEXT_COMPONENT} Magnum::Magnum)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
