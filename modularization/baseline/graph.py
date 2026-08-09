#!/usr/bin/env python3
"""Mechanical include-graph derivation for CNA modularization (Phase 0).
Walks include/ and src/, clusters files by candidate module, and emits the
observed cluster->cluster include-edge matrix plus offender details."""
import os, re, sys, json, collections

ROOT = "/rv/data/development/github.com/openeggbert/cnanext"
INC = os.path.join(ROOT, "include")
SRC = os.path.join(ROOT, "src")

# Curated classification of Microsoft/Xna/Framework ROOT files (basename, no ext).
XNA_MATH = {
    "Vector2","Vector3","Vector4","Matrix","Quaternion","Plane","Ray",
    "BoundingBox","BoundingSphere","BoundingFrustum","Rectangle","Point",
    "Color","MathHelper","ContainmentType","PlaneIntersectionType",
    "Curve","CurveKey","CurveKeyCollection","CurveContinuity","CurveLoopType",
    "CurveTangent",
}

def cluster_of(rel):
    """rel: project-relative include path like Microsoft/Xna/Framework/Game.hpp"""
    p = rel.split("/")
    if p[0] == "Microsoft":
        if p[1] == "Devices":
            return "ms.devices"
        if p[1] == "Xna" and p[2] == "Framework":
            if len(p) == 4:  # root file
                base = re.sub(r"\.(hpp|h|cpp|mm)$", "", p[3])
                return "xna.math" if base in XNA_MATH else "xna.runtime"
            sub = p[3].lower()
            return "xna." + sub
        return "ms.other"
    if p[0] == "CNA":
        if len(p) == 2:
            return "cna.core"
        if p[1] == "Internal":
            if len(p) == 3:
                return "internal.root"
            if p[2] == "Backends":
                if len(p) == 4:
                    return "internal.backends.root"
                b = p[3].lower()
                return ("backends.common" if b == "common" else "backend." + b)
            return "internal." + p[2].lower()
        return "cna." + p[1].lower()
    return None

EXT_PATTERNS = [
    (re.compile(r"^(SharpRuntime|System)/"), "ext.sharp"),
    (re.compile(r"^SDL3?/|^SDL\.h|^SDL_"), "ext.sdl"),
    (re.compile(r"^vulkan/"), "ext.vulkan"),
    (re.compile(r"^(d3d\d+|dxgi|ddraw|d2d1|d3dcompiler|windows\.h|wingdi|winuser|objbase|wrl)", re.I), "ext.win32d3d"),
    (re.compile(r"^(GL|GLES\d?|GLES3|EGL|KHR)/"), "ext.gl"),
    (re.compile(r"^(gl|glext|glcorearb)\.h$"), "ext.gl"),
    (re.compile(r"^bgfx/|^bx/|^bimg/"), "ext.bgfx"),
    (re.compile(r"^webgpu|^wgpu"), "ext.webgpu"),
    (re.compile(r"^(Magnum|Corrade)/"), "ext.magnum"),
    (re.compile(r"^include/core/Sk|^Sk[A-Z]"), "ext.skia"),
    (re.compile(r"^sokol"), "ext.sokol"),
    (re.compile(r"^LLGL/"), "ext.llgl"),
    (re.compile(r"^(Graphics|Common|Platforms)/.*(Diligent|interface)", re.I), "ext.diligent"),
    (re.compile(r"^Diligent"), "ext.diligent"),
    (re.compile(r"^Wicked|^wi[A-Z]"), "ext.wicked"),
    (re.compile(r"^Metal/|^QuartzCore/|^Foundation/|^Cocoa/"), "ext.metal"),
    (re.compile(r"^enet"), "ext.enet"),
    (re.compile(r"^libav|^libsw"), "ext.ffmpeg"),
    (re.compile(r"^(cgltf|stb_)"), "ext.vendored"),
    (re.compile(r"^emscripten"), "ext.emscripten"),
    (re.compile(r"^glide\.h|^glide3|^3dfx", re.I), "ext.glide"),
    (re.compile(r"^easy_?gl|^EasyGl|^easygl", re.I), "ext.easygl"),
    (re.compile(r"^shaderc"), "ext.shaderc"),
    (re.compile(r"^draco"), "ext.draco"),
]

INCLUDE_RE = re.compile(r'^\s*#\s*(?:include|import)\s*[<"]([^">]+)[">]', re.M)

def resolve(inc, cur_dir):
    """Return ('proj', rel) or ('ext', tag) or None(std)."""
    # project path rooted at include/ or src/?
    for root in (INC, SRC):
        if os.path.exists(os.path.join(root, inc)):
            return ("proj", inc)
    # relative to current file dir?
    cand = os.path.normpath(os.path.join(cur_dir, inc))
    for root in (INC, SRC):
        if cand.startswith(root + "/") and os.path.exists(cand):
            return ("proj", os.path.relpath(cand, root))
    for pat, tag in EXT_PATTERNS:
        if pat.search(inc):
            return ("ext", tag)
    if "/" not in inc and "." not in inc:
        return None  # std header
    if inc.endswith(".h") or inc.endswith(".hpp"):
        return ("ext", "ext.other:" + inc)
    return None

files = []
for base in (INC, SRC):
    for dp, dn, fn in os.walk(base):
        for f in fn:
            if f.endswith((".hpp", ".h", ".cpp", ".mm", ".inl")):
                files.append(os.path.join(dp, f))

edges = collections.defaultdict(set)          # (srccl,dstcl) -> set of (srcfile,dst)
counts = collections.Counter()                # cluster -> file count
unresolved = collections.Counter()
for path in sorted(files):
    base = INC if path.startswith(INC) else SRC
    rel = os.path.relpath(path, base)
    cl = cluster_of(rel)
    if cl is None:
        continue
    counts[cl] += 1
    cur_dir = os.path.dirname(path)
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    for m in INCLUDE_RE.finditer(text):
        r = resolve(m.group(1), cur_dir)
        if r is None:
            continue
        kind, val = r
        if kind == "proj":
            dcl = cluster_of(val)
            if dcl and dcl != cl:
                edges[(cl, dcl)].add((rel, val))
        else:
            if val.startswith("ext.other:"):
                unresolved[val] += 1
                val = "ext.other"
            if (cl, val) != (cl, cl):
                edges[(cl, val)].add((rel, m.group(1)))

out = {"counts": dict(counts),
       "edges": {f"{a} -> {b}": sorted(f"{s} => {d}" for s, d in v)
                  for (a, b), v in sorted(edges.items())},
       "unresolved_ext": dict(unresolved.most_common(40))}
json.dump(out, open(sys.argv[1], "w"), indent=1)

# Console summary: edge matrix with counts
print("=== CLUSTER SIZES ===")
for c, n in sorted(counts.items()):
    print(f"{n:5d}  {c}")
print("\n=== EDGES (distinct file-pair counts) ===")
for (a, b), v in sorted(edges.items(), key=lambda kv: (kv[0][0], -len(kv[1]))):
    print(f"{len(v):4d}  {a} -> {b}")
