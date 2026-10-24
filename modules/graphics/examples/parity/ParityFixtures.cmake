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
    # SDLGPU-85: construction-time PresentationParameters.MultiSampleCount produces a real
    # multisample backbuffer resolve, proved by partially covered pixels on an opaque diagonal.
    backbuffer_msaa
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
    # WEBGPU-174: all eight AlphaTestEffect CompareFunctions, each given a unique draw/discard
    # signature across three texel alphas (below, at, above the reference).
    alpha_test_sweep
    # WEBGPU-174: the two AlphaTestEffect inputs the sweep cannot reach -- a null Texture, and
    # VertexColorEnabled combined with DiffuseColor.
    alpha_test_sources
    # WEBGPU-175: DualTextureEffect's `color.rgb *= 2` doubling, the overlay, DiffuseColor, Alpha
    # and the two null-texture fallbacks.
    dual_texture_terms
    # WEBGPU-176: EnvironmentMapEffect's EnvironmentMapSpecular, light count, EyePosition, world
    # transform and the Fresnel gradient, all on a uniform cube so the face convention drops out.
    env_map_terms
    # WEBGPU-177: SkinnedEffect's bone palette (identity, translation, a 50/50 blend, the last
    # slot), light count, specular and VertexColorEnabled.
    skinned_terms
    # WEBGPU-178: SpriteBatch's geometric family -- source rectangles, rotation origins,
    # non-uniform scale, a sub-pixel destination and SpriteEffects flips.
    sprite_geometry
    # WEBGPU-178: SpriteBatch's state family -- sort modes and layerDepth, the Begin transform,
    # blend-state leakage between batches, and render-target-local sprite coordinates.
    sprite_state
    # WEBGPU-179: a SpriteFont string, a newline and a default-character substitution reach pixels.
    sprite_font
    # WEBGPU-186: sampler filters, mip filters, anisotropy and which sampler slot a stock draw reads.
    sampler_filters
    # WEBGPU-190 (blend family): every blend factor, function, separate channel, BlendFactor and
    # write mask, each against a computed equation rather than a golden image.
    blend_states
    # WEBGPU-190 (depth family): all eight depth CompareFunctions as an 8x3 signature matrix.
    depth_states
    # WEBGPU-190 (stencil family): every StencilOperation, verified by gating a second quad on the
    # value the operation should have written.
    stencil_states
    # SDLGPU-58: all eight stencil CompareFunctions, plus an Always -> Never -> Always sequence
    # that detects incomplete immutable-pipeline cache identity and bad restoration.
    stencil_compare
    # WEBGPU-190 (rasterizer/viewport family): cull modes with BOTH windings, the scissor, a
    # viewport sub-region, a render-target round trip, cull-state leakage and depth bias.
    rasterizer_viewport
    # WEBGPU-153: FillMode::WireFrame draws edges and leaves the interior empty.
    fill_mode_wireframe
    # WEBGPU-161: SamplerState.MaxMipLevel selects which mip level a sample comes from.
    sampler_max_mip_level
    # WEBGPU-205: SamplerState.MipMapLevelOfDetailBias shifts which mip level a sample comes from.
    sampler_lod_bias
    # WEBGPU-205: whether a SamplerState field beyond filter/address reaches a SpriteBatch draw
    # at all -- the sprite route measured against the 3D one on the same texture and scale.
    sprite_sampler_state
    # WEBGPU-193: the corpus milestone's one missing entry -- an instanced draw whose instances
    # disagree about which of them is being placed and which shaded.
    instanced_draw
)

