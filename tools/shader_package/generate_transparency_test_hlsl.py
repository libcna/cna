# SPDX-License-Identifier: MS-PL
"""Build HLSL variants of the renderer-neutral transparency test shaders."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import tempfile

from generate_post_process_hlsl import translate


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "modules/graphics-ext/tests/CNA/Graphics/shaders/transparency"
DEFAULT_OUTPUT = SOURCES.parent.parent / "TransparencyHlsl.generated.hpp"


def generate(glslang: Path, spirv_cross: Path, fxc: Path | None, output: Path) -> None:
    stages = (
        ("basic.vulkan.vert.glsl", "kBasicVertexHlsl"),
        ("direct.vulkan.vert.glsl", "kDirectVertexHlsl"),
        ("emitter.vulkan.frag.glsl", "kEmitterFragmentHlsl"),
        ("flat.vulkan.frag.glsl", "kFlatFragmentHlsl"),
        ("weight_probe.vulkan.frag.glsl", "kWeightProbeFragmentHlsl"),
    )
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_transparency_test_hlsl.py.",
        "#pragma once",
        "#include <string_view>",
        "namespace CNA::Tests::TransparencyHlslGenerated {",
    ]
    with tempfile.TemporaryDirectory(prefix="cna-transparency-hlsl-") as directory:
        scratch = Path(directory)
        for filename, symbol in stages:
            source = SOURCES / filename
            hlsl = translate(source, glslang, spirv_cross, fxc, scratch,
                             vertex_semantics=("POSITION",),
                             first_fragment_cbuffer_slot=4)
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            lines.append(f'// {filename} SHA-256: {digest}')
            lines.append(
                f'inline constexpr std::string_view {symbol} = '
                f'R"CNA_HLSL({hlsl})CNA_HLSL";')
    lines += ["}", ""]
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))
    print(f"{len(stages)} HLSL transparency test stages: {output}")


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
