#!/usr/bin/env python3
"""Compile this directory's annotated-GLSL shaders into sokol_shaders.hpp via sokol-shdc.

plans/plan_sokol.md SOKOL-14 / design decision 4: shaders are compiled OFFLINE and the generated
header is checked in, so an ordinary CNA build needs no sokol-shdc binary and no network. Run
this script only when a .glsl file in this directory changes, then commit the regenerated
sokol_shaders.hpp alongside it. This mirrors the Bgfx renderer's own checked-in
bgfx_shaders.hpp convention.

sokol-shdc is not vendored (it is an 11 MB prebuilt binary per host platform, distributed via
https://github.com/floooh/sokol-tools-bin). Point the script at one with --shdc, or via the
SOKOL_SHDC environment variable, or have `sokol-shdc` on PATH.

Usage:
    ./compile_shaders.py                       # regenerate sokol_shaders.hpp in place
    ./compile_shaders.py --shdc /path/sokol-shdc
    ./compile_shaders.py --check               # fail if the checked-in header is stale

The target shader languages below cover every sokol_gfx renderer CNA's CNA_SOKOL_API CMake
option can select (design decision 2). --ifdef wraps each language's generated code in the
matching SOKOL_* guard, so only the selected one is actually compiled.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "sokol_shaders.hpp"

# One entry per @program-bearing source file in this directory.
SOURCES = ["sprite.glsl", "colored3d.glsl", "textured3d.glsl", "lit3d.glsl", "dualtextured3d.glsl",
           "skinned3d.glsl", "instanced3d.glsl", "envmap3d.glsl"]

# glsl410 = SOKOL_GLCORE, glsl300es = SOKOL_GLES3, hlsl5 = SOKOL_D3D11,
# metal_macos = SOKOL_METAL, wgsl = SOKOL_WGPU. spirv_vk (SOKOL_VULKAN) is deliberately
# excluded: CNA already has a first-class Vulkan renderer of its own, and CNA_SOKOL_API does
# not offer VULKAN.
SLANGS = "glsl410:glsl300es:hlsl5:metal_macos:wgsl"

LICENSE_HEADER = "// SPDX-License-Identifier: MS-PL\n"


def find_shdc(explicit: str | None) -> str:
    for candidate in (explicit, os.environ.get("SOKOL_SHDC"), shutil.which("sokol-shdc")):
        if candidate and Path(candidate).is_file():
            return str(candidate)
    sys.exit(
        "sokol-shdc not found. Pass --shdc <path>, set SOKOL_SHDC, or put it on PATH.\n"
        "Prebuilt binaries: https://github.com/floooh/sokol-tools-bin (bin/<platform>/sokol-shdc)"
    )


def generate(shdc: str) -> str:
    """Runs sokol-shdc over every SOURCES entry and returns the concatenated header text."""
    chunks = [LICENSE_HEADER]
    with tempfile.TemporaryDirectory() as tmp:
        for index, source in enumerate(SOURCES):
            out = Path(tmp) / f"{Path(source).stem}.h"
            # --no-log-cmdline keeps the absolute path of whoever regenerated the header out of
            # the checked-in file, so the output is reproducible across machines.
            subprocess.run(
                [shdc, "-i", source, "-o", str(out), "-l", SLANGS,
                 "-f", "sokol_impl", "--ifdef", "--no-log-cmdline"],
                cwd=HERE, check=True,
            )
            text = out.read_text(encoding="utf-8")
            if index > 0:
                # Only the first chunk keeps #pragma once; the rest are appended verbatim.
                text = text.replace("#pragma once\n", "", 1)
            # sokol-shdc's output has no trailing newline, so without this the next chunk's
            # leading `/*` lands inside the previous one's final `//` comment and silently
            # swallows a whole declaration block.
            if not text.endswith("\n"):
                text += "\n"
            chunks.append(text)
    return "".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shdc", help="path to the sokol-shdc binary")
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero if sokol_shaders.hpp is out of date")
    args = parser.parse_args()

    generated = generate(find_shdc(args.shdc))

    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != generated:
            print(f"{OUTPUT.name} is out of date -- rerun {Path(__file__).name}", file=sys.stderr)
            return 1
        print(f"{OUTPUT.name} is up to date")
        return 0

    OUTPUT.write_text(generated, encoding="utf-8")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
