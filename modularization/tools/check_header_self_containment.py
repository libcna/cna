#!/usr/bin/env python3
"""Public-header self-containment gate for the physical module layout.

Compiles every framework-module public header (plus the backend contract headers and the
deliberately host-portable metal/glide policy headers) as a standalone TU with -fsyntax-only,
against ONLY the owning module's declared include-root closure (PUBLIC graph) plus the
third-party roots the build genuinely provides. Proves no header silently depends on the
former global include root or on accidental include order. Renderer-internal headers are
exercised by their own configurations' builds (the campaign matrix), not here.
"""
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PUBLIC = {
    "core": [],
    "math": ["core"],
    "graphics": ["math", "core"],
    "input": ["graphics", "math", "core"],
    # the accepted audio<->media/input pump cycle makes these mutually includable
    "audio": ["core", "math", "media", "graphics", "input"],
    "media": ["audio", "graphics", "input", "core", "math"],
    "content": ["graphics", "audio", "media", "math", "core", "input"],
    "storage": ["core"],
    "runtime": ["graphics", "input", "content", "audio", "media", "core", "math"],
    "devices": ["runtime", "graphics", "input", "content", "audio", "media", "core", "math"],
    "devices-ext": ["runtime", "graphics", "input", "content", "audio", "media", "core", "math"],
    "graphics-ext": ["graphics", "math", "core", "input"],
    "gamer-services": ["runtime", "storage", "graphics", "input", "content", "audio",
                       "media", "core", "math"],
    "net": ["gamer-services", "runtime", "storage", "graphics", "input", "content",
            "audio", "media", "core", "math"],
}
EXTRA_PORTABLE = [
    ("renderers/metal", ["graphics", "math", "core", "input"]),
    ("renderers/glide", ["graphics", "math", "core", "input"]),
]
THIRD_PARTY = [
    "third_party/SDL/include",
    "third_party/SDL_image/include",
    "third_party/SDL_mixer/include",
    "third_party/enet/include",
    "third_party/cgltf",
    "third_party/stb",
]
DEFINES = ["SOUND_ENABLED", "XNA5", "CNA_RENDERER_HEADLESS", "CNA_CNAEXT", "CNA_DEVICES",
           "CNA_FFMPEG_AVAILABLE"]
# Headers whose compilation needs optional external SDKs absent from the host contract.
SKIP = {
    # FFmpeg development headers are a media-module PRIVATE dependency; the public header
    # compiles only inside the module build (checked by the build matrix).
    "modules/media/include/CNA/Internal/Media/VideoDecoder.hpp",
    # The Glide ABI loader pair is Windows-only by design (<windows.h> HMODULE loading);
    # consumed only by the Windows-only GLIDE targets and the standalone ABI programs.
    "modules/renderers/glide/include/CNA/Internal/Renderers/Glide/GlideAbi.hpp",
    "modules/renderers/glide/include/CNA/Internal/Renderers/Glide/GlideAbiLoader.hpp",
}


def roots_for(mods):
    return [os.path.join(REPO, "modules", m, "include") for m in mods]


def sharp_runtime_roots():
    # Modular sharp-runtime (develop 81624983): every component owns modules/<name>/include.
    sr = os.path.abspath(os.path.join(REPO, "..", "sharp-runtime", "modules"))
    roots = []
    if os.path.isdir(sr):
        for d in sorted(os.listdir(sr)):
            p = os.path.join(sr, d, "include")
            if os.path.isdir(p):
                roots.append(p)
    return roots


def main():
    sr_roots = sharp_runtime_roots()
    if not sr_roots:
        print("FATAL: no sharp-runtime include roots found")
        return 1
    jobs = []
    for mod, deps in PUBLIC.items():
        base = os.path.join(REPO, "modules", mod, "include")
        if not os.path.isdir(base):
            continue
        incs = roots_for([mod] + deps)
        for dirpath, _, files in os.walk(base):
            for f in sorted(files):
                if f.endswith((".hpp", ".h")):
                    full = os.path.join(dirpath, f)
                    rel = os.path.relpath(full, REPO)
                    if rel in SKIP:
                        continue
                    jobs.append((rel, full, incs))
    for moddir, deps in EXTRA_PORTABLE:
        base = os.path.join(REPO, "modules", moddir, "include")
        incs = [base] + roots_for(deps)
        for dirpath, _, files in os.walk(base):
            for f in sorted(files):
                if f.endswith((".hpp", ".h")):
                    full = os.path.join(dirpath, f)
                    jobs.append((os.path.relpath(full, REPO), full, incs))

    tp = [os.path.join(REPO, t) for t in THIRD_PARTY]
    failures = []
    with tempfile.TemporaryDirectory() as td:
        tu = os.path.join(td, "probe.cpp")
        for i, (rel, full, incs) in enumerate(jobs):
            with open(tu, "w") as f:
                f.write(f'#include "{full}"\nint cna_header_probe_anchor;\n')
            cmd = (["g++", "-std=c++23", "-fsyntax-only", "-x", "c++"]
                   + [f"-I{d}" for d in incs + sr_roots + tp]
                   + [f"-D{d}" for d in DEFINES] + [tu])
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                failures.append((rel, r.stderr.strip().split("\n")[0]))
    print(f"checked {len(jobs)} public headers, {len(failures)} failures")
    for rel, err in failures[:40]:
        print(f"  FAIL {rel}: {err}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