# Renderer-neutral conformance programs, kept as complete public-API programs (PixelTestGame and
# their own main) instead of ParityFixture subclasses. They hold XNA behaviour first measured on
# renderers CNA has since retired (docs/removed-renderers.md), moved here so the knowledge keeps
# running on the surviving ones. Each <name> is conformance/<name>_test.cpp and registers as
# <TEST_PREFIX>_Conformance_<name>, exactly like a parity fixture. A program that needs a
# capability the renderer lacks exits 77 (CTest SKIP) and says which.
set(CNA_CONFORMANCE_FIXTURES
    # from sdlrenderer_disposed_guards_test.cpp (Task 717): disposed resources and states are
    # refused with ObjectDisposedException, never dereferenced; and sdlrenderer_double_dispose_test.cpp
    # (Task 718): a second Dispose() is a no-op and the refusals still hold.
    disposed_guards
    # from sdlrenderer_texture2d_fromstream_test.cpp (Task 682): a FromStream-decoded PNG draws
    # its exact pixels through the GPU texture.
    texture2d_fromstream
    # from sdlrenderer_texture2d_saveas_roundtrip_test.cpp (Task 683): SetData -> SaveAsPng/Jpeg ->
    # FromStream -> draw reaches the framebuffer unchanged.
    texture2d_saveas_roundtrip
    # from vulkan_pbreffect_handderived_test.cpp (GLTF-176): PbrEffect's roughness-0.5 analytic
    # BRDF at N=V=L, and SkinnedPbrEffect's TBN handedness under a mirrored joint.
    pbr_effect_hand_derived
    # from vulkan_basiceffect_textured_msaa_test.cpp (Task 904): textured BasicEffect on an MSAA backbuffer.
    basiceffect_textured_msaa
    # from vulkan_declared_effect_layout_test.cpp (VULKAN-147/148/151): stock effects read UVs from the declaration.
    declared_effect_layout
    # from vulkan_record_stride_binding_test.cpp (VULKAN-158): EnvironmentMapEffect and
    # PbrEffect fetch padded-stride records at the declaration's stride, two strides with one
    # set of offsets included.
    stock_effect_padded_stride
    # from vulkan_rendertarget_own_msaa_test.cpp (VULKAN-216): 4x/0x/4x render targets
    # interleaved in one frame; an edge is anti-aliased exactly when the applied count is > 1.
    render_target_own_msaa
    # from vulkan_shader_effect_test.cpp (MOD-2217, VULKAN-252): a package with no accepted
    # variant throws ShaderCompilationExceptionEXT with one labelled diagnostic per stage, and
    # an over-capacity uniform array is refused by name.
    shader_effect_refusal
    # from vulkan_shader_effect_3d_test.cpp (VULKAN-255, VULKAN-168): a ShaderEffect's 3D
    # inputs follow its declarations across one or two streams on both draw routes and per
    # instance; a missing declaration is refused by name, a missing input by location, a
    # negative first instance rejected.
    shader_effect_3d_layout
    # from vulkan_deferred_resource_lifetime_test.cpp: a custom effect or query disposed before the flush keeps its draw.
    deferred_effect_query_lifetime
    # from vulkan_depth_range_contract_test.cpp (VULKAN-098): clip depth is [0,1], near and far planes clip.
    clip_depth_range
    # from vulkan_graphicsdevice_clearoptions_test.cpp: all eight ClearOptions masks on every target type.
    clear_options_matrix
    # from vulkan_msaa_runtime_change_test.cpp (VULKAN-095): resources survive live MSAA toggles; edges follow the applied count.
    msaa_runtime_change
    # from vulkan_occlusionquery_precision_test.cpp (VULKAN-370): a precise PixelCount is the exact half-occluded tally.
    occlusion_query_precision
    # from vulkan_occlusionquery_reuse_generation_test.cpp (MSR-026): one query reused for 32 frames reports each generation.
    occlusion_query_reuse
    # from vulkan_orientation_{calibration,effects,rendertarget}_test.cpp: +X/+Y lands top-right, backbuffer and RT.
    effect_orientation
    # from vulkan_cube_mrt_binding_test.cpp: cube faces as MRT slots get their own outputs; XNA's refusals.
    cube_mrt_binding
    # from metal_pbr_vertex_colour_test.cpp (AM4-084): glTF COLOR_0 multiplies PBR base colour (rgb
    # and alpha) only when VertexColorEnabledEXT is on; a renderer without it refuses by name (skip).
    pbr_vertex_colour
    # from metal_untextured_declaration_test.cpp (AM4-080): an unlit BasicEffect draws over a
    # Position-only declaration; a textured one without TEXCOORD0 throws InvalidOperationException.
    basic_effect_declaration_inputs
    # from sdlrenderer_texture2d_partial_rect/startindex_test.cpp (Tasks 679/680): partial SetData
    # (rectangle, startIndex/elementCount, after first use) reaches the sampled GPU texture.
    texture2d_partial_setdata
    # handoff.md 5.1: a Texture2D and a TextureCube changed between two draws of one frame keep
    # their old contents in the first draw, on renderers that replay the frame after the upload.
    texture_setdata_after_draw
    # from sdlrenderer_presentationparameters_resize_test.cpp (Task 711): Reset to a larger back
    # buffer grows the real surface -- a clear and a sprite beyond the old bounds read back.
    backbuffer_reset_resize
    # from sdlrenderer_spritebatch_overloads_test.cpp (Task 666): each of XNA's seven
    # SpriteBatch.Draw overloads draws, honouring its own source/tint/scale/flip/rotation.
    spritebatch_draw_overloads
    # from graphics_adapter_query_contract_test.cpp (DX-245): GraphicsAdapter's format queries
    # answer before a device exists, and RenderTarget2D and Reset apply exactly that answer.
    graphics_adapter_query
    # from vulkan_skinnedeffect_vertexcolor_test.cpp (CNB-67, VULKAN-205, VULKAN-200):
    # SkinnedEffect.VertexColorEnabled's analytic on/off values; the vertex colour scales the
    # diffuse, not the specular, and the saturate follows the fold.
    skinned_effect_vertex_color
    # from vulkan_uniform_ring_growth_test.cpp (STREET-0004): 1500 PbrEffect and 100
    # SkinnedPbrEffect draws in one frame each keep their own parameters.
    stock_effect_uniform_ring
    # from vulkan_unsupplied_input_contract_test.cpp (VULKAN-156): stock effects fed a stream
    # missing their shader inputs render or refuse at the draw, never draw nothing and report
    # success.
    stock_effect_unsupplied_input
    # from renderer_capability_truth_test.cpp (DX-211): a capability answer agrees with what the
    # device does -- MSAA with the samples a four-sample Reset applies, the derived entries with
    # their sources.
    capability_truth
    # registered only by the retired VULKAN suite (VULKAN-200/201, F-37): the 36-byte
    # Position+Normal+Colour+TexCoord record XNA's ModelProcessor emits, lit per pixel (the vertex
    # colour multiplies the whole lit bracket in the fragment stage) and unlit (a colour gradient
    # with DiffuseColor 2 saturates per vertex, as Direct3D 9's oD0 does).
    basiceffect_lit_vertex_color_perpixel
    basiceffect_unlit_vertex_color_36byte
    # from vulkan_applied_multisample_test.cpp (VULKAN-347), RRC-047: a multisample request is
    # clamped by FNA's rule (rounded down to a power of two, 1 to 0) and PresentationParameters and
    # RenderTarget2D report the count applied, alike, for requests 0-16 including 3, 5, 7 and 9.
    multisample_count_clamp
)

