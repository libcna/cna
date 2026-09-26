# SPDX-License-Identifier: MS-PL
"""Build D3D11 scene shader variants from CNA's Vulkan GLSL stages."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import tempfile

from generate_post_process_hlsl import run, translate


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "modules/graphics-ext/src/shaders"
DEFAULT_OUTPUT = SOURCES / "SceneHlsl.generated.hpp"

# Graphics packages with resource models representable by the current DX11
# ShaderEffect path. Clustered lighting and particles need storage-buffer SRVs.
STAGES = (
    "atmospheric_sky/sky.vulkan.vert.glsl",
    "atmospheric_sky/sky.vulkan.frag.glsl",
    "skybox/skybox.vulkan.vert.glsl",
    "skybox/skybox.vulkan.frag.glsl",
    "shadow_caster/directional.vulkan.vert.glsl",
    "shadow_caster/directional.vulkan.frag.glsl",
    "shadow_caster/punctual.vulkan.vert.glsl",
    "shadow_caster/punctual.vulkan.frag.glsl",
    "shadow_caster/skinned.vulkan.vert.glsl",
    "volumetric_fog/fullscreen.vulkan.vert.glsl",
    "volumetric_fog/build.vulkan.frag.glsl",
    "volumetric_fog/resolve.vulkan.frag.glsl",
)

VERTEX_SEMANTICS = {
    "shadow_caster/directional.vulkan.vert.glsl": ("POSITION",),
    "shadow_caster/punctual.vulkan.vert.glsl": ("POSITION",),
    "shadow_caster/skinned.vulkan.vert.glsl":
        ("POSITION", "NORMAL", "TEXCOORD", "BLENDWEIGHT", "BLENDINDICES"),
}


def translate_stage(source: Path, label: str, glslang: Path, spirv_cross: Path,
                    fxc: Path | None, scratch: Path) -> str:
    if label == "skybox/skybox.vulkan.vert.glsl" and \
            "gl_Position = vec4(ndc.x, -ndc.y" not in source.read_text(encoding="utf-8"):
        raise ValueError("skybox vertex Y convention changed; review its D3D11 flip")
    result = translate(
        source, glslang, spirv_cross, None, scratch,
        vertex_semantics=VERTEX_SEMANTICS.get(
            label, ("POSITION", "TEXCOORD", "COLOR")),
        first_fragment_cbuffer_slot=4,
        # Skybox's Vulkan source already negates Y. A second flip reverses
        # triangle winding and the default rasterizer culls the whole quad.
        flip_vertex_y=label != "skybox/skybox.vulkan.vert.glsl")
    if label == "skybox/skybox.vulkan.frag.glsl":
        old_texture = "uEnvironment : register(t5)"
        old_sampler = "_uEnvironment_sampler : register(s5)"
        if result.count(old_texture) != 1 or result.count(old_sampler) != 1:
            raise ValueError("skybox cube binding changed; review its D3D11 mapping")
        result = result.replace(old_texture, "uEnvironment : register(t1)")
        result = result.replace(old_sampler, "_uEnvironment_sampler : register(s1)")
    if fxc is not None:
        hlsl = scratch / (source.name + ".hlsl")
        hlsl.write_text(result, encoding="utf-8")
        run([str(fxc), "/T", "vs_5_0" if label.endswith(".vert.glsl") else "ps_5_0",
             "/E", "main", "/Fo", str(scratch / (source.name + ".cso")),
             str(hlsl)])
    return result


def generate(glslang: Path, spirv_cross: Path, fxc: Path | None,
             output: Path) -> None:
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_scene_hlsl.py.",
        "#pragma once",
        "#include <array>",
        "#include <string_view>",
        "namespace CNA::Graphics::detail::SceneHlslGenerated {",
        "struct Stage { std::string_view label; std::string_view source; "
        "std::string_view sourceSha256; };",
    ]
    entries: list[tuple[str, str, str]] = []
    with tempfile.TemporaryDirectory(prefix="cna-scene-hlsl-") as directory:
        scratch = Path(directory)
        for index, label in enumerate(STAGES):
            source = SOURCES / label
            if not source.is_file():
                raise FileNotFoundError(source)
            result = translate_stage(source, label, glslang, spirv_cross,
                                     fxc, scratch)
            symbol = f"kStage{index}"
            lines.append(
                f'inline constexpr std::string_view {symbol} = '
                f'R"CNA_HLSL({result})CNA_HLSL";')
            entries.append((label.replace(".glsl", ".spv"), symbol,
                            hashlib.sha256(source.read_bytes()).hexdigest()))
    lines.append(f"inline constexpr std::array<Stage, {len(entries)}> kStages{{{{")
    for label, symbol, digest in entries:
        lines.append(f'    Stage{{"{label}", {symbol}, "{digest}"}},')
    lines += [
        "}};",
        "inline std::string_view FindStage(std::string_view label) {",
        "    for (const auto& stage : kStages)",
        "        if (stage.label == label) return stage.source;",
        "    return {};",
        "}",
        "}",
        "",
    ]
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))
    print(f"{len(entries)} HLSL scene stages: {output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslang", type=Path, required=True)
    parser.add_argument("--spirv-cross", type=Path, required=True)
    parser.add_argument("--fxc", type=Path)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    generate(args.glslang, args.spirv_cross, args.fxc, args.output)


if __name__ == "__main__":
    main()
