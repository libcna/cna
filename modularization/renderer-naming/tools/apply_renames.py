#!/usr/bin/env python3
"""CNA terminology + renderer-identity normalization engine (see ../DESIGN.md).

Applies one normalization pass to the ACTIVE scope of the repository:

  a   -- graphics "backend" -> "renderer" terminology (no identity changes)
  b1  -- DX1..DX8 / D3D9..D3D12 -> DIRECTX1..DIRECTX12 identity normalization
  b2  -- OPENGLES -> OPENGLES3
  c   -- NOXNA -> CNAEXT / CNA_NOXNA -> CNA_CNAEXT extension-macro normalization

Each pass rewrites file contents by an ordered, word-boundary-aware rule list,
renames files/directories through `git mv`, and writes machine-readable maps:

  maps/<pass>-rule-hits.tsv   -- regex rule -> replacement -> total hits -> files touched
  maps/<pass>-moves.tsv       -- old path -> new path (every git mv performed)
  maps/<pass>-token-map.tsv   -- concrete old token -> new token instances (unique)

Third-party/upstream identifiers and other subsystems' backend concepts are
sentinel-protected and restored verbatim (PROTECT below). Historical evidence
(audit/, plan_*.md, spikes, previous campaigns) is out of scope entirely.

Usage: apply_renames.py --pass {a,b1,b2,c} [--dry-run]
"""
import argparse
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
MAPS = os.path.join(os.path.dirname(HERE), "maps")

# --------------------------------------------------------------------------- scopes

FULL_PATHSPECS = [
    "modules/CMakeLists.txt",
    "modules/core", "modules/math", "modules/graphics", "modules/graphics-ext",
    "modules/runtime", "modules/media", "modules/content", "modules/storage",
    "modules/renderers", "cmake", "scripts", "tests", "tools", "examples", "docs",
    "CMakeLists.txt", "CMakePresets.json", "main.cpp", "Doxyfile",
    "README.md", "CLAUDE.md", "CHECKLIST.md", "NOXNA.md", "CNAEXT.md",
    ".github",
]

COMPOUND_PATHSPECS = [
    "modules/input", "modules/audio", "modules/devices", "modules/devices-ext",
    "modules/net", "modules/gamer-services",
]

# FULL-scope files whose bare-"backend" vocabulary belongs to another subsystem:
# demote them to compound-only treatment.
DEMOTE_TO_COMPOUND = re.compile(
    r"^docs/(input-[^/]*\.md|devices[^/]*|platform-input-notes\.md)$"
    r"|^\.github/workflows/(input-ci|devices-tests)\.yml$"
    r"|^tools/(input_parity|audio|net|media)/"
)

BINARY_EXT = (".png", ".jpg", ".jpeg", ".gif", ".ico", ".bmp", ".xnb", ".wav",
              ".ogg", ".mp3", ".mp4", ".ttf", ".otf", ".bin", ".exe", ".dll",
              ".a", ".o", ".zip", ".tar", ".gz", ".pdf", ".glb", ".cnj")


def tracked(pathspecs):
    out = subprocess.run(["git", "ls-files", "-z", "--"] + pathspecs, cwd=REPO,
                         check=True, capture_output=True).stdout
    return sorted(p.decode() for p in out.split(b"\0") if p)


# --------------------------------------------------------------------------- protection

