#!/usr/bin/env python3
"""Generate llgl_shaders.hpp from the GLSL sources in this directory.

The LLGL renderer picks its renderer module at runtime, so it needs BOTH shader flavours available
in the binary at once:

  *.vert.glsl / *.frag.glsl        Vulkan flavour, compiled here to SPIR-V words
  *.gl.vert.glsl / *.gl.frag.glsl  OpenGL flavour, embedded verbatim as GLSL source strings

Both are emitted into a single generated header so a build needs no shader toolchain at all -- the
same discipline as the other runtime-selected renderers' checked-in generated headers.

Requires glslangValidator (Debian/Ubuntu package: glslang-tools) on PATH, or --glslang <path>.

    ./compile_shaders.py                 # rewrites llgl_shaders.hpp in place
    ./compile_shaders.py --check         # verifies the checked-in header is up to date (exit 1 if not)
"""

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_OUTPUT = HERE / "llgl_shaders.hpp"

# (C++ identifier stem, Vulkan/SPIR-V source, OpenGL/GLSL source, glslang stage)
SHADERS = [
    ("Sprite2dVert", "sprite2d.vert.glsl", "sprite2d.gl.vert.glsl", "vert"),
    ("Sprite2dFrag", "sprite2d.frag.glsl", "sprite2d.gl.frag.glsl", "frag"),
    ("Colored3dVert", "colored3d.vert.glsl", "colored3d.gl.vert.glsl", "vert"),
    # LLGL-52: no colour attribute at all (untextured+unlit BasicEffect with a colourless vertex
    # layout, e.g. VertexPositionNormalTexture) -- pairs with Untextured3dFrag below unchanged.
    ("Flat3dVert", "flat3d.vert.glsl", "flat3d.gl.vert.glsl", "vert"),
    ("Textured3dVert", "textured3d.vert.glsl", "textured3d.gl.vert.glsl", "vert"),
    ("ColoredTextured3dVert", "colored_textured3d.vert.glsl", "colored_textured3d.gl.vert.glsl", "vert"),
    ("Untextured3dFrag", "untextured3d.frag.glsl", "untextured3d.gl.frag.glsl", "frag"),
    ("Textured3dFrag", "textured3d.frag.glsl", "textured3d.gl.frag.glsl", "frag"),
    ("LitTextured3dVert", "lit_textured3d.vert.glsl", "lit_textured3d.gl.vert.glsl", "vert"),
    # LLGL-52: lit+textured with no normal attribute (e.g. plain VertexPositionTexture) -- defaults
    # to a fixed (0, 0, 1) object-space normal instead of refusing the draw. Pairs with
    # LitTextured3dFrag below unchanged.
    ("LitTextured3dFlatNormalVert", "lit_textured3d_flatnormal.vert.glsl",
     "lit_textured3d_flatnormal.gl.vert.glsl", "vert"),
    ("LitColoredTextured3dVert", "lit_colored_textured3d.vert.glsl", "lit_colored_textured3d.gl.vert.glsl", "vert"),
    ("LitTextured3dFrag", "lit_textured3d.frag.glsl", "lit_textured3d.gl.frag.glsl", "frag"),
    ("LitColored3dVert", "lit_colored3d.vert.glsl", "lit_colored3d.gl.vert.glsl", "vert"),
    # LLGL-52: lit+untextured with no vertex-colour attribute either (e.g. VertexPositionNormalTexture
    # with TextureEnabled=false, VertexColorEnabled=false, EnableDefaultLighting()) -- pairs with
    # LitUntextured3dFrag below unchanged.
    ("LitFlat3dVert", "lit_flat3d.vert.glsl", "lit_flat3d.gl.vert.glsl", "vert"),
    ("LitUntextured3dFrag", "lit_untextured3d.frag.glsl", "lit_untextured3d.gl.frag.glsl", "frag"),
    ("DualTextured3dFrag", "dual_textured3d.frag.glsl", "dual_textured3d.gl.frag.glsl", "frag"),
    ("EnvMap3dVert", "env_map3d.vert.glsl", "env_map3d.gl.vert.glsl", "vert"),
    ("EnvMap3dFrag", "env_map3d.frag.glsl", "env_map3d.gl.frag.glsl", "frag"),
    ("Skinned3dVert", "skinned3d.vert.glsl", "skinned3d.gl.vert.glsl", "vert"),
    ("Skinned3dFrag", "skinned3d.frag.glsl", "skinned3d.gl.frag.glsl", "frag"),
    ("Skinned3dColorVert", "skinned3d_color.vert.glsl", "skinned3d_color.gl.vert.glsl", "vert"),
    ("Skinned3dColorFrag", "skinned3d_color.frag.glsl", "skinned3d_color.gl.frag.glsl", "frag"),
    ("Pbr3dVert", "pbr3d.vert.glsl", "pbr3d.gl.vert.glsl", "vert"),
    # plan_gltf.md GLTF-462/GLTF-465: the rigid dual-UV variant is only ever selected for stride 60,
    # and every stride-60 record carries a packed COLOR_0 slot -- so it always takes the vertex-colour
    # define too, and specularState.z decides whether the colour multiplies.
    ("Pbr3dDualUvVert", "pbr3d.vert.glsl", "pbr3d.gl.vert.glsl", "vert",
     "CNA_PBR_DUAL_UV", "CNA_PBR_VERTEX_COLOR"),
    ("Pbr3dFrag", "pbr3d.frag.glsl", "pbr3d.gl.frag.glsl", "frag"),
    ("Pbr3dSkinnedVert", "pbr3d_skinned.vert.glsl", "pbr3d_skinned.gl.vert.glsl", "vert"),
    ("Pbr3dSkinnedDualUvVert", "pbr3d_skinned.vert.glsl", "pbr3d_skinned.gl.vert.glsl",
     "vert", "CNA_PBR_DUAL_UV"),
    # plan_gltf.md GLTF-463: stride 80 -- the stride-76 skinned record with a packed COLOR_0. Stride 76
    # has no colour attribute to bind, so this needs its own variant rather than a runtime flag.
    ("Pbr3dSkinnedDualUvColorVert", "pbr3d_skinned.vert.glsl", "pbr3d_skinned.gl.vert.glsl",
     "vert", "CNA_PBR_DUAL_UV", "CNA_PBR_VERTEX_COLOR"),
]


