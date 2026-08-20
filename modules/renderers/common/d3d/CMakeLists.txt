# plans/plan_dx.md design decision 4: D3DCommon shared core, consumed by cna_renderer_directx11
# and cna_renderer_directx12. Only built when actually needed -- the renderers
# composition adds this directory for the D3D11/D3D12 configurations only.
# plans/plan_dx9.md design decision 12: D3D9 deliberately does NOT join this condition. D3DFORMAT is a
# different enum space from DXGI_FORMAT and D3D9 has no state objects at all -- D3D9 gets its own
# D3D9FormatMapping/D3D9StateMapping/D3D9VertexDeclarations instead of expanding this shared core.
# D3D10 is likewise independent (its own format/state mapping, no D3DCommon include or link
# edge -- verified mechanically during the physical-module move).
file(GLOB D3DCOMMON_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(cna_renderer_d3dcommon STATIC ${D3DCOMMON_SOURCES})
target_link_libraries(cna_renderer_d3dcommon PUBLIC cna_renderer_common d3d11 dxgi)
cna_link_sharp_runtime(cna_renderer_d3dcommon PUBLIC)
target_include_directories(cna_renderer_d3dcommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Same reverse edges as every renderer target (cna_renderer_common_setup): the shared
# core compiles against the graphics/math/core public headers and calls CNA-owned symbols.
target_link_libraries(cna_renderer_d3dcommon PRIVATE cna_graphics_core cna_core cna_math)