PROTECT = [
    # Skia upstream (Gr* / SkSurfaces)
    r"GrBackendRenderTargets?", r"GrBackendSurface", r"GrGLBackendSurface",
    r"WrapBackendRenderTarget",
    # sokol upstream
    r"sg_query_backend", r"sg_backend", r"SG_BACKEND_[A-Z0-9_]+",
    # bgfx upstream renderer-type vocabulary (RendererType::OpenGLES and its name string)
    r"bgfx::RendererType::\w+", r"\"OpenGLES\"",
    # other CNA subsystems' backend concepts that may be referenced from FULL-scope files
    r"I?Sdl(?:Gamepad|Joystick|Haptic|Camera|FileDialog|MessageBox|Tray)Backend\w*",
    r"FakeSdl(?:Gamepad|Joystick|Haptic)Backend\w*", r"FakeSystemDeviceBackend",
    r"I?System(?:Device|Keyboard|Mouse|Power|Sensor)Backend\w*",
    r"SdlHapticVibrateBackend", r"I?VibrateBackend\w*",
    r"Android(?:Compass|Motion)Backend\w*", r"I(?:Compass|Motion)Backend\w*",
    r"I(?:Camera|FileDialog|MessageBox|Tray)Backend\w*", r"ENetBackend\w*",
    r"[Aa]udio backends?", r"SDL3?_mixer backend", r"FAudio backend",
    r"ENet backend", r"input backends?",
    # historical file references that must stay textually accurate
    r"BackendLibraries\.cmake", r"BackendLibraries",
    r"input-backend\.md", r"devices-native-backend-design\.md",
    r"input_noxna(?:_progress)?\.md", r"noxna_devices\.md",
]

SENT = "⁉CNAPROT{}⁉"  # unlikely sentinel


def protect(text, store):
    for i, pat in enumerate(PROTECT):
        def repl(m, i=i):
            store.setdefault(i, [])
            store[i].append(m.group(0))
            return SENT.format(f"{i}_{len(store[i]) - 1}")
        text = re.sub(pat, repl, text)
    return text


def restore(text, store):
    def repl(m):
        i, j = m.group(1).split("_")
        return store[int(i)][int(j)]
    return re.sub(SENT.format(r"(\d+_\d+)"), repl, text)


# --------------------------------------------------------------------------- rules

FAMILIES = ("Ascii|Bgfx|Canvas|Diligent|Direct2D|EasyGL|FreeDirect|Gdi|Glide|"
            "Headless|HtmlDom|Llgl|Magnum|Metal|OpenGL1|OpenGL2|OpenGL4|OpenGLES1|"
            "SdlGpu|Skia|Software|Sokol|Stub|Vulkan|WebGPU|Wicked|"
            "Dx1|Dx2|Dx3|Dx5|Dx6|Dx7|Dx8|D3D9|D3D10|D3D11|D3D12")

