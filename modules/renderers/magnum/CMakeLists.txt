# plan_magnum.md MAGNUM-2: Magnum::GL is the whole rendering surface this backend uses, and
# Magnum::<platform>Context supplies the OpenGL function loader Platform::GLContext needs for a
# context CNA created itself through SDL3 (which stays the window/context owner, exactly as in
# every other windowed backend here). CNA_MAGNUM_CONTEXT_COMPONENT is resolved per platform in
# cmake/ThirdPartyMagnum.cmake.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE
    SDL3::SDL3 Magnum::GL Magnum::${CNA_MAGNUM_CONTEXT_COMPONENT} Magnum::Magnum)
