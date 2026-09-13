#!/usr/bin/env python3
"""Validate (and reproducibly regenerate) the SDL_GPU EasyGL-example audit."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_DIR = ROOT / "modules/renderers/easygl/examples"
MANIFEST = ROOT / "plans/sdlgpu_easygl_example_classification.csv"

CATEGORIES = {
    "classic-xna-direct-parity",
    "classic-xna-covered-by-existing-sdlgpu-test",
    "classic-xna-new-sdlgpu-test-needed",
    "classic-xna-feature-missing",
    "modern-cnaext-out",
    "easygl-specific",
    "duplicate",
    "easygl-defect",
    "unclassified",
}

# These exercise public classic-XNA calls for which the live SDL_GPU baseline has a demonstrated
# gap, a false capability claim, or no implementation. The task is the remediation owner.
MISSING = {
    "easygl_occlusion_query_occluded_quad_test.cpp":
        ("SDLGPU-80", "⛔ vendored SDL_gpu 3.5.0 has no occlusion/query-pool API and exact renderer emulation cannot preserve depth/stencil/MSAA/discard semantics"),
    "easygl_occlusion_query_visible_quad_test.cpp":
        ("SDLGPU-80", "⛔ vendored SDL_gpu 3.5.0 has no occlusion/query-pool API and exact renderer emulation cannot preserve depth/stencil/MSAA/discard semantics"),
}

# Relevant behavior that appears present, but whose SDL_GPU evidence is weaker than the EasyGL
# corpus. These rows deliberately remain distinct from demonstrated implementation gaps.
NEEDS_TEST: dict[str, tuple[str, str]] = {}

MODERN_TOKENS = (
    "_gltf_", "_pbr", "skinnedpbr", "_shader_effect", "_shadereffect", "_animsprite_",
    "_billboard_", "_bloom_", "_blur_", "_cartooneffect_", "_clouds_", "_distort",
    "_flatshaded_", "_normalmapping_", "_particleeffect_", "_perpixellighting_",
    "_postprocesseffect_", "_shadowmapping_", "_shattereffect_", "_shipgame_",
    "_vertexlighting_",
)

MODERN_EXPLICIT = {
    "easygl_instancedmodel_shader_test.cpp":
        "ShaderEffect is explicitly CNAEXT; ordinary compiled Effect instancing is covered by SDLGPU-79",
    "easygl_texture3d_addressw_test.cpp":
        "CNA-specific ShaderEffect sampler3D route; ordinary XNA SamplerState AddressW is covered by SDLGPU-64/79/121/122",
}

EASYGL_SPECIFIC = {
    "easygl_anisotropic_gl_state_test.cpp",
    "easygl_background_content_context_test.cpp",
    "easygl_goldenimage_smoke_test.cpp",
    "easygl_handle_release_test.cpp",
    "easygl_move_semantics_test.cpp",
    "easygl_pixeltestgame_smoke_test.cpp",
    "easygl_resource_leak_test.cpp",
    "easygl_thread_context_lease_exclusion_test.cpp",
    "easygl_unknown_stride_rejection_test.cpp",
}

EASYGL_DEFECT = {
    "easygl_msaa_change_test.cpp":
        ("SDLGPU-85", "asserts EasyGL's inability to apply construction-only MSAA during Reset; SDL GPU implements the XNA/FNA reset contract"),
    "easygl_render_target_usage_test.cpp":
        ("SDLGPU-74", "uses GetBackBufferData while a target is bound and therefore tests EasyGL current-FBO readback rather than XNA backbuffer readback; shared RenderTargetSemantics is the valid usage oracle"),
    "easygl_rt_roundtrip_test.cpp":
        ("SDLGPU-74", "uses GetBackBufferData to read active render targets; XNA defines that method as backbuffer readback, while RenderTarget2D.GetData is the valid target oracle"),
}

# CPU/API-only programs do not discriminate the active renderer. They remain relevant XNA tests,
# but compiling another renderer-specific copy would add no parity evidence.
DIRECT = {
    "easygl_effect_clone_test.cpp",
    "easygl_effect_current_technique_test.cpp",
    "easygl_model_json_reader_bone_hierarchy_test.cpp",
    "easygl_model_json_reader_skeleton_test.cpp",
    "easygl_model_json_reader_test.cpp",
    "easygl_model_json_reader_texture_test.cpp",
}

# These exact EasyGL sources are also compiled and registered under SDL GPU. Unlike the CPU-only
# DIRECT set, they are renderer-discriminating and retain the task that established the evidence.
VERIFIED_DIRECT = {
    "easygl_backbuffer_resize_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_basiceffect_preferperpixellighting_test.cpp":
        ("SDLGPU-77", "identical EasyGL source is registered under SDL GPU and distinguishes per-vertex from per-pixel lighting"),
    "easygl_blendstate_separate_factors_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_blendstate_separate_functions_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_buffer_usage_test.cpp":
        ("SDLGPU-78", "identical source passes under SDL GPU for BufferUsage properties draws and dynamic options"),
    "easygl_depthstencilstate_compare_function_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_stencil_mask_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_stencil_ops_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_stencil_twosided_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_write_enable_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_device_reset_events_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_disposed_buffer_test.cpp":
        ("SDLGPU-78", "identical source passes all static and dynamic buffer disposal guards under SDL GPU"),
    "easygl_draw_noindexbuffer_test.cpp":
        ("SDLGPU-78", "identical source passes indexed and instanced missing-index-buffer guards under SDL GPU"),
    "easygl_draw_novertexbuffer_test.cpp":
        ("SDLGPU-78", "identical source passes every bound-draw missing-vertex-buffer guard under SDL GPU"),
    "easygl_draw_range_validation_test.cpp":
        ("SDLGPU-78", "identical source passes primitive vertex index and instance range guards under SDL GPU"),
    "easygl_draw_user_indexed_primitives_32_test.cpp":
        ("SDLGPU-78", "identical 32-bit indexed-user source passes zero and nonzero offset pixels under SDL GPU"),
    "easygl_draw_user_indexed_primitives_vpc_test.cpp":
        ("SDLGPU-78", "identical 16-bit indexed-user source passes zero and nonzero offset pixels under SDL GPU"),
    "easygl_draw_user_primitives_custom_test.cpp":
        ("SDLGPU-78", "identical explicit-declaration source passes raw typed 16-bit and 32-bit user draws under SDL GPU"),
    "easygl_draw_user_primitives_vpc_test.cpp":
        ("SDLGPU-78", "identical typed user source passes zero and nonzero vertex offsets under SDL GPU"),
    "easygl_dxt_format_test.cpp": ("SDLGPU-69", "same source passes SDL GPU with native-or-decoded BC storage"),
    "easygl_dynamic_buffer_stress_test.cpp":
        ("SDLGPU-78", "identical twelve-frame None Discard NoOverwrite vertex/index replacement source passes under SDL GPU"),
    "easygl_graphicsdevice_reference_stencil_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_graphicsdevicemanager_vsync_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_packed16_format_test.cpp": ("SDLGPU-69", "same source passes SDL GPU with native packed storage"),
    "easygl_presentation_parameters_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_primitivetype_validation_test.cpp":
        ("SDLGPU-78", "identical topology acceptance and invalid-enum source passes under SDL GPU"),
    "easygl_real_window_resize_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_rendertarget2d_properties_test.cpp":
        ("SDLGPU-72", "exact source compiles under SDL GPU and verifies constructors properties and Rgba64 readback"),
    "easygl_rendertargetcube_depthformat_test.cpp":
        ("SDLGPU-73", "exact depth-versus-no-depth pixel source compiles and passes under SDL GPU"),
    "easygl_rendertargetcube_properties_test.cpp":
        ("SDLGPU-73", "exact constructor property mip and MSAA source compiles and passes under SDL GPU"),
    "easygl_scissor_test.cpp": ("SDLGPU-67", "same source passes SDL GPU"),
    "easygl_skinnedeffect_preferperpixellighting_test.cpp":
        ("SDLGPU-77", "identical EasyGL source is registered under SDL GPU and distinguishes per-vertex from per-pixel skinned lighting"),
    "easygl_skinnedeffect_weightspervertex_test.cpp":
        ("SDLGPU-77", "identical EasyGL source is registered under SDL GPU and proves one two and four weight paths"),
    "easygl_surface_format_throws_test.cpp": ("SDLGPU-69", "same source passes SDL GPU with truthful format classification"),
    "easygl_texturecube_content_load_test.cpp": ("SDLGPU-70", "same DDS content source passes SDL GPU"),
    "easygl_texturecube_faces_test.cpp": ("SDLGPU-70", "same six-face source passes SDL GPU"),
    "easygl_texturecube_mip_test.cpp": ("SDLGPU-70", "same authored-mip source passes SDL GPU"),
    "easygl_texturecube_partial_rect_test.cpp": ("SDLGPU-70", "same partial-rectangle source passes SDL GPU"),
    "easygl_texture3d_mip_test.cpp": ("SDLGPU-71", "same authored-volume-mip source passes SDL GPU"),
    "easygl_texture3d_partial_box_readback_test.cpp":
        ("SDLGPU-71", "same asymmetric volume-readback source passes SDL GPU"),
    "easygl_texture3d_partial_box_test.cpp":
        ("SDLGPU-71", "same asymmetric volume-upload source passes SDL GPU"),
    "easygl_texture3d_slices_test.cpp":
        ("SDLGPU-71", "same independent-z-slice source passes SDL GPU"),
    "easygl_vertexbuffer_indexbuffer_getdata_test.cpp":
        ("SDLGPU-78", "identical typed vertex and 16/32-bit index CPU round-trip source passes under SDL GPU"),
    "easygl_vertexbuffer_setdata_test.cpp":
        ("SDLGPU-78", "identical partial raw dynamic and index-width upload source passes under SDL GPU"),
    "easygl_viewport_state_test.cpp": ("SDLGPU-67", "same source passes SDL GPU"),
    "easygl_viewport_subregion_test.cpp": ("SDLGPU-67", "same source passes SDL GPU"),
}

VERIFIED_COVERED = {
    "easygl_alphatest_comparefunction_sweep_test.cpp":
        ("SDLGPU-77", "shared alpha-test sources fixture verifies all compare functions and threshold boundaries"),
    "easygl_basiceffect_combinations_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture verifies texture color diffuse emissive alpha and lighting combinations"),
    "easygl_basiceffect_default_lighting_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture verifies default three-light contribution"),
    "easygl_basiceffect_lit_vertex_color_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture verifies lit vertex-color multiplication"),
    "easygl_basiceffect_multilight_emissive_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture isolates multiple lights and emissive"),
    "easygl_basiceffect_one_light_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture isolates each directional light"),
    "easygl_basiceffect_position_normal_test.cpp":
        ("SDLGPU-59", "semantic POSITION0/Normal stock path passes the shared pixel oracle"),
    "easygl_basiceffect_specular_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture isolates specular power and color"),
    "easygl_basiceffect_texture_enabled_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture verifies the TextureEnabled gate and white fallback"),
    "easygl_basiceffect_texture_vertexcolor_enabled_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture verifies texture and vertex-color product"),
    "easygl_basiceffect_vertex_color_clamp_test.cpp":
        ("SDLGPU-77", "shared BasicEffect terms fixture verifies clamped per-vertex lighting varyings"),
    "easygl_basiceffect_world_scale_precision_test.cpp":
        ("SDLGPU-77", "shared BasicEffect transform fixture verifies nonidentity and nonuniform world transforms"),
    "easygl_depth_bias_test.cpp":
        ("SDLGPU-65", "expanded SDL GPU depth-bias pixel/cache oracle passes 67/67"),
    "easygl_dualtextureeffect_independent_uv_test.cpp":
        ("SDLGPU-59", "shared independent TEXCOORD0/TEXCOORD1 frame is byte-identical"),
    "easygl_environmentmapeffect_fresnel_gradient_test.cpp":
        ("SDLGPU-77", "shared environment-map fixture verifies amount fresnel and gradient behavior"),
    "easygl_environmentmapeffect_multilight_test.cpp":
        ("SDLGPU-77", "shared environment-map terms fixture verifies directional light count"),
    "easygl_environmentmapeffect_specular_test.cpp":
        ("SDLGPU-77", "shared environment-map terms fixture isolates specular contribution"),
    "easygl_environmentmapeffect_worldtransform_test.cpp":
        ("SDLGPU-77", "shared environment-map terms fixture verifies nonidentity world and normal transforms"),
    "easygl_model_draw_test.cpp":
        ("SDLGPU-79", "exact EasyGL Model.Draw source is registered under SDL GPU and passes"),
    "easygl_model_hierarchy_child_mesh_test.cpp":
        ("SDLGPU-79", "exact EasyGL child-bone transform source is registered under SDL GPU and passes"),
    "easygl_model_json_reader_32bit_indices_test.cpp":
        ("SDLGPU-79", "exact EasyGL 32-bit model-index source is registered under SDL GPU and passes"),
    "easygl_model_skinned_animation_playback_test.cpp":
        ("SDLGPU-79", "exact EasyGL skinned playback source is registered under SDL GPU and passes"),
    "easygl_model_two_meshes_effects_test.cpp":
        ("SDLGPU-79", "exact EasyGL per-mesh effect source is registered under SDL GPU and passes"),
    "easygl_mrt_test.cpp":
        ("SDLGPU-75", "ordinary compiled Effect writes independent outputs to mixed-format MRTs with per-slot masks and cache transitions"),
    "easygl_sampler_state_effect_test.cpp":
        ("SDLGPU-64", "complete stock sampler snapshots pass shared and SDL pixel oracles"),
    "easygl_skinnedeffect_multilight_test.cpp":
        ("SDLGPU-77", "shared skinned terms fixture isolates multiple lights and emissive"),
    "easygl_skinnedeffect_specular_test.cpp":
        ("SDLGPU-77", "shared skinned terms fixture isolates specular power and color"),
    "easygl_skinnedeffect_vector4_bone_indices_test.cpp":
        ("SDLGPU-77", "shared skinned declaration fixture verifies Vector4 bone-index conversion by semantic and offset"),
    "easygl_spritebatch_layerdepth_test.cpp":
        ("SDLGPU-76", "shared sprite-state oracle distinguishes all four sort modes and both layer-depth orders"),
    "easygl_spritebatch_rendertarget_size_test.cpp":
        ("SDLGPU-76", "shared sprite-state oracle verifies two target-local placements in opposite corners"),
    "easygl_texture2d_anisotropic_singlelevel_test.cpp":
        ("SDLGPU-64", "one-level anisotropic sampling is covered by the complete sampler suite"),
    "easygl_texture_address_mode_mirror_effect_test.cpp":
        ("SDLGPU-64", "stock mirror addressing passes the shared sampler suite"),
    "easygl_texture_anisotropic_effect_test.cpp":
        ("SDLGPU-64", "stock anisotropy passes the shared sampler suite"),
    "easygl_texture_mip_filter_effect_test.cpp":
        ("SDLGPU-64", "authored mip filter and level selection pass shared pixel oracles"),
}

# Every remaining classic example is named explicitly in one of these evidence groups. This is
# intentionally not a filename-prefix classifier: adding another EasyGL source must produce an
# `unclassified` failure until a reviewer assigns it to evidence, an exclusion, or a gap.
EXPLICIT_COVERAGE_GROUPS = (
    (
        "SDLGPU-77",
        "shared stock-effect term, transform, fog and pixel fixtures cover this EasyGL case",
        {
            "easygl_alphatest_fog_test.cpp",
            "easygl_alphatest_modes_test.cpp",
            "easygl_alphatest_null_texture_test.cpp",
            "easygl_alphatest_vertexcolor_diffuse_test.cpp",
            "easygl_alphatesteffect_golden_test.cpp",
            "easygl_basiceffect_combined_test.cpp",
            "easygl_basiceffect_emissive_test.cpp",
            "easygl_basiceffect_fog_test.cpp",
            "easygl_basiceffect_golden_test.cpp",
            "easygl_basiceffect_vertexcolor_disabled_test.cpp",
            "easygl_basiceffect_vertexcolor_enabled_test.cpp",
            "easygl_dual_texture_test.cpp",
            "easygl_dualtexture_test.cpp",
            "easygl_dualtextureeffect_alpha_test.cpp",
            "easygl_dualtextureeffect_combined_test.cpp",
            "easygl_dualtextureeffect_doubling_test.cpp",
            "easygl_dualtextureeffect_fog_test.cpp",
            "easygl_dualtextureeffect_golden_test.cpp",
            "easygl_dualtextureeffect_null_texture0_test.cpp",
            "easygl_dualtextureeffect_null_texture2_test.cpp",
            "easygl_emissive_ambient_composition_test.cpp",
            "easygl_env_map_test.cpp",
            "easygl_environmentmapeffect_amount_one_test.cpp",
            "easygl_environmentmapeffect_amount_zero_test.cpp",
            "easygl_environmentmapeffect_combined_test.cpp",
            "easygl_environmentmapeffect_eyeposition_test.cpp",
            "easygl_environmentmapeffect_fog_test.cpp",
            "easygl_environmentmapeffect_fresnel_test.cpp",
            "easygl_environmentmapeffect_golden_test.cpp",
            "easygl_sample_dualtexture_swap_test.cpp",
            "easygl_sample_keyboard_cube3d_test.cpp",
            "easygl_sample_moving_quad3d_test.cpp",
            "easygl_skinned_effect_bones_test.cpp",
            "easygl_skinnedeffect_combined_test.cpp",
            "easygl_skinnedeffect_fog_test.cpp",
            "easygl_skinnedeffect_golden_test.cpp",
            "easygl_skinnedeffect_identity_bones_test.cpp",
            "easygl_skinnedeffect_translation_bone_test.cpp",
            "easygl_skinnedeffect_twobone_blend_test.cpp",
            "easygl_skinnedeffect_vertexcolor_test.cpp",
            "easygl_skinnedeffect_world_normal_test.cpp",
            "easygl_transform_matrix_test.cpp",
            "easygl_viewspace_fog_test.cpp",
        },
    ),
    (
        "SDLGPU-58",
        "shared blend/depth/stencil state matrices and A-to-B-to-A cache oracles cover this case",
        {
            "easygl_blendstate_additive_golden_test.cpp",
            "easygl_blendstate_additive_test.cpp",
            "easygl_blendstate_alphablend_test.cpp",
            "easygl_blendstate_blendfactor_test.cpp",
            "easygl_blendstate_nonpremultiplied_test.cpp",
            "easygl_blendstate_opaque_test.cpp",
            "easygl_clear_overloads_test.cpp",
            "easygl_colorwritechannels_test.cpp",
            "easygl_depthstencilstate_stencil_enable_test.cpp",
            "easygl_depthstencilstate_write_enable_golden_test.cpp",
            "easygl_graphicsdevice_clear_stencil_test.cpp",
            "easygl_rasterizerstate_cullmode_golden_test.cpp",
            "easygl_rasterizerstate_cullmode_test.cpp",
        },
    ),
    (
        "SDLGPU-81",
        "shared deferred resource lifetime and disposal guards cover this resource/device case",
        {
            "easygl_bound_resource_dispose_test.cpp",
            "easygl_device_dispose_order_test.cpp",
            "easygl_disposed_resource_test.cpp",
            "easygl_double_dispose_test.cpp",
            "easygl_resource_events_test.cpp",
        },
    ),
    (
        "SDLGPU-68/85",
        "presentation lifecycle, reset and backbuffer-MSAA tests cover the observable XNA behavior",
        {
            "easygl_depth_format_test.cpp",
            "easygl_fullscreen_field_test.cpp",
            "easygl_msaa_test.cpp",
            "easygl_present_interval_test.cpp",
        },
    ),
    (
        "SDLGPU-73/74",
        "render-target format, MSAA, mip, readback, sampling and lifetime contracts cover this case",
        {
            "easygl_gfx164_bound_msaa_alpha_test.cpp",
            "easygl_render_target_test.cpp",
            "easygl_rendertarget2d_mip_test.cpp",
            "easygl_rendertarget2d_msaa_test.cpp",
            "easygl_rendertargetcube_sample_test.cpp",
        },
    ),
    (
        "SDLGPU-69",
        "Texture2D NPOT, partial-transfer and authored-mip contracts cover this storage case",
        {
            "easygl_npot_texture_test.cpp",
            "easygl_texture2d_mip_test.cpp",
            "easygl_texture2d_partial_rect_test.cpp",
        },
    ),
    (
        "SDLGPU-64",
        "shared and SDL-only sampler pixel matrices cover this address/filter case",
        {
            "easygl_texture_address_mode_clamp_effect_test.cpp",
            "easygl_texture_address_mode_mirror_test.cpp",
            "easygl_texture_address_mode_test.cpp",
            "easygl_texture_filter_linear_golden_test.cpp",
            "easygl_texture_filter_point_vs_linear_test.cpp",
            "easygl_textured_quad_test.cpp",
        },
    ),
    (
        "SDLGPU-76",
        "shared SpriteBatch geometry/state/font and ordering fixtures cover this case",
        {
            "easygl_sample_layered_blend_test.cpp",
            "easygl_sprite_effects_test.cpp",
            "easygl_spritebatch_blendstate_leak_test.cpp",
            "easygl_spritebatch_rotation_golden_test.cpp",
            "easygl_spritebatch_rotation_test.cpp",
            "easygl_spritebatch_scale_test.cpp",
            "easygl_spritebatch_sourcerect_test.cpp",
            "easygl_spritefont_default_char_test.cpp",
            "easygl_spritefont_effects_flip_test.cpp",
            "easygl_spritefont_effects_rotation_scale_test.cpp",
            "easygl_spritefont_multiglyph_spacing_test.cpp",
            "easygl_spritefont_newline_test.cpp",
            "easygl_spritefont_single_glyph_test.cpp",
        },
    ),
    (
        "SDLGPU-57/78",
        "public validation and declaration-driven draw suites cover this API contract",
        {
            "easygl_device_validation_test.cpp",
            "easygl_vertex_formats_test.cpp",
        },
    ),
)

for group_task, group_evidence, group_names in EXPLICIT_COVERAGE_GROUPS:
    overlap = set(VERIFIED_COVERED).intersection(group_names)
    if overlap:
        raise RuntimeError(f"duplicate explicit EasyGL classifications: {sorted(overlap)}")
    VERIFIED_COVERED.update(
        {name: (group_task, group_evidence) for name in group_names}
    )


def classify(name: str) -> tuple[str, str, str]:
    if name in VERIFIED_DIRECT:
        task, note = VERIFIED_DIRECT[name]
        return "classic-xna-direct-parity", task, note
    if name in VERIFIED_COVERED:
        task, note = VERIFIED_COVERED[name]
        return "classic-xna-covered-by-existing-sdlgpu-test", task, note
    if name in MISSING:
        task, note = MISSING[name]
        return "classic-xna-feature-missing", task, note
    if name in NEEDS_TEST:
        task, note = NEEDS_TEST[name]
        return "classic-xna-new-sdlgpu-test-needed", task, note
    if name in EASYGL_DEFECT:
        task, note = EASYGL_DEFECT[name]
        return "easygl-defect", task, note
    if name in EASYGL_SPECIFIC:
        return "easygl-specific", "-", "OpenGL/EasyGL harness or native-resource diagnostic"
    if name in DIRECT:
        return "classic-xna-direct-parity", "-", "renderer-independent public API behavior"
    if name in MODERN_EXPLICIT:
        return "modern-cnaext-out", "-", MODERN_EXPLICIT[name]
    if any(token in name for token in MODERN_TOKENS):
        return "modern-cnaext-out", "-", "ShaderEffect/PBR/glTF/sample-specific modern graphics path"
    return "unclassified", "-", "no reviewed classification exists"


def live_names() -> list[str]:
    return sorted(path.name for path in EXAMPLE_DIR.glob("*.cpp"))


def expected_rows() -> list[dict[str, str]]:
    rows = []
    for name in live_names():
        category, task, note = classify(name)
        rows.append({"example": name, "category": category, "task": task, "evidence": note})
    return rows


def write_manifest() -> None:
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    with MANIFEST.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=("example", "category", "task", "evidence"),
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(expected_rows())


def validate() -> None:
    with MANIFEST.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        fieldnames = reader.fieldnames
    names = [row["example"] for row in rows]
    live = live_names()
    errors = []
    expected_fieldnames = ["example", "category", "task", "evidence"]
    if fieldnames != expected_fieldnames:
        errors.append(
            f"manifest header is {fieldnames!r}, expected {expected_fieldnames!r}"
        )
    if names != sorted(names):
        errors.append("manifest rows are not sorted")
    if len(names) != len(set(names)):
        errors.append("manifest contains duplicate examples")
    missing = sorted(set(live) - set(names))
    stale = sorted(set(names) - set(live))
    if missing:
        errors.append(f"unclassified live examples: {missing}")
    if stale:
        errors.append(f"stale manifest examples: {stale}")
    bad = sorted({row["category"] for row in rows} - CATEGORIES)
    if bad:
        errors.append(f"unknown categories: {bad}")
    if any(not row["evidence"].strip() for row in rows):
        errors.append("every row needs evidence")
    unclassified = sorted(row["example"] for row in rows if row["category"] == "unclassified")
    if unclassified:
        errors.append(f"unclassified examples: {unclassified}")
    expected = expected_rows()
    if rows != expected:
        actual_by_name = {row["example"]: row for row in rows}
        expected_by_name = {row["example"]: row for row in expected}
        for name in sorted(set(actual_by_name) & set(expected_by_name)):
            if actual_by_name[name] != expected_by_name[name]:
                errors.append(
                    f"classification drift for {name}: "
                    f"actual={actual_by_name[name]!r}, expected={expected_by_name[name]!r}"
                )
    if errors:
        raise SystemExit("\n".join(errors))
    counts = Counter(row["category"] for row in rows)
    print(f"classified {len(rows)} EasyGL examples")
    for category in sorted(CATEGORIES):
        print(f"{category}: {counts[category]}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="regenerate the checked manifest")
    args = parser.parse_args()
    if args.write:
        write_manifest()
    validate()


if __name__ == "__main__":
    main()
