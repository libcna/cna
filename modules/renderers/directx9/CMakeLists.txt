# plan_dx9.md Phase D9-11 (D9-110/D9-111), design decision 16: D3D9EffectRenderer.cpp calls
# D3DCompile() (the custom-ShaderEffect runtime compile path), which needs d3dcompiler -- but the
# stock D3D9 pipeline must stay d3dcompiler-free (unlike D3D11/D3D12, which link it into their
# whole renderer target unconditionally, DX-58/DX-121). Excluded from the main glob and built as its
# own isolated static library instead, with d3dcompiler linked ONLY there (D9-111's own "on this
# target only"). D3D9ConstantTable.cpp (D9-110's CTAB parser) moves here too -- it has no consumer
# outside D3D9EffectRenderer.cpp, and leaving it in the main glob while D3D9EffectRenderer.cpp moved
# out created a genuine link-order circular dependency between the two targets (this effect target
# calling ParseConstantTableEXT() while ALSO needing to be linked INTO the main renderer target for
# DirectX9Renderer::CreateEffectRenderer() to construct one) -- found empirically via a real
# "undefined reference to ParseConstantTableEXT" link failure across every D3D9 test binary, not
# assumed in advance. ${RENDERER_TARGET} links this target (D9-112) so
# DirectX9Renderer::CreateEffectRenderer() can construct one.
set(D3D9_EFFECT_BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9EffectRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9ConstantTable.cpp"
)

file(GLOB _cna_d3d9_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D9EffectRenderer.cpp")
    list(REMOVE_ITEM _cna_d3d9_sources ${D3D9_EFFECT_BACKEND_SOURCES})
    add_library(cna_renderer_d3d9_effect STATIC ${D3D9_EFFECT_BACKEND_SOURCES})
    target_link_libraries(cna_renderer_d3d9_effect PUBLIC cna_renderer_common)
    cna_link_sharp_runtime(cna_renderer_d3d9_effect PUBLIC)
    target_include_directories(cna_renderer_d3d9_effect PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cna_renderer_d3d9_effect PRIVATE d3d9 d3dcompiler)
    # Same reverse edges as every renderer target: the effect renderer compiles against the
    # graphics/math/core public headers and calls CNA-owned symbols.
    target_link_libraries(cna_renderer_d3d9_effect PRIVATE cna_graphics_core cna_core cna_math)
endif()

# plan_runtimerenderer.md RTR-P6-6: derive this family's own target rather than reading the global
# scalar, which names the build DEFAULT.
cna_renderer_target_name(_cna_d3d9_target)
add_library(${_cna_d3d9_target} STATIC ${_cna_d3d9_sources})
cna_renderer_common_setup(${_cna_d3d9_target})
set(RENDERER_TARGET "${_cna_d3d9_target}")
# plan_dx9.md design decision 16 / D9-2: d3d9 alone was confirmed sufficient (no dxguid, no
# dxgi -- D3D9 predates DXGI) by D9-2's own spike. No cna_renderer_d3dcommon (design
# decision 12 -- D3D9 does not share D3DCommon). d3dcompiler is deliberately NOT linked here --
# design decision 9/16 keep the stock pipeline dependency-free; it lives on the isolated effect
# target only.
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 d3d9)
# D9-112: DirectX9Renderer::CreateEffectRenderer() needs to construct a real D3D9EffectRenderer,
# so ${RENDERER_TARGET} links the isolated, d3dcompiler-carrying effect target.
if(TARGET cna_renderer_d3d9_effect)
    target_link_libraries(${RENDERER_TARGET} PRIVATE cna_renderer_d3d9_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
