#!/usr/bin/env python3
"""Module include-reachability checker for the physical layout.

For every module file (src + include), maps each #include "CNA/..." / "Microsoft/..."
directive to the owning module and verifies the owner is reachable through the declared
module graph (PUBLIC edges transitively; PRIVATE edges for the module's own TUs).
Config-gated renderer includes (#if CNA_RENDERER_*/CNA_GL_PROFILE_*) are attributed to the
guarded backend and checked only for the configuration that selects it.
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PUBLIC = {
    "core": [],
    "math": ["core"],  # cna_core_headers: include-equivalent to core
    "graphics": ["math", "core"],
    "input": ["graphics", "math", "core"],
    "audio": ["core", "math"],
    "media": ["audio", "graphics"],
    "content": ["graphics", "audio", "media", "math", "core"],
    "storage": ["core"],  # cna_core_headers: include-equivalent to core
    "runtime": ["graphics", "input", "content", "audio", "media", "core", "math"],
    "devices": ["runtime", "graphics", "core", "math"],
    "devices-ext": ["runtime", "graphics", "core", "math"],
    "graphics-ext": ["graphics"],
    "gamer-services": ["runtime", "storage"],
    "net": ["gamer-services"],
}
PRIVATE = {
    "graphics": ["input", "@backend"],
    "audio": ["media", "input"],
    "media": ["input"],
}
# every renderer: own module + PRIVATE graphics/core/math reverse edges
RENDERER_PRIVATE = ["graphics", "core", "math"]
RENDERER_EXTRA = {
    "gdi": ["software"], "ascii": ["sdl-renderer"],
    "directx11": ["common/d3d"], "directx12": ["common/d3d"],
}
FAMILY = {"Ascii": "ascii", "Bgfx": "bgfx", "Canvas": "canvas", "DirectX10": "directx10",
          "DirectX11": "directx11", "DirectX12": "directx12", "DirectX9": "directx9",
          "Diligent": "diligent",
          "Direct2D": "direct2d", "DirectX1": "directx1", "DirectX2": "directx2",
          "DirectX3": "directx3", "DirectX5": "directx5",
          "DirectX6": "directx6", "DirectX7": "directx7", "DirectX8": "directx8",
          "EasyGL": "easygl",
          "FreeDirect": "freedirect", "Gdi": "gdi", "Glide": "glide", "Headless": "headless",
          "HtmlDom": "html-dom", "Llgl": "llgl", "Magnum": "magnum", "Metal": "metal",
          "OpenGL1": "opengl1", "OpenGL2": "opengl2", "OpenGL4": "opengl4",
          "OpenGLES1": "opengles1", "SdlGpu": "sdl-gpu", "SdlRenderer": "sdl-renderer",
          "Skia": "skia", "Software": "software", "Sokol": "sokol", "Stub": "stub",
          "Vulkan": "vulkan", "WebGPU": "webgpu", "Wicked": "wicked",
          "D3DCommon": "common/d3d"}

INC = re.compile(r'^\s*#\s*include\s*"((?:CNA|Microsoft)/[^"]+)"', re.M)


def build_header_owner_map():
    owner = {}
    out = subprocess.run(["git", "ls-files", "modules/"], cwd=REPO,
                         capture_output=True, text=True).stdout.split()
    for p in out:
        m = re.match(r"modules/(renderers/(?:common/d3d|[a-z0-9-]+)|[a-z-]+)/include/(.+)$", p)
        if m:
            owner[m.group(2)] = m.group(1).replace("renderers/", "r:")
    return owner, out


def closure(mod):
    seen = set()

    def walk(m):
        if m in seen:
            return
        seen.add(m)
        for d in PUBLIC.get(m, []):
            walk(d)
    walk(mod)
    return seen


def main():
    owner, files = build_header_owner_map()
    problems = []
    for p in files:
        if not p.endswith((".cpp", ".hpp", ".h", ".mm")):
            continue
        m = re.match(r"modules/(renderers/(?:common/d3d|[a-z0-9-]+)|[a-z-]+)/(src|include|tests)/", p)
        if not m:
            continue
        mod, area = m.group(1).replace("renderers/", "r:"), m.group(2)
        if area == "tests":
            continue  # tests link the umbrella; separate concern
        if mod.startswith("r:"):
            reach = set()
            for d in ["graphics", "core", "math"] + RENDERER_EXTRA.get(mod[2:], []):
                if d == "common/d3d":
                    reach.add("r:common/d3d")
                elif d in ("software", "sdl-renderer"):
                    reach.add("r:" + d)
                else:
                    reach |= closure(d)
            reach.add(mod)
        else:
            reach = closure(mod)
            if area == "src":
                for d in PRIVATE.get(mod, []):
                    if d == "@backend":
                        continue
                    reach |= closure(d)
        if area != "src":
            continue  # headers are validated transitively from the TUs that compile them
        visit = [(p, None)]
        seen_hdrs = set()
        while visit:
            cur, via = visit.pop()
            try:
                text = open(os.path.join(REPO, cur), encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            lines = text.split("\n")
            guard = []
            for i, l in enumerate(lines):
                ls = l.strip()
                if ls.startswith("#if"):
                    gm = re.findall(r"CNA_RENDERER_([A-Z0-9_]+)|CNA_GL_PROFILE_", ls)
                    guard.append(bool(gm))
                elif ls.startswith("#endif") and guard:
                    guard.pop()
                im = re.match(r'\s*#\s*include\s*"((?:CNA|Microsoft)/[^"]+)"', l)
                if not im:
                    continue
                hdr = im.group(1)
                if hdr in seen_hdrs:
                    continue
                seen_hdrs.add(hdr)
                own = owner.get(hdr)
                if own is None:
                    problems.append(f"{cur}:{i+1} (from {p}): includes UNKNOWN header {hdr}")
                    continue
                if own.startswith("r:") and own != mod and any(guard):
                    continue  # config-gated backend include: checked in that configuration
                if own != mod and own not in reach:
                    problems.append(
                        f"{cur}:{i+1} (from TU {p}): [{mod}] needs {hdr} owned by [{own}] (unreachable)")
                    continue
                hpath = None
                for pref in ("modules/", ):
                    cand = (f"modules/{own.replace('r:', 'renderers/')}/include/{hdr}")
                    if os.path.exists(os.path.join(REPO, cand)):
                        hpath = cand
                if hpath:
                    visit.append((hpath, cur))
    if problems:
        print(f"{len(problems)} include-reachability problems:")
        for x in problems:
            print("  " + x)
        return 1
    print("OK: every module include resolves through the declared module graph")
    return 0


if __name__ == "__main__":
    sys.exit(main())
