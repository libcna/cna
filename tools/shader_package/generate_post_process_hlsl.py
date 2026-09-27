# SPDX-License-Identifier: MS-PL
"""Build the Direct3D shader-package variants from CNA's Vulkan GLSL sources."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "modules/graphics-ext/src/shaders/post_process"
DEFAULT_OUTPUT = SOURCES / "PostProcessHlsl.generated.hpp"


def run(command: list[str], timeout: int = 90) -> None:
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(
            f"{' '.join(command)} failed:\n{result.stdout}{result.stderr}")


def explicit_ssr_lod(source: str) -> str:
    """Avoid FXC unrolling implicit derivatives in the ray loop; use mip zero."""
    result: list[str] = []
    position = 0
    while (start := source.find(".Sample(", position)) >= 0:
        result.append(source[position:start])
        opening = start + len(".Sample")
        depth = 0
        for end in range(opening, len(source)):
            if source[end] == "(":
                depth += 1
            elif source[end] == ")":
                depth -= 1
                if depth == 0:
                    break
        else:
            raise ValueError("unbalanced HLSL Sample arguments")
        result.append(
            ".SampleLevel(" + source[opening + 1:end] + ", 0.0f)")
        position = end + 1
    result.append(source[position:])
    return "".join(result)


def translate(
    source: Path, glslang: Path, spirv_cross: Path, fxc: Path | None,
    scratch: Path, vertex_semantics: tuple[str, ...] = ("POSITION", "TEXCOORD", "COLOR"),
    first_fragment_cbuffer_slot: int = 1,
    flip_vertex_y: bool = True,
) -> str:
    vertex = source.name.endswith(".vert.glsl")
    stage = "vert" if vertex else "frag"
    spirv = scratch / (source.name + ".spv")
    hlsl = scratch / (source.name + ".hlsl")
    run([str(glslang), "-V", "-g", "-Od", "-S", stage,
         str(source), "-o", str(spirv)])
    cross_command = [
        str(spirv_cross), str(spirv), "--hlsl", "--shader-model", "50",
        "--hlsl-auto-binding", "cbv",
    ]
    if vertex:
        if flip_vertex_y:
            cross_command.append("--flip-vert-y")
        for location, semantic in enumerate(vertex_semantics):
            cross_command += [
                "--set-hlsl-vertex-input-semantic", str(location), semantic]
    cross_command += ["--output", str(hlsl)]
    run(cross_command)
    text = hlsl.read_text(encoding="utf-8")
    text = re.sub(r"\bpc_([A-Za-z0-9_]+)", r"\1", text)
    # SPIRV-Cross prefixes names from standalone GLSL uniform blocks with
    # the SPIR-V object ID. Restore the package API names used by SetUniform*.
    text = re.sub(r"\b_[0-9]+_(u[A-Za-z0-9_]+)", r"\1", text)
    slot = 0 if vertex else first_fragment_cbuffer_slot

    def assign_slot(match: re.Match[str]) -> str:
        nonlocal slot
        assigned = slot
        slot += 1
        if assigned >= 14:
            raise ValueError(f"{source.name} exceeds D3D11's 14 cbuffer slots")
        return f"cbuffer {match.group(1)} : register(b{assigned})"

    text = re.sub(
        r"(?m)^cbuffer ([A-Za-z0-9_]+)(?: : register\(b[0-9]+\))?",
        assign_slot, text)
    if source.name == "color_grade_volume.vulkan.frag.glsl":
        # The pass uses ShaderEffect texture unit 1 for every LUT layout. The
        # Vulkan descriptor's binding 9 is separate from that runtime unit.
        if (text.count("uLutVolume : register(t9)") != 1 or
                text.count("_uLutVolume_sampler : register(s9)") != 1):
            raise ValueError("volume-LUT shader binding changed; review its HLSL mapping")
        text = text.replace("uLutVolume : register(t9)",
                            "uLutVolume : register(t1)")
        text = text.replace("_uLutVolume_sampler : register(s9)",
                            "_uLutVolume_sampler : register(s1)")
    if source.name == "ssr.vulkan.frag.glsl":
        text = explicit_ssr_lod(text)
    if ")CNA_HLSL" in text:
        raise ValueError(f"{source.name} contains the raw-string delimiter")
    hlsl.write_text(text, encoding="utf-8")
    if fxc is not None:
        run([str(fxc), "/T", "vs_5_0" if vertex else "ps_5_0",
             "/E", "main", "/Fo", str(scratch / (source.name + ".cso")),
             str(hlsl)])
    return text


def symbol_for(source: Path) -> str:
    stem = source.name.split(".vulkan.")[0]
    suffix = "VertexHlsl" if source.name.endswith(".vert.glsl") else "FragmentHlsl"
    return "k" + "".join(word.capitalize() for word in stem.split("_")) + suffix


def generate(
    glslang: Path, spirv_cross: Path, fxc: Path | None, output: Path,
) -> None:
    sources = sorted(SOURCES.glob("*.vulkan.frag.glsl"))
    vertex_source = SOURCES / "fullscreen.vulkan.vert.glsl"
    if not vertex_source.exists() or not sources:
        raise FileNotFoundError("the post-process Vulkan shader sources are missing")
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_post_process_hlsl.py.",
        "#pragma once",
        "#include <array>",
        "#include <string_view>",
        "namespace CNA::Graphics::detail::PostProcessHlslGenerated {",
        "struct Fragment { std::string_view label; std::string_view source; "
        "std::string_view sourceSha256; };",
    ]
    fragments: list[tuple[str, str, str]] = []
    with tempfile.TemporaryDirectory(prefix="cna-post-hlsl-") as directory:
        scratch = Path(directory)
        for source in [vertex_source, *sources]:
            text = translate(source, glslang, spirv_cross, fxc, scratch)
            symbol = symbol_for(source)
            lines.append(
                f'inline constexpr std::string_view {symbol} = '
                f'R"CNA_HLSL({text})CNA_HLSL";')
            if source != vertex_source:
                label = "post_process/" + source.name.replace(".glsl", ".spv")
                digest = hashlib.sha256(source.read_bytes()).hexdigest()
                fragments.append((label, symbol, digest))
    lines.append(f"inline constexpr std::array<Fragment, {len(fragments)}> kFragments{{{{")
    for label, symbol, digest in fragments:
        lines.append(f'    Fragment{{"{label}", {symbol}, "{digest}"}},')
    lines += [
        "}};",
        "inline std::string_view FindFragment(std::string_view label) {",
        "    for (const auto& fragment : kFragments)",
        "        if (fragment.label == label) return fragment.source;",
        "    return {};",
        "}",
        "}",
        "",
    ]
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))
    print(f"{len(fragments)} HLSL fragment stages: {output}")


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
