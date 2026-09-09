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

# plans/plan_fx.md FX-063: the optional runtime uses MojoShader's D3D11 adapter and the same
# renderer-neutral reflection/state translation as the other compiled-effect backends.
if(CNA_DIRECTX11_COMPILED_EFFECTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_DIRECTX11_STOCK_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_DIRECTX11_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_DIRECTX11_STOCK_EFFECT_HEADER
        "${CNA_DIRECTX11_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_DIRECTX11_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_DIRECTX11_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_DIRECTX11_STOCK_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_DIRECTX11_STOCK_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for D3D11 compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_directx11_sprite_effect
        DEPENDS "${CNA_DIRECTX11_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_directx11_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_DIRECTX11_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_DIRECTX11_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
