# plan_dx5.md design decision 9: same confirmed minimal link set as DX1/DX2/DX3 -- ddraw +
# dxguid + SDL3::SDL3. No separate Direct3D import library is needed here either (DX5-0 spike
# confirmed IDirect3D3/IDirect3DDevice3 are still obtained purely via QueryInterface/
# CreateDevice, now off an IDirectDraw4 object instead of v2 -- see plan_dx5.md section 1).
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
