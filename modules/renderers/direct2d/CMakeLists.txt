# Direct2D 1.1 presents through a BGRA-capable D3D11/DXGI flip-model swap chain. d2d1 is
# the renderer; d3d11/dxgi provide only the device and presentation surface it targets.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE d2d1 d3d11 dxgi)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
