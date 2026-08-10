# plan_dx6.md design decision 10: same confirmed minimal link set as DIRECTX1/DIRECTX2/DIRECTX3/DIRECTX5 -- ddraw +
# dxguid + SDL3::SDL3. DIRECTX6 introduces no new interface at all, so no new link dependency either.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