RULES_A = [
    (r"getCurrentGraphicsBackendType", "getCurrentGraphicsRendererType"),
    (r"getCurrentGraphicsBackendName", "getCurrentGraphicsRendererName"),
    (r"GetGraphicsBackendType", "GetGraphicsRendererType"),
    (r"GetGraphicsBackendName", "GetGraphicsRendererName"),
    (r"GraphicsBackendCreateArgs", "GraphicsRendererCreateArgs"),
    (r"CreateGraphicsBackend", "CreateGraphicsRenderer"),
    (rf"\b({FAMILIES})GraphicsBackend", r"\1Renderer"),
    (r"\bSdlGraphicsBackend", "SdlRenderer"),
    (r"GraphicsBackend", "GraphicsRenderer"),
    (r"graphicsBackend", "graphicsRenderer"),
    (r"GRAPHICS_BACKEND", "GRAPHICS_RENDERER"),
    (r"graphics_backend", "graphics_renderer"),
    (r"cna_backend_graphics_d3dcommon", "cna_renderer_d3dcommon"),
    (r"cna_backend_graphics_d3d9_effect", "cna_renderer_d3d9_effect"),
    (r"cna_backend_graphics_", "cna_renderer_"),
    (r"\bBACKEND_TARGET", "RENDERER_TARGET"),
    (r"\bBACKEND_DIR", "RENDERER_DIR"),
    (r"_cna_backend_", "_cna_renderer_"),
    (r"_cna_default_backend", "_cna_default_renderer"),
    (r"_cna_enabled_backends", "_cna_enabled_renderers"),
    (r"_cna_explicit_backend_selection", "_cna_explicit_renderer_selection"),
    (r"cna_add_renderer_backend", "cna_add_renderer"),
    (r"cna_renderer_backend_common_setup", "cna_renderer_common_setup"),
    (r"cna_register_backend_test", "cna_register_renderer_test"),
    (r"CNA_BACKEND_DEFINE", "CNA_RENDERER_DEFINE"),
    (r"CNA_BACKEND_", "CNA_RENDERER_"),
    (r"CNA/Internal/Backends", "CNA/Internal/Renderers"),
    (r"CNA::Internal::Backends", "CNA::Internal::Renderers"),
    (r"\bBackends::(?=[A-Z])", "Renderers::"),
    (r"Internal/Backends", "Internal/Renderers"),
    (r"BackendSelection\.cmake", "RendererSelection.cmake"),
    (r"BackendSelection", "RendererSelection"),
    (r"CrossBackendBenchmark", "CrossRendererBenchmark"),
    (r"cross_backend_", "cross_renderer_"),
    (r"_effectbackend_", "_effectrenderer_"),
    (r"resource_backends_test", "resource_renderers_test"),
    (r"run-all-backend-smoke-tests", "run-all-renderer-smoke-tests"),
    (r"graphics-backend-feature-matrix\.md", "graphics-renderer-feature-matrix.md"),
    (r"directx-legacy-backends-analysis\.md", "directx-legacy-renderers-analysis.md"),
    (r"(?<!input)-backend\.md", "-renderer.md"),
    (r"Backend_", "Renderer_"),
    (r"backend_", "renderer_"),
    (r"Backend(?=[A-Z0-9])", "Renderer"),
    (r"backend(?=[A-Z0-9])", "renderer"),
    (r"Backends\b", "Renderers"),
    (r"\bbackends\b", "renderers"),
    (r"Backend\b", "Renderer"),
    (r"\bbackend\b", "renderer"),
    (r"BACKENDS\b", "RENDERERS"),
    (r"BACKEND\b", "RENDERER"),
]

# Unambiguous graphics tokens only -- safe inside input/audio/devices/net scopes.
RULES_A_COMPOUND = [
    RULES_A[i] for i in list(range(0, 15)) + list(range(25, 40))
]

