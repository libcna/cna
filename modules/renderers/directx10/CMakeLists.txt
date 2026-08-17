# plan_d3d10.md design decision: unlike DIRECTX8, mingw-w64 ships a REAL d3d10 import library
# (libd3d10.a) -- no DXVK .dll.a linking hack needed. DXVK itself ships no d3d10.dll at all
# (only d3d10core.dll); the real d3d10.dll/d3d10_1.dll come from Wine's own builtin, which
# forward to d3d10core (overridden to DXVK's real implementation at the Wine-prefix level, not
# a link-time concern).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE d3d10 dxgi d3dcompiler)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
