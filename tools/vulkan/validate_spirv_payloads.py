#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_vulkan_modern_graphics.md VMG-0005: validate every checked-in SPIR-V payload.

The Vulkan renderer's own shaders (modules/renderers/vulkan/src/shaders/spirv_shaders.hpp) and every
generated shader package (`*ShaderPackage.generated.hpp`, tools/shader_package) embed SPIR-V as
`uint32_t NAME[] = { ... };` word arrays. A driver may accept a malformed module and misbehave, or
reject it only on another vendor, so each array whose first word is the SPIR-V magic number is
extracted and run through `spirv-val` against the environment the renderer creates (Vulkan 1.1,
VulkanRenderer.cpp's VkApplicationInfo::apiVersion), with the same layout rules it enables.

    validate_spirv_payloads.py [--spirv-val PATH] [--target-env vulkan1.1] <header-or-dir>...

Exit 0 when every payload validates, 1 when any does not, 77 (ctest skip) without spirv-val.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAGIC = 0x07230203
ARRAY = re.compile(r"(?:std::)?uint32_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*\]\s*=\s*\{([^}]*)\}", re.S)
NUMBER = re.compile(r"0[xX][0-9a-fA-F]+|\d+")


def find_spirv_val(explicit):
    for candidate in [explicit, os.environ.get("SPIRV_VAL"), shutil.which("spirv-val"),
                      str(Path.home() / "deps/spirv-tools/usr/bin/spirv-val")]:
        if candidate and Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def payloads(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    # Comments may quote a hex word; strip them so only real initializers are parsed.
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for match in ARRAY.finditer(text):
        words = [int(token, 0) for token in NUMBER.findall(match.group(2))]
        if words and words[0] == MAGIC:
            yield match.group(1), words


def headers(arguments):
    for argument in arguments:
        path = Path(argument)
        if path.is_dir():
            for suffix in ("*.hpp", "*.h", "*.cpp", "*.inc"):
                for header in sorted(path.rglob(suffix)):
                    yield header
        elif path.is_file():
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--spirv-val")
    parser.add_argument("--target-env", default="vulkan1.1")
    parser.add_argument("paths", nargs="+")
    args = parser.parse_args()

    tool = find_spirv_val(args.spirv_val)
    if tool is None:
        print("[SKIP] spirv-val not found (PATH, $SPIRV_VAL or ~/deps/spirv-tools/usr/bin)")
        return 77

    checked = 0
    failures = []
    versions = {}
    with tempfile.TemporaryDirectory(prefix="cna-spirv-") as scratch:
        for header in headers(args.paths):
            for name, words in payloads(header):
                version = f"{(words[1] >> 16) & 0xff}.{(words[1] >> 8) & 0xff}"
                versions[version] = versions.get(version, 0) + 1
                module = Path(scratch) / f"{name}.spv"
                module.write_bytes(b"".join(word.to_bytes(4, "little") for word in words))
                result = subprocess.run([tool, "--target-env", args.target_env, str(module)],
                                        capture_output=True, text=True)
                checked += 1
                if result.returncode != 0:
                    failures.append((header, name, (result.stdout + result.stderr).strip()))
    for header, name, message in failures:
        print(f"INVALID  {header}:{name}\n{message}\n")
    summary = ", ".join(f"SPIR-V {v}: {n}" for v, n in sorted(versions.items()))
    print(f"validate_spirv_payloads: {checked} module(s) checked with {tool} --target-env "
          f"{args.target_env} ({summary}); {len(failures)} invalid")
    if checked == 0:
        print("validate_spirv_payloads: no SPIR-V payload found -- the paths are wrong")
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