RULES_B1 = [
    (r"GraphicsRendererType::D3D(9|10|11|12)\b", r"GraphicsRendererType::DirectX\1"),
    (r"CNA_RENDERER_DX([1-8])\b", r"CNA_RENDERER_DIRECTX\1"),
    (r"CNA_RENDERER_D3D(9|10|11|12)\b", r"CNA_RENDERER_DIRECTX\1"),
    (r"cna_renderer_dx([1-8])\b", r"cna_renderer_directx\1"),
    (r"cna_renderer_d3d(9|10|11|12)\b", r"cna_renderer_directx\1"),
    (r"\bD3D(9|10|11|12)Renderer\b", r"DirectX\1Renderer"),
    (r"cna_d3d(9|10|11|12)_test", r"cna_directx\1_test"),
    (r"cna_d3d(9|10|11|12)_ctest_command", r"cna_directx\1_ctest_command"),
    (r"cna_dx([1-8])_test", r"cna_directx\1_test"),
    (r"cna_dx([1-8])_ctest_command", r"cna_directx\1_ctest_command"),
    (r"cna_test_d3d(9|10|11|12)_", r"cna_test_directx\1_"),
    (r"cna_test_dx([1-8])_", r"cna_test_directx\1_"),
    (r"(?<![A-Za-z0-9_])_d3d(9|10|11|12)_(?=[a-z0-9_]+\b)", r"_directx\1_"),
    (r"(?<![A-Za-z0-9_])_dx([1-8])_(?=[a-z0-9_]+\b)", r"_directx\1_"),
    (r"Internal/Renderers/D3D(9|10|11|12)/", r"Internal/Renderers/DirectX\1/"),
    (r"Internal::Renderers::D3D(9|10|11|12)\b", r"Internal::Renderers::DirectX\1"),
    (r"modules/renderers/d3d(9|10|11|12)\b", r"modules/renderers/directx\1"),
    (r"modules/renderers/dx([1-8])\b", r"modules/renderers/directx\1"),
    (r"\bD3D(9|10|11|12)_(?=[A-Z][a-z])", r"DirectX\1_"),
    (r"\bDx([1-8])(?=[A-Z_])", r"DirectX\1"),
    (r"\bDx([1-8])\b(?!-)", r"DirectX\1"),
    # (?!-) keeps plan-ledger task IDs (DX2-46, DX7-0, ...) and SDK-era references intact
    (r"\bDX([1-8])\b(?!-)", r"DIRECTX\1"),
    (r"examples/d3d(9|10|11|12)_", r"examples/directx\1_"),
    (r"\bd3d(9|10|11|12)_((?:[a-z0-9]+_)*(?:test|diag|smoke))", r"directx\1_\2"),
    (r"run-wine-dx([1-8])\.sh", r"run-wine-directx\1.sh"),
    (r"run-wine-d3d10\.sh", "run-wine-directx10.sh"),
    (r"check-dx([1-8])-", r"check-directx\1-"),
    (r"check-d3d10-", "check-directx10-"),
    (r"Dx([1-8])Tests\.cmake", r"DirectX\1Tests.cmake"),
    (r"D3D(9|10|11|12)Tests\.cmake", r"DirectX\1Tests.cmake"),
    (r"docs/dx([1-8])-renderer\.md", r"docs/directx\1-renderer.md"),
    (r"docs/d3d(9|10|11|12)-renderer\.md", r"docs/directx\1-renderer.md"),
]

# Quoted public-selector strings -- applied only in registry/registration/test files
# (never on lines that carry sokol's native-API axis).
RULES_B1_QUOTED = [
    (r'"DX([1-8])"', r'"DIRECTX\1"'),
    (r'"D3D(9|10|11|12)"', r'"DIRECTX\1"'),
    (r"'D3D(9|10|11|12)'", r"'DIRECTX\1'"),
    (r"'DX([1-8])'", r"'DIRECTX\1'"),
]
QUOTED_FILES = re.compile(
    r"^(cmake/RendererSelection\.cmake|cmake/Tests/[^/]+\.cmake|CMakePresets\.json"
    r"|cmake/Harnesses\.cmake|cmake/UnitTests\.cmake|cmake/Examples\.cmake"
    r"|\.github/workflows/[^/]+\.yml|scripts/[^/]+"
    r"|modules/CMakeLists\.txt|modules/renderers/CMakeLists\.txt"
    r"|modules/core/include/CNA/GraphicsRendererType\.hpp"
    r"|modules/graphics/tests/.*"
    r"|modules/renderers/(dx[1-8]|d3d9|d3d10|d3d11|d3d12)/.*"
    r"|examples/.*|tests/.*|docs/renderer-registry\.md)$"
)
SOKOL_GUARD = re.compile(r"SOKOL|sokol")

RULES_B2 = [
    (r"CNA_RENDERER_OPENGLES\b", "CNA_RENDERER_OPENGLES3"),
    (r"CNA_GL_PROFILE_OPENGLES\b", "CNA_GL_PROFILE_OPENGLES3"),
    (r"\bOpenGLES\b", "OpenGLES3"),
    (r"\bOPENGLES\b", "OPENGLES3"),
    (r"\bopengles\b", "opengles3"),
]

