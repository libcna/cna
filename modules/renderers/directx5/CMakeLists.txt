# plan_dx5.md design decision 9: same confirmed minimal link set as DIRECTX1/DIRECTX2/DIRECTX3 -- ddraw +
# dxguid. RendererSurfaceInfo supplies the native window. No separate Direct3D import library is needed here either (DX5-0 spike
# confirmed IDirect3D3/IDirect3DDevice3 are still obtained purely via QueryInterface/
# CreateDevice, now off an IDirectDraw4 object instead of v2 -- see plan_dx5.md section 1).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE ddraw dxguid)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