def compile_spirv(glslang: str, source: Path, stage: str, defines=()) -> list:
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / (source.name + ".spv")
        result = subprocess.run(
            [glslang, "-V", "-S", stage, "-o", str(output),
             *[f"-D{define}=1" for define in defines], str(source)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout + result.stderr)
            raise SystemExit(f"glslangValidator failed for {source.name}")
        blob = output.read_bytes()

    if len(blob) % 4 != 0:
        raise SystemExit(f"{source.name}: SPIR-V blob is not a whole number of 32-bit words")
    return list(struct.unpack(f"<{len(blob) // 4}I", blob))


def format_spirv(name: str, words: list) -> str:
    lines = [f"    /** @brief SPIR-V words for the Vulkan flavour of the {name} shader. */",
             f"    inline constexpr std::uint32_t k{name}Spv[] = {{"]
    for index in range(0, len(words), 8):
        row = "".join(f"0x{word:08x}u, " for word in words[index:index + 8])
        lines.append("        " + row.rstrip())
    lines.append("    };")
    return "\n".join(lines)


def format_glsl(name: str, source: str) -> str:
    return (
        f"    /** @brief GLSL source for the OpenGL flavour of the {name} shader. */\n"
        f"    inline constexpr const char* k{name}Glsl = R\"GLSL(\n"
        f"{source.rstrip()}\n"
        f")GLSL\";"
    )


def apply_glsl_defines(source: str, defines) -> str:
    """Insert variant defines after #version, which GLSL requires to remain the first directive."""
    if not defines:
        return source
    lines = source.splitlines()
    version_index = next(index for index, line in enumerate(lines)
                         if line.lstrip().startswith("#version"))
    lines[version_index + 1:version_index + 1] = [f"#define {define} 1" for define in defines]
    return "\n".join(lines) + ("\n" if source.endswith("\n") else "")


def generate(glslang: str) -> str:
    chunks = [
        "// SPDX-License-Identifier: MS-PL",
        "// AUTO-GENERATED by compile_shaders.py -- do not edit by hand.",
        "// Regenerate after any shader change: src/CNA/Internal/Renderers/Llgl/shaders/compile_shaders.py",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace CNA::Internal::Renderers::Llgl::Shaders",
        "{",
    ]
    for shader in SHADERS:
        name, spirv_source, glsl_source, stage = shader[:4]
        defines = shader[4:]
        words = compile_spirv(glslang, HERE / spirv_source, stage, defines)
        chunks.append(format_spirv(name, words))
        chunks.append("")
        chunks.append(format_glsl(
            name, apply_glsl_defines((HERE / glsl_source).read_text(), defines)))
        chunks.append("")
    chunks.append("}")
    chunks.append("")
    return "\n".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslang", default=shutil.which("glslangValidator"))
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true",
                        help="do not write; fail if the existing header differs")
    args = parser.parse_args()

    if not args.glslang:
        raise SystemExit("glslangValidator not found -- install glslang-tools or pass --glslang")

    generated = generate(args.glslang)

    if args.check:
        if not args.output.exists():
            print(f"{args.output} is missing")
            return 1
        if args.output.read_text() != generated:
            print(f"{args.output} is out of date -- re-run compile_shaders.py")
            return 1
        print(f"{args.output} is up to date")
        return 0

    args.output.write_text(generated)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
