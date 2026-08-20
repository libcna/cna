# plans/plan_dx1.md design decision 10: ddraw + dxguid (GUID storage for IID_IDirectDraw etc.) is the
# confirmed minimal link set (DX1-0 spike) -- no free-direct, no DXVK, no d3dcompiler, no
# d3d11/dxgi. Native window access comes from RendererSurfaceInfo.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE ddraw dxguid)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