RULES_C = [
    (r"CNA_NOXNA", "CNA_CNAEXT"),
    (r"cna_noxna", "cna_cnaext"),
    (r"CNA::NoXna", "CNA::CnaExt"),
    (r"probe_noxna", "probe_cnaext"),
    (r"noxna_settings_example", "cnaext_settings_example"),
    (r"_noxna_", "_cnaext_"),
    (r"NOXNA_", "CNAEXT_"),
    (r"cmake-build-noxna", "cmake-build-cnaext"),
    (r"NoXna(?=[A-Z])", "CnaExt"),
    (r"\bNoXna\b", "CnaExt"),
    (r"\bNOXNA\b", "CNAEXT"),
    (r"\bnoxna\b", "cnaext"),
]

PATH_RULES = {
    "a": [
        (r"/Internal/Backends/", "/Internal/Renderers/"),
        (rf"({FAMILIES}|Sdl)GraphicsBackend", r"\1Renderer"),
        (r"GraphicsBackend", "GraphicsRenderer"),
        (r"cmake/BackendSelection\.cmake", "cmake/RendererSelection.cmake"),
        (r"CrossBackendBenchmark", "CrossRendererBenchmark"),
        (r"graphics_backend_benchmark", "graphics_renderer_benchmark"),
        (r"cross_backend_", "cross_renderer_"),
        (r"_effectbackend_", "_effectrenderer_"),
        (r"resource_backends_test", "resource_renderers_test"),
        (r"run-all-backend-smoke-tests", "run-all-renderer-smoke-tests"),
        (r"graphics-backend-feature-matrix\.md", "graphics-renderer-feature-matrix.md"),
        (r"directx-legacy-backends-analysis\.md", "directx-legacy-renderers-analysis.md"),
        (r"(?<!input)-backend\.md", "-renderer.md"),
        (r"Backend(?=[A-Z.])", "Renderer"),
        (r"Backends(?=\.|/|$)", "Renderers"),
    ],
    "b1": [
        (r"modules/renderers/dx([1-8])/", r"modules/renderers/directx\1/"),
        (r"modules/renderers/d3d(9|10|11|12)/", r"modules/renderers/directx\1/"),
        (r"/Renderers/Dx([1-8])/", r"/Renderers/DirectX\1/"),
        (r"/Renderers/D3D(9|10|11|12)/", r"/Renderers/DirectX\1/"),
        (r"Dx([1-8])(?=[A-Z])", r"DirectX\1"),
        (r"D3D(9|10|11|12)Renderer\.(hpp|cpp)", r"DirectX\1Renderer.\2"),
        (r"examples/dx([1-8])_", r"examples/directx\1_"),
        (r"examples/d3d(9|10|11|12)_", r"examples/directx\1_"),
        (r"cmake/Tests/Dx([1-8])Tests\.cmake", r"cmake/Tests/DirectX\1Tests.cmake"),
        (r"cmake/Tests/D3D(9|10|11|12)Tests\.cmake", r"cmake/Tests/DirectX\1Tests.cmake"),
        (r"scripts/run-wine-dx([1-8])\.sh", r"scripts/run-wine-directx\1.sh"),
        (r"scripts/run-wine-d3d10\.sh", "scripts/run-wine-directx10.sh"),
        (r"scripts/check-dx([1-8])-", r"scripts/check-directx\1-"),
        (r"scripts/check-d3d10-", "scripts/check-directx10-"),
        (r"docs/dx([1-8])-renderer\.md", r"docs/directx\1-renderer.md"),
        (r"docs/d3d(9|10|11|12)-renderer\.md", r"docs/directx\1-renderer.md"),
    ],
    "b2": [],
    "c": [
        (r"tests/modules/probe_noxna\.cpp", "tests/modules/probe_cnaext.cpp"),
        (r"examples/noxna_settings_example\.cpp", "examples/cnaext_settings_example.cpp"),
        (r"^NOXNA\.md$", "CNAEXT.md"),
        (r"modules/input/src/NoXna/", "modules/input/src/CnaExt/"),
    ],
}


