#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_opengl4_modern_graphics.md GL4-0005: derive OpenGL4's EasyGL parity corpus.

EasyGL is the reference renderer family for OpenGL4's classic surface, and the most direct
evidence that OpenGL4 provides an EasyGL capability is that the very test source which proves it
on EasyGL also passes on OpenGL4. Most of EasyGL's example sources are renderer-neutral -- public
XNA API plus the shared PixelTestGame harness -- so they build unchanged against any renderer.

This reads EasyGL's own example registrations (modules/renderers/easygl/examples/CMakeLists.txt)
and writes modules/renderers/opengl4/examples/EasyGLParityCorpus.cmake: one entry per EasyGL
registration whose source

  * names no SDL symbol or header (OpenGL4 is validated SDL-free, GL4-0003), and
  * includes no EasyGL-internal header (those assert EasyGL's own objects, not the contract).

Registrations that pass extra command-line arguments or run through a script are skipped: they
are harness wrappers, not a single program the corpus can reuse. Everything else is listed with
the working directory and timeout EasyGL registered it with, so the OpenGL4 twin runs under the
same conditions.

    generate_easygl_parity_corpus.py            rewrite the .cmake file
    generate_easygl_parity_corpus.py --check    exit 1 when the file is stale
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EASYGL_CMAKE = ROOT / "modules/renderers/easygl/examples/CMakeLists.txt"
OUTPUT = ROOT / "modules/renderers/opengl4/examples/EasyGLParityCorpus.cmake"

# An SDL header or an SDL API name (SDL_Window, SDL_GetRenderer, ...). All-capital spellings such as
# CNA_RENDERER_SDL_GPU or the "SDL_RENDERER" identity string are renderer names, not SDL usage.
SDL_PATTERN = re.compile(r"#\s*include\s*[<\"]SDL|(?<![A-Za-z_])SDL_[A-Z][a-z]")
EASYGL_INTERNAL = re.compile(r"Renderers/EasyGL/|EasyGLRenderer|easygl::|metagl")

PATH_VARS = {
    "${CMAKE_SOURCE_DIR}": ROOT,
    "${CNA_GRAPHICS_EXAMPLES_DIR}": ROOT / "modules/graphics/examples",
}


def resolve(source: str) -> Path:
    for var, base in PATH_VARS.items():
        if source.startswith(var):
            return base / source[len(var):].lstrip("/")
    return ROOT / "modules/renderers/easygl/examples" / source


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def cmake_path(path: Path) -> str:
    return "${CMAKE_SOURCE_DIR}/" + path.relative_to(ROOT).as_posix()


def main() -> int:
    text = EASYGL_CMAKE.read_text()
    targets = {}
    for match in re.finditer(r"cna_easygl_test\(\s*(\S+)\s+([^\)\s]+)\s*\)", text):
        targets[match.group(1)] = match.group(2)

    # Per-target additions EasyGL makes after its builder macro: extra libraries and include
    # directories. They are carried over so the OpenGL4 twin links and compiles the same way.
    libraries = {}
    for match in re.finditer(r"target_link_libraries\(\s*(\S+)\s+PRIVATE\s+([^\)]*)\)", text):
        libraries.setdefault(match.group(1), []).extend(match.group(2).split())
    includes = {}
    for match in re.finditer(r"target_include_directories\(\s*(\S+)\s+PRIVATE\s+([^\)]*)\)", text):
        includes.setdefault(match.group(1), []).extend(match.group(2).split())

    entries = []
    skipped = {"args": 0, "sdl": 0, "internal": 0, "missing": 0}
    for match in re.finditer(
            r"cna_register_renderer_test\(\s*NAME\s+(\S+)\s+COMMAND\s+(.*?)\)\s*\n", text, re.S):
        name, rest = match.group(1), match.group(2)
        command = rest.split()[0]
        if command not in targets:
            skipped["args"] += 1
            continue
        tail = rest[len(command):].strip()
        if tail and not re.match(r"(TIMEOUT|LABELS|ENVIRONMENT|WORKING_DIRECTORY)\b", tail):
            skipped["args"] += 1
            continue
        source = resolve(targets[command])
        if not source.is_file():
            skipped["missing"] += 1
            continue
        body = strip_comments(source.read_text(errors="replace"))
        if SDL_PATTERN.search(body):
            skipped["sdl"] += 1
            continue
        if EASYGL_INTERNAL.search(body):
            skipped["internal"] += 1
            continue
        timeout = re.search(r"TIMEOUT\s+(\d+)", tail)
        workdir = "SOURCE" if re.search(r"WORKING_DIRECTORY\s+\"\$\{(CMAKE|CNA)_SOURCE_DIR\}\"", tail) else "BINARY"
        short = name[len("EasyGL_"):] if name.startswith("EasyGL_") else name
        target = command.removeprefix("cna_test_easygl_").removeprefix("cna_test_")
        libs = [lib for lib in libraries.get(command, []) if lib not in ("easy-gl",)]
        if len(libs) != len(libraries.get(command, [])):
            skipped["internal"] += 1
            continue
        entries.append((short, target, cmake_path(source),
                        timeout.group(1) if timeout else "60", workdir,
                        " ".join(libs), " ".join(includes.get(command, []))))

    entries.sort()
    lines = [
        "# GENERATED by tools/opengl4/generate_easygl_parity_corpus.py -- do not edit by hand.",
        "# plans/plan_opengl4_modern_graphics.md GL4-0005: EasyGL registrations whose source is",
        "# renderer-neutral (no SDL, no EasyGL internals), rebuilt against OPENGL4 unchanged.",
        f"# {len(entries)} entries; skipped: {skipped['args']} harness/argument registrations,",
        f"# {skipped['sdl']} SDL-naming sources, {skipped['internal']} EasyGL-internal sources,",
        f"# {skipped['missing']} missing sources.",
        "#",
        "# Each entry: <test suffix>|<target suffix>|<source>|<timeout>|<BINARY or SOURCE working dir>",
        "#             |<extra link libraries>|<extra include directories>",
        "set(CNA_OPENGL4_EASYGL_PARITY_CORPUS",
    ]
    for short, target, source, timeout, workdir, libs, incs in entries:
        lines.append(f"    \"{short}|{target}|{source}|{timeout}|{workdir}|{libs}|{incs}\"")
    lines.append(")")
    generated = "\n".join(lines) + "\n"

    if "--check" in sys.argv[1:]:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != generated:
            print(f"{OUTPUT.relative_to(ROOT)} is stale; rerun {Path(__file__).name}")
            return 1
        return 0
    OUTPUT.write_text(generated)
    print(f"wrote {len(entries)} entries to {OUTPUT.relative_to(ROOT)}; skipped {skipped}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
