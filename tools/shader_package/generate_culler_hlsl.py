# SPDX-License-Identifier: MS-PL
"""Build D3D11 GPU-culler shader variants from CNA's Vulkan GLSL sources."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import tempfile

from generate_post_process_hlsl import run, translate


ROOT = Path(__file__).resolve().parents[2]
COMPUTE_SOURCE = (ROOT / "modules/graphics-ext/src/shaders/gpu_instance_culler"
                  / "cull.vulkan.comp.glsl")
DRAW_DIRECTORY = (ROOT / "modules/graphics-ext/tests/CNA/Graphics/shaders"
                  / "gpu_instance_culler")
COMPUTE_OUTPUT = COMPUTE_SOURCE.parent / "GpuInstanceCullerHlsl.generated.hpp"
DRAW_OUTPUT = DRAW_DIRECTORY.parent / "GpuInstanceCullerDrawHlsl.generated.hpp"


def compute_hlsl(glslang: Path, spirv_cross: Path, fxc: Path | None,
                 scratch: Path) -> str:
    spirv = scratch / "cull.spv"
    hlsl = scratch / "cull.hlsl"
    run([str(glslang), "-V", "-g", "-Od", "-S", "comp", str(COMPUTE_SOURCE),
         "-o", str(spirv)])
    run([str(spirv_cross), str(spirv), "--hlsl", "--shader-model", "50",
         "--hlsl-auto-binding", "cbv", "--output", str(hlsl)])
    source = hlsl.read_text(encoding="utf-8")

    # The compute contract binds all four storage buffers as UAVs. These two
    # inputs remain read-only in shader logic; D3D11 still needs their u slots.
    for slot in (0, 2):
        pattern = rf"(?m)^ByteAddressBuffer (\w+) : register\(t{slot}\);$"
        source, count = re.subn(
            pattern, rf"RWByteAddressBuffer \1 : register(u{slot});", source)
        if count != 1:
            raise ValueError(f"culler input t{slot} changed; review its UAV mapping")
    if ")CNA_HLSL" in source:
        raise ValueError("culler HLSL contains the raw-string delimiter")
    hlsl.write_text(source, encoding="utf-8")
    if fxc is not None:
        run([str(fxc), "/T", "cs_5_0", "/E", "main", "/Fo",
             str(scratch / "cull.cso"), str(hlsl)])
    return source


def write_header(path: Path, namespace: str,
                 stages: list[tuple[str, str, Path]]) -> None:
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_culler_hlsl.py.",
        "#pragma once",
        "#include <string_view>",
        f"namespace {namespace} {{",
    ]
    for symbol, source, original in stages:
        lines.append(
            f'inline constexpr std::string_view {symbol} = '
            f'R"CNA_HLSL({source})CNA_HLSL";')
        lines.append(
            f'inline constexpr std::string_view {symbol}Sha256 = '
            f'"{hashlib.sha256(original.read_bytes()).hexdigest()}";')
    lines += ["}", ""]
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))


def generate(glslang: Path, spirv_cross: Path, fxc: Path | None,
             compute_output: Path, draw_output: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cna-culler-hlsl-") as directory:
        scratch = Path(directory)
        compute = compute_hlsl(glslang, spirv_cross, fxc, scratch)
        vertex_source = DRAW_DIRECTORY / "draw.vulkan.vert.glsl"
        fragment_source = DRAW_DIRECTORY / "draw.vulkan.frag.glsl"
        vertex = translate(vertex_source, glslang, spirv_cross, fxc, scratch,
                           vertex_semantics=("POSITION",))
        fragment = translate(fragment_source, glslang, spirv_cross, fxc,
                             scratch)
    write_header(compute_output,
                 "CNA::Graphics::detail::GpuInstanceCullerHlslGenerated",
                 [("kCullComputeSource", compute, COMPUTE_SOURCE)])
    write_header(draw_output, "CNA::Tests::GpuInstanceCullerDrawHlslGenerated",
                 [("kDrawVertexSource", vertex, vertex_source),
                  ("kDrawFragmentSource", fragment, fragment_source)])
    print(f"D3D11 culler HLSL stages: {compute_output}, {draw_output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslang", type=Path, required=True)
    parser.add_argument("--spirv-cross", type=Path, required=True)
    parser.add_argument("--fxc", type=Path)
    parser.add_argument("--compute-output", type=Path, default=COMPUTE_OUTPUT)
    parser.add_argument("--draw-output", type=Path, default=DRAW_OUTPUT)
    args = parser.parse_args()
    generate(args.glslang, args.spirv_cross, args.fxc,
             args.compute_output, args.draw_output)


if __name__ == "__main__":
    main()
