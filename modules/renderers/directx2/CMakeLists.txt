# plan_dx2.md design decision 10: same confirmed minimal link set as DIRECTX1 -- ddraw + dxguid +
# SDL3::SDL3. No separate Direct3D import library is needed: IDirect3D2/IDirect3DDevice2 are
# obtained purely via QueryInterface/CreateDevice on DirectDraw objects/surfaces (confirmed
# during the DX2-0 spike, see plan_dx2.md section 1).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
