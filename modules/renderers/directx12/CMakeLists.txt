# plans/plan_dx.md DX-100/DX-101: d3d12+dxgi alone was confirmed sufficient for device/queue/
# command-list creation by DX-100's own spike -- start from that minimum, same discipline as
# DX-12/design decision 3. dxguid is deliberately NOT linked (still unneeded). d3dcompiler was
# added by DX-121 (D3D12EffectRenderer's runtime D3DCompile() path) -- confirmed safe to link in
# isolation by D3D11's own DX-14-compile/DX-58 precedent.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE d3d12 dxgi d3dcompiler)
# DirectX12Renderer.hpp exposes D3D12PipelineStateCache.hpp, whose public
# declarations include the shared D3D shader-cache contract.  Consumers of the
# renderer target therefore need D3DCommon's include directory as a transitive
# usage requirement.
target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_d3dcommon)

if(CNA_DIRECTX12_COMPILED_EFFECTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_DIRECTX12_STOCK_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_DIRECTX12_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_DIRECTX12_STOCK_EFFECT_HEADER
        "${CNA_DIRECTX12_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_DIRECTX12_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_DIRECTX12_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_DIRECTX12_STOCK_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_DIRECTX12_STOCK_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for D3D12 compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_directx12_sprite_effect
        DEPENDS "${CNA_DIRECTX12_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_directx12_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_DIRECTX12_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_DIRECTX12_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
