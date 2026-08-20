# plans/plan_dx6.md design decision 10: same confirmed minimal link set as DIRECTX1/DIRECTX2/DIRECTX3/DIRECTX5 -- ddraw +
# dxguid. RendererSurfaceInfo supplies the native window; DIRECTX6 adds no link dependency.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE ddraw dxguid)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
