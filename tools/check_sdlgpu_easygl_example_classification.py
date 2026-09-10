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
}

# These exercise public classic-XNA calls for which the live SDL_GPU baseline has a demonstrated
# gap, a false capability claim, or no implementation. The task is the remediation owner.
MISSING = {
    "easygl_instancedmodel_shader_test.cpp":
        ("SDLGPU-79", "custom ShaderEffect instancing remains a named boundary; core XNA instance streams are covered by SDLGPU-60"),
    "easygl_mrt_test.cpp": ("SDLGPU-75", "independent MRT outputs"),
    "easygl_rendertarget2d_properties_test.cpp": ("SDLGPU-72", "RenderTarget2D surface formats"),
    "easygl_rendertargetcube_properties_test.cpp": ("SDLGPU-73", "RenderTargetCube surface formats"),
    "easygl_occlusion_query_occluded_quad_test.cpp": ("SDLGPU-80", "SDL_gpu has no query API"),
    "easygl_occlusion_query_visible_quad_test.cpp": ("SDLGPU-80", "SDL_gpu has no query API"),
}

# Relevant behavior that appears present, but whose SDL_GPU evidence is weaker than the EasyGL
# corpus. These rows deliberately remain distinct from demonstrated implementation gaps.
NEEDS_TEST = {
    "easygl_alphatest_comparefunction_sweep_test.cpp": ("SDLGPU-77", "all compare functions"),
    "easygl_basiceffect_combinations_test.cpp": ("SDLGPU-77", "effect option combinations"),
    "easygl_basiceffect_default_lighting_test.cpp": ("SDLGPU-77", "default lighting"),
    "easygl_basiceffect_lit_vertex_color_test.cpp": ("SDLGPU-77", "lit vertex color"),
    "easygl_basiceffect_multilight_emissive_test.cpp": ("SDLGPU-77", "multiple lights and emissive"),
    "easygl_basiceffect_one_light_test.cpp": ("SDLGPU-77", "single-light isolation"),
    "easygl_basiceffect_preferperpixellighting_test.cpp": ("SDLGPU-77", "per-pixel lighting"),
    "easygl_basiceffect_specular_test.cpp": ("SDLGPU-77", "specular isolation"),
    "easygl_basiceffect_texture_enabled_test.cpp": ("SDLGPU-77", "TextureEnabled gate"),
    "easygl_basiceffect_texture_vertexcolor_enabled_test.cpp": ("SDLGPU-77", "texture and color product"),
    "easygl_basiceffect_vertex_color_clamp_test.cpp": ("SDLGPU-77", "vertex-color clamp"),
    "easygl_basiceffect_world_scale_precision_test.cpp": ("SDLGPU-77", "large world scale"),
    "easygl_environmentmapeffect_fresnel_gradient_test.cpp": ("SDLGPU-77", "Fresnel gradient"),
    "easygl_environmentmapeffect_multilight_test.cpp": ("SDLGPU-77", "environment-map light count"),
    "easygl_environmentmapeffect_specular_test.cpp": ("SDLGPU-77", "environment-map specular"),
    "easygl_environmentmapeffect_worldtransform_test.cpp": ("SDLGPU-77", "environment-map world transform"),
    "easygl_model_draw_test.cpp": ("SDLGPU-79", "Model.Draw renderer behavior"),
    "easygl_model_hierarchy_child_mesh_test.cpp": ("SDLGPU-79", "model hierarchy transforms"),
    "easygl_model_skinned_animation_playback_test.cpp": ("SDLGPU-79", "skinned model playback"),
    "easygl_model_two_meshes_effects_test.cpp": ("SDLGPU-79", "per-mesh effects"),
    "easygl_render_target_usage_test.cpp": ("SDLGPU-74", "PreserveContents/DiscardContents"),
    "easygl_rendertargetcube_depthformat_test.cpp": ("SDLGPU-73", "cube depth/stencil formats"),
    "easygl_skinnedeffect_multilight_test.cpp": ("SDLGPU-77", "skinned multiple lights"),
    "easygl_skinnedeffect_preferperpixellighting_test.cpp": ("SDLGPU-77", "skinned per-pixel lighting"),
    "easygl_skinnedeffect_specular_test.cpp": ("SDLGPU-77", "skinned specular"),
    "easygl_skinnedeffect_vector4_bone_indices_test.cpp": ("SDLGPU-77", "Vector4 bone indices"),
    "easygl_skinnedeffect_weightspervertex_test.cpp": ("SDLGPU-77", "weights-per-vertex variants"),
    "easygl_spritebatch_layerdepth_test.cpp": ("SDLGPU-76", "sort modes and layer depth"),
    "easygl_spritebatch_rendertarget_size_test.cpp": ("SDLGPU-76", "render-target-local sprite coordinates"),
}

