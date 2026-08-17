# plan_dx2.md design decision 10: same confirmed minimal link set as DIRECTX1 -- ddraw + dxguid +
# RendererSurfaceInfo supplies the native window. No separate Direct3D import library is needed: IDirect3D2/IDirect3DDevice2 are
# obtained purely via QueryInterface/CreateDevice on DirectDraw objects/surfaces (confirmed
# during the DX2-0 spike, see plan_dx2.md section 1).
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE ddraw dxguid)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
