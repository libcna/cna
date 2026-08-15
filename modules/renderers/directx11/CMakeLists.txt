# plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the
# stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's
# runtime D3DCompile() custom-ShaderEffect path (D3D11EffectRenderer) -- confirmed safe to link
# in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE d3d11 dxgi d3dcompiler cna_renderer_d3dcommon)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
