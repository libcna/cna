# plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the
# stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's
# runtime D3DCompile() custom-ShaderEffect path (D3D11EffectBackend) -- confirmed safe to link
# in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d11 dxgi d3dcompiler cna_backend_graphics_d3dcommon)
