# plan_dx9.md Phase D9-11 (D9-110/D9-111), design decision 16: D3D9EffectBackend.cpp calls
# D3DCompile() (the custom-ShaderEffect runtime compile path), which needs d3dcompiler -- but the
# stock D3D9 pipeline must stay d3dcompiler-free (unlike D3D11/D3D12, which link it into their
# whole backend target unconditionally, DX-58/DX-121). Excluded from the main glob and built as its
# own isolated static library instead, with d3dcompiler linked ONLY there (D9-111's own "on this
# target only"). D3D9ConstantTable.cpp (D9-110's CTAB parser) moves here too -- it has no consumer
# outside D3D9EffectBackend.cpp, and leaving it in the main glob while D3D9EffectBackend.cpp moved
# out created a genuine link-order circular dependency between the two targets (this effect target
# calling ParseConstantTableEXT() while ALSO needing to be linked INTO the main backend target for
# D3D9GraphicsBackend::CreateEffectBackend() to construct one) -- found empirically via a real
# "undefined reference to ParseConstantTableEXT" link failure across every D3D9 test binary, not
# assumed in advance. ${BACKEND_TARGET} links this target (D9-112) so
# D3D9GraphicsBackend::CreateEffectBackend() can construct one.
set(D3D9_EFFECT_BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9EffectBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9ConstantTable.cpp"
)

file(GLOB _cna_d3d9_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9EffectBackend.cpp")
    list(REMOVE_ITEM _cna_d3d9_sources ${D3D9_EFFECT_BACKEND_SOURCES})
    add_library(cna_backend_graphics_d3d9_effect STATIC ${D3D9_EFFECT_BACKEND_SOURCES})
    target_link_libraries(cna_backend_graphics_d3d9_effect PUBLIC cna_backend_graphics_common)
    cna_link_sharp_runtime(cna_backend_graphics_d3d9_effect PUBLIC)
    target_include_directories(cna_backend_graphics_d3d9_effect PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cna_backend_graphics_d3d9_effect PRIVATE d3d9 d3dcompiler)
    # Same reverse edges as every backend target: the effect backend compiles against the
    # graphics/math/core public headers and calls CNA-owned symbols.
    target_link_libraries(cna_backend_graphics_d3d9_effect PRIVATE cna_graphics_core cna_core cna_math)
endif()

add_library(${BACKEND_TARGET} STATIC ${_cna_d3d9_sources})
cna_renderer_backend_common_setup()
# plan_dx9.md design decision 16 / D9-2: d3d9 alone was confirmed sufficient (no dxguid, no
# dxgi -- D3D9 predates DXGI) by D9-2's own spike. No cna_backend_graphics_d3dcommon (design
# decision 12 -- D3D9 does not share D3DCommon). d3dcompiler is deliberately NOT linked here --
# design decision 9/16 keep the stock pipeline dependency-free; it lives on the isolated effect
# target only.
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d9)
# D9-112: D3D9GraphicsBackend::CreateEffectBackend() needs to construct a real D3D9EffectBackend,
# so ${BACKEND_TARGET} links the isolated, d3dcompiler-carrying effect target.
if(TARGET cna_backend_graphics_d3d9_effect)
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_backend_graphics_d3d9_effect)
endif()
