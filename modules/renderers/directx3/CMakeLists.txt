# plans/plan_dx3.md design decision 6: same confirmed minimal link set as DIRECTX1/DIRECTX2 -- ddraw + dxguid +
# RendererSurfaceInfo supplies the native window. No separate Direct3D import library is needed here either (DX30-0 spike confirmed
# IDirect3D2/IDirect3DDevice2 are still obtained purely via QueryInterface/CreateDevice, now off
# an IDirectDraw2 object instead of v1 -- see plans/plan_dx3.md section 1).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE ddraw dxguid)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
