# plans/plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the
# stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's
# runtime D3DCompile() custom-ShaderEffect path (D3D11EffectRenderer) -- confirmed safe to link
# in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE d3d11 dxgi d3dcompiler)
# plans/plan_dx.md DX-251: PUBLIC, exactly as the DirectX12 sibling already declares it, and for the
# same reason. Four of this module's own PUBLIC headers -- DirectX11Renderer.hpp,
# D3D11InputLayoutCache.hpp, D3D11SamplerCache.hpp, D3D11StateObjectCache.hpp and
# D3D11Buffers.hpp -- include CNA/Internal/Renderers/D3DCommon/... , so D3DCommon's include
# directory is a transitive usage requirement of this target, not a private implementation
# detail. With the PRIVATE edge it used to carry, every consumer outside this module (i.e.
# every executable in examples/) failed to compile the moment it included the renderer header.
target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_d3dcommon)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
