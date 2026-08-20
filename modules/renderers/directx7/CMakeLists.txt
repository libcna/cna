# plans/plan_dx7.md design decision 14: same confirmed minimal link set as DIRECTX1/DIRECTX2/DIRECTX3/DIRECTX5/DIRECTX6 --
# ddraw + dxguid. RendererSurfaceInfo supplies the native window; the APIs resolve from the
# same import libraries, spike-confirmed (DX7-0) with no new link dependency.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE ddraw dxguid)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
