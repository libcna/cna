# plans/plan_webgpu.md WEBGPU-207: the ONE list of shared cross-renderer behavioural parity fixtures,
# and the ONE registration entry point every renderer calls.
#
# A parity fixture is a renderer-neutral source in this directory (see ParityFixture.hpp for the
# convention). Adding one is: write `parity_<name>.cpp`, append `<name>` to CNA_PARITY_FIXTURES
# below. Every renderer that calls cna_register_parity_fixtures() then builds it and registers its
# CTest automatically -- no per-renderer edit, no second test, no second oracle.
#
# Each fixture executable additionally accepts an optional output path and writes the whole
# backbuffer there as raw RGBA8, in exactly the format cna_diag_compare already reads, so
# scripts/run-parity-fixture.sh can diff one renderer's frame against another's.

set(CNA_PARITY_FIXTURE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The fixture names, without the `parity_` prefix or the `.cpp` suffix.
set(CNA_PARITY_FIXTURES
    # WEBGPU-155: the same mesh through declarations that differ only in element order/offset,
    # plus the semantic cases WEBGPU-156/157/158/159 each add their own leg to.
    vertex_semantics
    # WEBGPU-156: BasicEffect lighting on an untextured Position+Normal declaration.
    lit_untextured
    # WEBGPU-157: the stock ModelProcessor's Position+Normal+Color+TextureCoordinate vertex.
    lit_vertex_color
    # WEBGPU-158: a Position+Colour vertex that declares no Normal renders unlit, keeping its colour.
    unlit_position_color
    # WEBGPU-159: DualTextureEffect consuming TEXCOORD0 and TEXCOORD1 independently.
    dual_texture_uv1
    # WEBGPU-172: one vertex split across two VertexBufferBindings is the same picture as one
    # buffer, and each binding's VertexOffset is converted with its own stride.
    multi_stream_split
    # WEBGPU-164: a mipMap=true RenderTarget2D has a real, regenerated, readable chain.
    render_target_mip
    # WEBGPU-199: an HdrBlendable render target keeps values above 1.0, visible in 8-bit output
    # through a half tint, and both renderers agree on the sampled result.
    hdr_render_target
    # WEBGPU-206: a DXT1 TextureCube stores blocks, reads back decoded, and samples exactly like the
    # RGBA8 cube it encodes.
    compressed_cube
    # WEBGPU-173: each BasicEffect lighting term -- emissive, ambient, the light sum, specular --
    # reaches the surface through its own colour channel.
    basic_effect_light_terms
    # WEBGPU-173: VertexColorEnabled and TextureEnabled are independent gates, and the product
    # clamps rather than wrapping.
    basic_effect_vertex_color
    # WEBGPU-173: EnableDefaultLighting, Alpha's premultiply into RGB, and large-world-scale
    # transform precision.
    basic_effect_alpha_scale
    # WEBGPU-153: FillMode::WireFrame draws edges and leaves the interior empty.
    fill_mode_wireframe
    # WEBGPU-161: SamplerState.MaxMipLevel selects which mip level a sample comes from.
    sampler_max_mip_level
    # WEBGPU-205: SamplerState.MipMapLevelOfDetailBias shifts which mip level a sample comes from.
    sampler_lod_bias
)

# Builds and registers every fixture for one renderer.
#
#   BUILDER        name of the renderer's own "add an example executable" macro, called as
#                  <BUILDER>(<target> <source>) -- e.g. cna_easygl_test / cna_webgpu_test.
#   TARGET_SUFFIX  suffix for the executable name: cna_parity_<fixture>_<suffix>.
#   TEST_PREFIX    prefix for the CTest name: <prefix>_Parity_<fixture>.
#   LABELS         CTest labels (multi-value, forwarded verbatim).
#   ENVIRONMENT    CTest environment entries (multi-value, forwarded verbatim).
#   TIMEOUT        per-fixture CTest timeout in seconds (default 60).
function(cna_register_parity_fixtures)
    cmake_parse_arguments(P "" "BUILDER;TARGET_SUFFIX;TEST_PREFIX;TIMEOUT" "LABELS;ENVIRONMENT" ${ARGN})
    if(NOT P_BUILDER OR NOT P_TARGET_SUFFIX OR NOT P_TEST_PREFIX)
        message(FATAL_ERROR "cna_register_parity_fixtures: BUILDER, TARGET_SUFFIX and TEST_PREFIX are required")
    endif()
    if(NOT P_TIMEOUT)
        set(P_TIMEOUT 60)
    endif()
    foreach(_fixture IN LISTS CNA_PARITY_FIXTURES)
        set(_target "cna_parity_${_fixture}_${P_TARGET_SUFFIX}")
        set(_source "${CNA_PARITY_FIXTURE_DIR}/parity_${_fixture}.cpp")
        if(NOT EXISTS "${_source}")
            message(FATAL_ERROR "cna_register_parity_fixtures: no source for fixture '${_fixture}' at ${_source}")
        endif()
        # cmake_language(CALL) so one shared list can drive each renderer's own link/runtime-copy
        # macro without this file knowing anything about any renderer.
        cmake_language(CALL ${P_BUILDER} ${_target} "${_source}")
        cna_register_renderer_test(NAME "${P_TEST_PREFIX}_Parity_${_fixture}" COMMAND ${_target}
            TIMEOUT ${P_TIMEOUT} LABELS ${P_LABELS} ENVIRONMENT ${P_ENVIRONMENT})
    endforeach()
endfunction()
