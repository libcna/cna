# plan_dx3.md design decision 6: same confirmed minimal link set as DX1/DX2 -- ddraw + dxguid +
# SDL3::SDL3. No separate Direct3D import library is needed here either (DX30-0 spike confirmed
# IDirect3D2/IDirect3DDevice2 are still obtained purely via QueryInterface/CreateDevice, now off
# an IDirectDraw2 object instead of v1 -- see plan_dx3.md section 1).
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