# Builds and registers every fixture for one renderer.
#
#   BUILDER        name of the renderer's own "add an example executable" macro, called as
#                  <BUILDER>(<target> <source>) -- e.g. cna_easygl_test / cna_webgpu_test.
#   TARGET_SUFFIX  suffix for the executable name: cna_parity_<fixture>_<suffix>.
#   TARGET_PREFIX  optional complete target prefix. When set, executable names are
#                  <prefix><fixture><suffix>; this lets an established renderer adopt the central
#                  registry without renaming and rebuilding every existing parity executable.
#   TEST_PREFIX    prefix for the CTest name: <prefix>_Parity_<fixture>.
#   LABELS         CTest labels (multi-value, forwarded verbatim).
#   ENVIRONMENT    CTest environment entries (multi-value, forwarded verbatim).
#   TIMEOUT        per-fixture CTest timeout in seconds (default 60).
#   FAIL_REGULAR_EXPRESSION optional validation/error patterns made fatal for every fixture.
function(cna_register_parity_fixtures)
    cmake_parse_arguments(P ""
        "BUILDER;TARGET_SUFFIX;TARGET_PREFIX;TEST_PREFIX;TIMEOUT"
        "LABELS;ENVIRONMENT;FAIL_REGULAR_EXPRESSION" ${ARGN})
    if(P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "cna_register_parity_fixtures: unparsed arguments: ${P_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT P_BUILDER OR NOT P_TEST_PREFIX OR (NOT P_TARGET_PREFIX AND NOT P_TARGET_SUFFIX))
        message(FATAL_ERROR
            "cna_register_parity_fixtures: BUILDER, TEST_PREFIX and TARGET_SUFFIX or TARGET_PREFIX are required")
    endif()
    if(NOT P_TIMEOUT)
        set(P_TIMEOUT 60)
    endif()
    foreach(_fixture IN LISTS CNA_PARITY_FIXTURES)
        if(P_TARGET_PREFIX)
            set(_target "${P_TARGET_PREFIX}${_fixture}${P_TARGET_SUFFIX}")
        else()
            set(_target "cna_parity_${_fixture}_${P_TARGET_SUFFIX}")
        endif()
        set(_source "${CNA_PARITY_FIXTURE_DIR}/parity_${_fixture}.cpp")
        if(NOT EXISTS "${_source}")
            message(FATAL_ERROR "cna_register_parity_fixtures: no source for fixture '${_fixture}' at ${_source}")
        endif()
        # cmake_language(CALL) so one shared list can drive each renderer's own link/runtime-copy
        # macro without this file knowing anything about any renderer.
        cmake_language(CALL ${P_BUILDER} ${_target} "${_source}")
        cna_register_renderer_test(NAME "${P_TEST_PREFIX}_Parity_${_fixture}" COMMAND ${_target}
            TIMEOUT ${P_TIMEOUT} LABELS ${P_LABELS} ENVIRONMENT ${P_ENVIRONMENT})
        if(P_FAIL_REGULAR_EXPRESSION)
            set_tests_properties("${P_TEST_PREFIX}_Parity_${_fixture}" PROPERTIES
                FAIL_REGULAR_EXPRESSION "${P_FAIL_REGULAR_EXPRESSION}")
        endif()
    endforeach()
    cna_register_conformance_programs(
        BUILDER ${P_BUILDER} TEST_PREFIX ${P_TEST_PREFIX} TIMEOUT ${P_TIMEOUT}
        LABELS ${P_LABELS} ENVIRONMENT ${P_ENVIRONMENT}
        FAIL_REGULAR_EXPRESSION ${P_FAIL_REGULAR_EXPRESSION})
endfunction()

# Builds and registers every CNA_CONFORMANCE_FIXTURES program for one renderer, as
# <TEST_PREFIX>_Conformance_<name> with the Conformance label. cna_register_parity_fixtures() calls
# it; a family without the parity corpus (SOFTWARE, HEADLESS, FNA3D) calls it directly, so a
# renderer-neutral contract is never absent from a family merely because nobody registered it
# (RRC-045). A program decides at run time whether this renderer can execute it and exits 77 (CTest
# SKIP) with the capability it lacks; registration does not pre-filter by family.
#
#   BUILDER, TEST_PREFIX, LABELS, ENVIRONMENT, TIMEOUT and FAIL_REGULAR_EXPRESSION as above.
function(cna_register_conformance_programs)
    cmake_parse_arguments(P ""
        "BUILDER;TEST_PREFIX;TIMEOUT"
        "LABELS;ENVIRONMENT;FAIL_REGULAR_EXPRESSION" ${ARGN})
    if(P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "cna_register_conformance_programs: unparsed arguments: ${P_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT P_BUILDER OR NOT P_TEST_PREFIX)
        message(FATAL_ERROR
            "cna_register_conformance_programs: BUILDER and TEST_PREFIX are required")
    endif()
    if(NOT P_TIMEOUT)
        set(P_TIMEOUT 60)
    endif()
    string(TOLOWER "${P_TEST_PREFIX}" _cna_conformance_suffix)
    foreach(_fixture IN LISTS CNA_CONFORMANCE_FIXTURES)
        set(_target "cna_conformance_${_fixture}_${_cna_conformance_suffix}")
        set(_source "${CNA_PARITY_FIXTURE_DIR}/../conformance/${_fixture}_test.cpp")
        if(NOT EXISTS "${_source}")
            message(FATAL_ERROR "cna_register_conformance_programs: no source for conformance program '${_fixture}' at ${_source}")
        endif()
        cmake_language(CALL ${P_BUILDER} ${_target} "${_source}")
        # The shared example headers (common/PixelTestGame.hpp, ...) and the shared header-only
        # test fixtures (CNA/TestSupport/...), e.g. the synthetic compiled-effect builders a
        # multi-output pixel shader comes from.
        target_include_directories(${_target} PRIVATE
            "${CNA_PARITY_FIXTURE_DIR}/.." "${CMAKE_SOURCE_DIR}/tests/support")
        cna_register_renderer_test(NAME "${P_TEST_PREFIX}_Conformance_${_fixture}" COMMAND ${_target}
            TIMEOUT ${P_TIMEOUT} LABELS ${P_LABELS} Conformance ENVIRONMENT ${P_ENVIRONMENT})
        if(P_FAIL_REGULAR_EXPRESSION)
            set_tests_properties("${P_TEST_PREFIX}_Conformance_${_fixture}" PROPERTIES
                FAIL_REGULAR_EXPRESSION "${P_FAIL_REGULAR_EXPRESSION}")
        endif()
    endforeach()
endfunction()
