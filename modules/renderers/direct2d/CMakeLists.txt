# Direct2D 1.1 presents through a BGRA-capable D3D11/DXGI flip-model swap chain. d2d1 is
# the renderer; d3d11/dxgi provide only the device and presentation surface it targets.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 d2d1 d3d11 dxgi)