def rules_for(passname, path):
    compound = any(path.startswith(p + "/") for p in COMPOUND_PATHSPECS) \
        or DEMOTE_TO_COMPOUND.match(path)
    if passname == "a":
        return RULES_A_COMPOUND if compound else RULES_A
    if passname == "b1":
        return RULES_B1
    if passname == "b2":
        return RULES_B2
    if passname == "c":
        return RULES_C
    raise SystemExit(f"unknown pass {passname}")


def transform(passname, path, text, hits, tokens):
    store = {}
    text = protect(text, store)
    for pat, repl in rules_for(passname, path):
        def cap(m, pat=pat, repl=repl):
            new = m.expand(repl) if "\\" in repl else repl
            hits[(pat, repl)] += 1
            tokens[(m.group(0), new)] += 1
            return new
        text = re.sub(pat, cap, text)
    if passname == "b1" and QUOTED_FILES.match(path):
        lines = text.split("\n")
        for k, line in enumerate(lines):
            if SOKOL_GUARD.search(line):
                continue
            for pat, repl in RULES_B1_QUOTED:
                def cap(m, pat=pat, repl=repl):
                    new = m.expand(repl)
                    hits[(pat, repl)] += 1
                    tokens[(m.group(0), new)] += 1
                    return new
                line = re.sub(pat, cap, line)
            lines[k] = line
        text = "\n".join(lines)
    return restore(text, store)


def new_path(passname, path):
    out = path
    for pat, repl in PATH_RULES[passname]:
        out = re.sub(pat, repl, out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pass", dest="passname", required=True,
                    choices=["a", "b1", "b2", "c"])
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    files = tracked(FULL_PATHSPECS + COMPOUND_PATHSPECS)
    # this campaign's own evidence tree stays out of scope
    files = [p for p in files if not p.startswith("modularization/")]

    hits, tokens = Counter(), Counter()
    changed = 0
    for p in files:
        if p.endswith(BINARY_EXT):
            continue
        full = os.path.join(REPO, p)
        try:
            with open(full, encoding="utf-8", errors="surrogateescape") as f:
                text = f.read()
        except (OSError, UnicodeError):
            continue
        if "\0" in text[:8192]:
            continue
        out = transform(a.passname, p, text, hits, tokens)
        if out != text:
            changed += 1
            if not a.dry_run:
                with open(full, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(out)

    moves = []
    for p in files:
        if a.passname == "a" and (
                any(p.startswith(c + "/") for c in COMPOUND_PATHSPECS)
                or DEMOTE_TO_COMPOUND.match(p)):
            continue  # other subsystems keep their Backend-named files
        np = new_path(a.passname, p)
        if np != p:
            moves.append((p, np))
    if not a.dry_run:
        for old, new in moves:
            os.makedirs(os.path.join(REPO, os.path.dirname(new)), exist_ok=True)
            subprocess.run(["git", "mv", old, new], cwd=REPO, check=True)

    os.makedirs(MAPS, exist_ok=True)
    tag = "dry-" + a.passname if a.dry_run else a.passname
    with open(os.path.join(MAPS, f"{tag}-rule-hits.tsv"), "w") as f:
        for (pat, repl), n in sorted(hits.items(), key=lambda kv: -kv[1]):
            f.write(f"{pat}\t{repl}\t{n}\n")
    with open(os.path.join(MAPS, f"{tag}-token-map.tsv"), "w") as f:
        for (old, new), n in sorted(tokens.items()):
            f.write(f"{old}\t{new}\t{n}\n")
    with open(os.path.join(MAPS, f"{tag}-moves.tsv"), "w") as f:
        for old, new in moves:
            f.write(f"{old}\t{new}\n")

    print(f"pass={a.passname} dry_run={a.dry_run} files_changed={changed} "
          f"moves={len(moves)} rules_hit={len(hits)} tokens={len(tokens)}")


if __name__ == "__main__":
    sys.exit(main())
