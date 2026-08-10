# plan_dx6.md design decision 10: same confirmed minimal link set as DX1/DX2/DX3/DX5 -- ddraw +
# dxguid + SDL3::SDL3. DX6 introduces no new interface at all, so no new link dependency either.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
