# SPDX-License-Identifier: MS-PL
"""Build Direct3D variants of CNA's depth/normal/velocity prepass shaders."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import tempfile

from generate_post_process_hlsl import translate


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "modules/graphics-ext/src/shaders/depth_normal_prepass"
DEFAULT_OUTPUT = SOURCES / "DepthNormalPrepassHlsl.generated.hpp"


def generate(glslang: Path, spirv_cross: Path, fxc: Path | None, output: Path) -> None:
    stages = (
        ("rigid.vulkan.vert.glsl", "kRigidVertexHlsl",
         ("POSITION", "NORMAL")),
        ("skinned.vulkan.vert.glsl", "kSkinnedVertexHlsl",
         ("POSITION", "NORMAL", "TEXCOORD", "BLENDWEIGHT", "BLENDINDICES")),
        ("prepass.vulkan.frag.glsl", "kPrepassFragmentHlsl", ()),
        ("prepass_velocity.vulkan.frag.glsl", "kVelocityFragmentHlsl", ()),
    )
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "// Rebuild with tools/shader_package/generate_depth_normal_prepass_hlsl.py.",
        "#pragma once",
        "#include <string_view>",
        "namespace CNA::Graphics::detail::DepthNormalPrepassHlslGenerated {",
    ]
    with tempfile.TemporaryDirectory(prefix="cna-prepass-hlsl-") as directory:
        scratch = Path(directory)
        for filename, symbol, semantics in stages:
            source = SOURCES / filename
            if not source.exists():
                raise FileNotFoundError(source)
            hlsl = translate(source, glslang, spirv_cross, fxc, scratch,
                             vertex_semantics=semantics,
                             first_fragment_cbuffer_slot=4)
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            lines.append(f'// {filename} SHA-256: {digest}')
            lines.append(
                f'inline constexpr std::string_view {symbol} = '
                f'R"CNA_HLSL({hlsl})CNA_HLSL";')
    lines += ["}", ""]
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines))
    print(f"{len(stages)} HLSL prepass stages: {output}")


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
