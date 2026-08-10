# plan_dx7.md design decision 14: same confirmed minimal link set as DX1/DX2/DX3/DX5/DX6 --
# ddraw + dxguid + SDL3::SDL3. DirectDrawCreateEx/IDirectDraw7/IDirect3D7 all resolve from the
# same import libraries, spike-confirmed (DX7-0) with no new link dependency.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