MODERN_TOKENS = (
    "_gltf_", "_pbr", "skinnedpbr", "_shader_effect", "_shadereffect", "_animsprite_",
    "_billboard_", "_bloom_", "_blur_", "_cartooneffect_", "_clouds_", "_distort",
    "_flatshaded_", "_normalmapping_", "_particleeffect_", "_perpixellighting_",
    "_postprocesseffect_", "_shadowmapping_", "_shattereffect_", "_shipgame_",
    "_vertexlighting_",
)

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

# CPU/API-only programs do not discriminate the active renderer. They remain relevant XNA tests,
# but compiling another renderer-specific copy would add no parity evidence.
DIRECT = {
    "easygl_effect_clone_test.cpp",
    "easygl_effect_current_technique_test.cpp",
    "easygl_model_json_reader_32bit_indices_test.cpp",
    "easygl_model_json_reader_bone_hierarchy_test.cpp",
    "easygl_model_json_reader_skeleton_test.cpp",
    "easygl_model_json_reader_test.cpp",
    "easygl_model_json_reader_texture_test.cpp",
}

# These exact EasyGL sources are also compiled and registered under SDL GPU. Unlike the CPU-only
# DIRECT set, they are renderer-discriminating and retain the task that established the evidence.
VERIFIED_DIRECT = {
    "easygl_backbuffer_resize_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_blendstate_separate_factors_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_blendstate_separate_functions_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_compare_function_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_stencil_mask_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_stencil_ops_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_stencil_twosided_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_depthstencilstate_write_enable_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_device_reset_events_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_dxt_format_test.cpp": ("SDLGPU-69", "same source passes SDL GPU with native-or-decoded BC storage"),
    "easygl_graphicsdevice_reference_stencil_test.cpp": ("SDLGPU-58", "same source passes SDL GPU"),
    "easygl_graphicsdevicemanager_vsync_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_msaa_change_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_packed16_format_test.cpp": ("SDLGPU-69", "same source passes SDL GPU with native packed storage"),
    "easygl_presentation_parameters_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_real_window_resize_test.cpp": ("SDLGPU-68", "same source passes SDL GPU"),
    "easygl_scissor_test.cpp": ("SDLGPU-67", "same source passes SDL GPU"),
    "easygl_surface_format_throws_test.cpp": ("SDLGPU-69", "same source passes SDL GPU with truthful format classification"),
    "easygl_viewport_state_test.cpp": ("SDLGPU-67", "same source passes SDL GPU"),
    "easygl_viewport_subregion_test.cpp": ("SDLGPU-67", "same source passes SDL GPU"),
}

VERIFIED_COVERED = {
    "easygl_basiceffect_position_normal_test.cpp":
        ("SDLGPU-59", "semantic POSITION0/Normal stock path passes the shared pixel oracle"),
    "easygl_depth_bias_test.cpp":
        ("SDLGPU-65", "expanded SDL GPU depth-bias pixel/cache oracle passes 67/67"),
    "easygl_draw_user_primitives_custom_test.cpp":
        ("SDLGPU-59", "custom declarations are resolved by semantic/index/format/offset"),
    "easygl_dualtextureeffect_independent_uv_test.cpp":
        ("SDLGPU-59", "shared independent TEXCOORD0/TEXCOORD1 frame is byte-identical"),
    "easygl_sampler_state_effect_test.cpp":
        ("SDLGPU-64", "complete stock sampler snapshots pass shared and SDL pixel oracles"),
    "easygl_texture2d_anisotropic_singlelevel_test.cpp":
        ("SDLGPU-64", "one-level anisotropic sampling is covered by the complete sampler suite"),
    "easygl_texture_address_mode_mirror_effect_test.cpp":
        ("SDLGPU-64", "stock mirror addressing passes the shared sampler suite"),
    "easygl_texture_anisotropic_effect_test.cpp":
        ("SDLGPU-64", "stock anisotropy passes the shared sampler suite"),
    "easygl_texture_mip_filter_effect_test.cpp":
        ("SDLGPU-64", "authored mip filter and level selection pass shared pixel oracles"),
}


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
    if name in EASYGL_SPECIFIC:
        return "easygl-specific", "-", "OpenGL/EasyGL harness or native-resource diagnostic"
    if name in DIRECT:
        return "classic-xna-direct-parity", "-", "renderer-independent public API behavior"
    if any(token in name for token in MODERN_TOKENS):
        return "modern-cnaext-out", "-", "ShaderEffect/PBR/glTF/sample-specific modern graphics path"
    return (
        "classic-xna-covered-by-existing-sdlgpu-test",
        "-",
        "covered by the 85-test SDL_GPU baseline and/or a shared graphics oracle; family re-audited by listed backlog",
    )


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
        rows = list(csv.DictReader(stream))
    names = [row["example"] for row in rows]
    live = live_names()
    errors = []
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
