# SPDX-License-Identifier: MS-PL
"""Build D3D11 particle shader variants from CNA's Vulkan GLSL sources."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import tempfile

from generate_post_process_hlsl import run, translate


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "modules/graphics-ext/src/shaders/particle_system"
DEFAULT_OUTPUT = SOURCES / "ParticleSystemHlsl.generated.hpp"


def compute_hlsl(glslang: Path, spirv_cross: Path, fxc: Path | None,
                 scratch: Path) -> str:
    source = SOURCES / "simulate.vulkan.comp.glsl"
    spirv = scratch / "simulate.spv"
    hlsl = scratch / "simulate.hlsl"
    run([str(glslang), "-V", "-g", "-Od", "-S", "comp", str(source),
         "-o", str(spirv)])
    run([str(spirv_cross), str(spirv), "--hlsl", "--shader-model", "50",
         "--hlsl-auto-binding", "cbv", "--output", str(hlsl)])
    result = hlsl.read_text(encoding="utf-8")
    original = "cbuffer ParticleSimulationParameters\n"
    if result.count(original) != 1:
        raise ValueError("particle compute parameter block changed; review its b1 binding")
    result = result.replace(
        original, "cbuffer ParticleSimulationParameters : register(b1)\n")
    if ")CNA_HLSL" in result:
        raise ValueError("particle compute HLSL contains the raw-string delimiter")
    hlsl.write_text(result, encoding="utf-8")
    if fxc is not None:
        run([str(fxc), "/T", "cs_5_0", "/E", "main", "/Fo",
             str(scratch / "simulate.cso"), str(hlsl)])
    return result


def generate(glslang: Path, spirv_cross: Path, fxc: Path | None,
             output: Path) -> None:
    compute_source = SOURCES / "simulate.vulkan.comp.glsl"
    vertex_source = SOURCES / "draw.vulkan.vert.glsl"
    fragment_source = SOURCES / "draw.vulkan.frag.glsl"
    with tempfile.TemporaryDirectory(prefix="cna-particle-hlsl-") as directory:
        scratch = Path(directory)
        stages = [
            ("kSimulateComputeSource", compute_hlsl(
                glslang, spirv_cross, fxc, scratch), compute_source),
            ("kDrawVertexSource", translate(
                vertex_source, glslang, spirv_cross, fxc, scratch,
                vertex_semantics=("POSITION",)), vertex_source),
            ("kDrawFragmentSource", translate(
                fragment_source, glslang, spirv_cross, fxc, scratch,
                first_fragment_cbuffer_slot=4),
             fragment_source),
        ]
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_particle_hlsl.py.",
        "#pragma once",
        "#include <string_view>",
        "namespace CNA::Graphics::detail::ParticleSystemHlslGenerated {",
    ]
    for symbol, source, original in stages:
        lines.append(
            f'inline constexpr std::string_view {symbol} = '
            f'R"CNA_HLSL({source})CNA_HLSL";')
        lines.append(
            f'inline constexpr std::string_view {symbol}Sha256 = '
            f'"{hashlib.sha256(original.read_bytes()).hexdigest()}";')
    lines += ["}", ""]
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))
    print(f"3 HLSL particle stages: {output}")


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
