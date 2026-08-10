# plan_dx1.md design decision 10: ddraw + dxguid (GUID storage for IID_IDirectDraw etc.) is the
# confirmed minimal link set (DX1-0 spike) -- no free-direct, no DXVK, no d3dcompiler, no
# d3d11/dxgi. SDL3 is linked only for window/HWND access (design decision 3), same as every
# other renderer.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
