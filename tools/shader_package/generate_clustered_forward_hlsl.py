# SPDX-License-Identifier: MS-PL
"""Build the DX11 clustered-forward stages from the Vulkan GLSL sources."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import tempfile

from generate_post_process_hlsl import translate


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "modules/graphics-ext/src/shaders/clustered_forward"
DEFAULT_OUTPUT = SOURCES / "ClusteredForwardHlsl.generated.hpp"


def chunks(source: str, limit: int = 8000) -> list[str]:
    parts: list[str] = []
    current = ""
    for line in source.splitlines(keepends=True):
        if len(line) > limit:
            raise ValueError("one generated HLSL line exceeds the MSVC literal limit")
        if len(current) + len(line) > limit:
            parts.append(current)
            current = ""
        current += line
    if current:
        parts.append(current)
    return parts


def generate(glslang: Path, spirv_cross: Path, fxc: Path | None,
             output: Path) -> None:
    stages = (("Vertex", "forward.vulkan.vert.glsl"),
              ("Fragment", "forward.vulkan.frag.glsl"))
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_clustered_forward_hlsl.py.",
        "#pragma once",
        "#include <array>",
        "#include <string>",
        "#include <string_view>",
        "namespace CNA::Graphics::detail::ClusteredForwardHlslGenerated {",
    ]
    with tempfile.TemporaryDirectory(prefix="cna-cluster-hlsl-") as directory:
        scratch = Path(directory)
        for stage, name in stages:
            source = SOURCES / name
            hlsl = translate(source, glslang, spirv_cross, fxc, scratch,
                             vertex_semantics=("POSITION", "NORMAL"),
                             first_fragment_cbuffer_slot=4)
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            parts = chunks(hlsl)
            lines.append(f'inline constexpr std::string_view k{stage}SourceSha256 = "{digest}";')
            lines.append(
                f'inline constexpr std::array<std::string_view, {len(parts)}> '
                f'k{stage}Parts{{{{')
            for part in parts:
                if ")CNA_HLSL" in part:
                    raise ValueError("generated shader contains its raw-string delimiter")
                lines.append(f'    R"CNA_HLSL({part})CNA_HLSL",')
            lines.append("}};")
            lines.append(f"inline std::string {stage}Source() {{")
            lines.append("    std::string result;")
            lines.append(f"    result.reserve({len(hlsl)});")
            lines.append(f"    for (const auto part : k{stage}Parts) result.append(part);")
            lines.append("    return result;")
            lines.append("}")
    lines += ["}", ""]
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))
    print(f"clustered-forward HLSL: {output}")


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
