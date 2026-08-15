# plan_dx.md DX-100/DX-101: d3d12+dxgi alone was confirmed sufficient for device/queue/
# command-list creation by DX-100's own spike -- start from that minimum, same discipline as
# DX-12/design decision 3. dxguid is deliberately NOT linked (still unneeded). d3dcompiler was
# added by DX-121 (D3D12EffectRenderer's runtime D3DCompile() path) -- confirmed safe to link in
# isolation by D3D11's own DX-14-compile/DX-58 precedent.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE d3d12 dxgi d3dcompiler cna_renderer_d3dcommon)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
