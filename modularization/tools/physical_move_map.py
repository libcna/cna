#!/usr/bin/env python3
"""Move-map generator for the Phase-3 physical module layout.

Emits modularization/physical-modules/move-map.tsv (old<TAB>new<TAB>group) covering every
tracked file under include/, src/ and tests/ (minus the deliberate stay-put areas), and
verifies totality/uniqueness before anything moves. Deterministic; no filesystem writes
besides the map.
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FAMILY = {  # Backends/<X> directory -> modules/renderers/<family>
    "Ascii": "ascii", "Bgfx": "bgfx", "Canvas": "canvas", "D3D10": "d3d10",
    "D3D11": "d3d11", "D3D12": "d3d12", "D3D9": "d3d9", "Diligent": "diligent",
    "Direct2D": "direct2d", "Dx1": "dx1", "Dx2": "dx2", "Dx3": "dx3", "Dx5": "dx5",
    "Dx6": "dx6", "Dx7": "dx7", "Dx8": "dx8", "EasyGL": "easygl",
    "FreeDirect": "freedirect", "Gdi": "gdi", "Glide": "glide", "Headless": "headless",
    "HtmlDom": "html-dom", "Llgl": "llgl", "Magnum": "magnum", "Metal": "metal",
    "OpenGL1": "opengl1", "OpenGL2": "opengl2", "OpenGL4": "opengl4",
    "OpenGLES1": "opengles1", "SdlGpu": "sdl-gpu", "SdlRenderer": "sdl-renderer",
    "Skia": "skia", "Software": "software", "Sokol": "sokol", "Stub": "stub",
    "Vulkan": "vulkan", "WebGPU": "webgpu", "Wicked": "wicked",
}

MATH_ROOT = {
    "BoundingBox", "BoundingFrustum", "BoundingSphere", "Color", "ContainmentType",
    "Curve", "CurveContinuity", "CurveKey", "CurveKeyCollection", "CurveLoopType",
    "CurveTangent", "MathHelper", "Matrix", "Plane", "PlaneIntersectionType", "Point",
    "Quaternion", "Ray", "Rectangle", "Vector2", "Vector3", "Vector4",
}
RUNTIME_ROOT = {
    "DrawableGameComponent", "ExitingEventArgs", "Game", "GameComponent",
    "GameComponentCollection", "GameComponentCollectionEventArgs", "GameServiceContainer",
    "GameTime", "GameWindow", "GraphicsDeviceInformation", "GraphicsDeviceManager",
    "IDrawable", "IGameComponent", "IGraphicsDeviceManager", "IUpdateable",
    "LaunchParameters", "PreparingDeviceSettingsEventArgs", "TitleContainer",
    "TitleLocation",
}
CORE_ROOT = {"PlayerIndex", "NamespaceDocs"}
GRAPHICS_ROOT = {"DisplayOrientation"}
AUDIO_ROOT = {"FrameworkDispatcher"}

CNA_ROOT = {  # include/CNA/<name>.hpp -> module
    "CNAException": "core", "CNAHelper": "core", "DesktopOS": "core",
    "Entrypoint": "core", "LogCategory": "core", "Logger": "core", "LogLevel": "core",
    "Platform": "core", "Misc": "runtime", "GraphicsBackendType": "core",
    "GraphicsCapability": "graphics", "Unsupported3DGraphicsCallBehavior": "graphics",
}
CNA_INTERNAL_ROOT = {  # include/CNA/Internal/<name>.hpp -> module
    "CnjEnvelope": "content", "CnjSourceFile": "content", "Json": "content",
    "PathContainment": "core", "Utf8Decode": "graphics",
}
INTERNAL_AREA = {  # include/CNA/Internal/<Area>/ and tests/CNA/Internal/<Area>/
    "Audio": "audio", "Graphics": "graphics", "Input": "input", "Media": "media",
    "Net": "net", "Xnb": "content", "GltfImport": "content",
    "GamerServices": "gamer-services",
}
FRAMEWORK_SUB = {  # Microsoft/Xna/Framework/<Sub>/ -> module
    "Audio": "audio", "Content": "content", "GamerServices": "gamer-services",
    "Graphics": "graphics", "Input": "input", "Media": "media", "Net": "net",
    "Storage": "storage",
}
# tests/Microsoft/Xna/Framework/<file>.cpp -> module (framework-root test classification)
ROOT_TEST = {}
for n in MATH_ROOT: ROOT_TEST[n] = "math"
for n in RUNTIME_ROOT: ROOT_TEST[n] = "runtime"
ROOT_TEST.update({"PlayerIndex": "core", "DisplayOrientation": "graphics",
                  "GraphicsBackendCompileDefinition": "graphics",
                  "FrameworkDispatcher": "audio", "StorageDevice": "storage"})
CNA_TEST_ROOT = {"LoggerTests.cpp": "core", "GraphicsBackendTypeTests.cpp": "graphics"}
CNA_INTERNAL_TEST_ROOT = {"PathContainmentTests.cpp": "content_or_core"}  # resolved below


BASE = "ea61123e6fda52150a522af8db30023edf4ba1d2"


def tracked(prefix):
    out = subprocess.run(["git", "ls-tree", "-r", "-z", "--name-only", BASE, "--", prefix],
                         cwd=REPO, check=True, capture_output=True).stdout
    return sorted(p.decode() for p in out.split(b"\0") if p)


def module_root(m):
    return f"modules/{m}"


def map_include(p):
    rest = p[len("include/"):]
    parts = rest.split("/")
    if rest.startswith("Microsoft/Xna/Framework/"):
        tail = rest[len("Microsoft/Xna/Framework/"):]
        if tail == "Graphics/PackedVector/IPackedVector.hpp":
            # Interface header consumed by math's Color (and by every PackedVector type);
            # ownership follows dependency direction -- include spelling is unchanged.
            return f"modules/math/include/{rest}"
        if "/" in tail:
            sub = tail.split("/")[0]
            m = FRAMEWORK_SUB.get(sub)
            if not m:
                return None
            return f"modules/{m}/include/{rest}"
        base = tail[:-len(".hpp")]
        for names, m in ((MATH_ROOT, "math"), (RUNTIME_ROOT, "runtime"),
                         (CORE_ROOT, "core"), (GRAPHICS_ROOT, "graphics"),
                         (AUDIO_ROOT, "audio")):
            if base in names:
                return f"modules/{m}/include/{rest}"
        return None
    if rest.startswith("Microsoft/Devices/"):
        return f"modules/devices/include/{rest}"
    if rest.startswith("CNA/Internal/Backends/Common/"):
        return f"modules/graphics/include/{rest}"
    if rest.startswith("CNA/Internal/Backends/"):
        x = parts[3]
        fam = FAMILY.get(x)
        if x == "D3DCommon":
            return f"modules/renderers/common/d3d/include/{rest}"
        if not fam:
            return None
        return f"modules/renderers/{fam}/include/{rest}"
    if rest.startswith("CNA/Internal/"):
        tail = rest[len("CNA/Internal/"):]
        if "/" in tail:
            m = INTERNAL_AREA.get(tail.split("/")[0])
            if not m:
                return None
            return f"modules/{m}/include/{rest}"
        m = CNA_INTERNAL_ROOT.get(tail[:-len(".hpp")])
        if not m:
            return None
        return f"modules/{m}/include/{rest}"
    if rest.startswith("CNA/Devices/"):
        return f"modules/devices-ext/include/{rest}"
    if rest.startswith("CNA/Graphics/"):
        return f"modules/graphics-ext/include/{rest}"
    if rest.startswith("CNA/Input/"):
        return f"modules/input/include/{rest}"
    if rest.startswith("CNA/") and rest.count("/") == 1:
        m = CNA_ROOT.get(parts[1][:-len(".hpp")])
        if not m:
            return None
        return f"modules/{m}/include/{rest}"
    return None


def map_src(p):
    rest = p[len("src/"):]
    parts = rest.split("/")
    top = parts[0]
    if top == "Graphics":
        if parts[1] == "Backends":
            x = parts[2]
            tail = "/".join(parts[3:])
            if x == "D3DCommon":
                return f"modules/renderers/common/d3d/src/{tail}"
            fam = FAMILY.get(x)
            if not fam:
                return None
            if x == "D3D9" and tail == "shaders/D3D9ShaderRegisters.hpp":
                # Referenced from the module's include-tree headers via the public-internal
                # CNA/Internal/... spelling (Phase-2 latent defect repair) -- it belongs to the
                # module's include tree.
                return ("modules/renderers/d3d9/include/CNA/Internal/Backends/D3D9/"
                        "shaders/D3D9ShaderRegisters.hpp")
            return f"modules/renderers/{fam}/src/{tail}"
        return f"modules/graphics/src/{'/'.join(parts[1:])}"
    if top == "Devices":
        if parts[1] == "Microsoft":
            return f"modules/devices/src/{'/'.join(parts[2:])}"
        if parts[1] == "NoXna":
            return f"modules/devices-ext/src/{'/'.join(parts[2:])}"
        return None
    if top == "NoXna":
        if parts[1] == "Graphics":
            return f"modules/graphics-ext/src/{'/'.join(parts[2:])}"
        return None
    simple = {"Audio": "audio", "Content": "content", "Core": "core", "Input": "input",
              "Math": "math", "Media": "media", "Runtime": "runtime",
              "Storage": "storage", "Net": "net", "GamerServices": "gamer-services"}
    m = simple.get(top)
    if not m:
        return None
    return f"modules/{m}/src/{'/'.join(parts[1:])}"


def map_test(p):
    rest = p[len("tests/"):]
    parts = rest.split("/")
    if rest.startswith("assets/") or rest.startswith("modules/"):
        return p  # stays
    if rest == "PackedVectorGolden.md":
        return "modules/graphics/tests/PackedVectorGolden.md"
    if rest.startswith("opengl1/"):
        return f"modules/renderers/opengl1/tests/{'/'.join(parts[1:])}"
    if rest.startswith("Microsoft/Xna/Framework/"):
        tail = rest[len("Microsoft/Xna/Framework/"):]
        if "/" in tail:
            m = FRAMEWORK_SUB.get(tail.split("/")[0])
            if not m:
                return None
            return f"modules/{m}/tests/{rest}"
        stem = tail[:-len("Tests.cpp")] if tail.endswith("Tests.cpp") else None
        m = ROOT_TEST.get(stem) if stem else None
        if not m:
            return None
        return f"modules/{m}/tests/{rest}"
    if rest.startswith("Microsoft/Devices/"):
        return f"modules/devices/tests/{rest}"
    if rest.startswith("CNA/Internal/Backends/"):
        x = parts[3]
        fam = FAMILY.get(x)
        if not fam:
            return None
        return f"modules/renderers/{fam}/tests/{rest}"
    if rest.startswith("CNA/Internal/"):
        tail = rest[len("CNA/Internal/"):]
        if "/" in tail:
            m = INTERNAL_AREA.get(tail.split("/")[0])
            if not m:
                return None
            return f"modules/{m}/tests/{rest}"
        m = {"PathContainmentTests.cpp": "core", "CnjEnvelopeTests.cpp": "content",
             "JsonTests.cpp": "content"}.get(tail)
        if not m:
            return None
        return f"modules/{m}/tests/{rest}"
    if rest.startswith("CNA/Devices/"):
        return f"modules/devices-ext/tests/{rest}"
    if rest.startswith("CNA/Graphics/"):
        return f"modules/graphics-ext/tests/{rest}"
    if rest.startswith("CNA/Input/"):
        return f"modules/input/tests/{rest}"
    if rest.startswith("CNA/") and rest.count("/") == 1:
        m = CNA_TEST_ROOT.get(parts[1])
        if not m:
            return None
        return f"modules/{m}/tests/{rest}"
    return None


def group_of(new):
    if new.startswith("modules/renderers/"):
        return "renderers"
    m = new.split("/")[1]
    return {
        "core": "core-math-runtime", "math": "core-math-runtime",
        "runtime": "core-math-runtime",
        "graphics": "graphics-input", "input": "graphics-input",
        "audio": "audio-media", "media": "audio-media",
        "content": "content-storage", "storage": "content-storage",
        "devices": "devices-ext-split", "devices-ext": "devices-ext-split",
        "graphics-ext": "devices-ext-split",
        "net": "net-gamerservices", "gamer-services": "net-gamerservices",
    }.get(m, "other")


def main():
    rows = []
    problems = []
    for p in tracked("include/"):
        n = map_include(p)
        (rows.append((p, n)) if n else problems.append(p))
    for p in tracked("src/"):
        n = map_src(p)
        (rows.append((p, n)) if n else problems.append(p))
    stays = 0
    for p in tracked("tests/"):
        n = map_test(p)
        if n == p:
            stays += 1
            continue
        (rows.append((p, n)) if n else problems.append(p))
    if problems:
        print("UNMAPPED:")
        for p in problems:
            print("  " + p)
        return 1
    news = [n for _, n in rows]
    if len(set(news)) != len(news):
        dupes = sorted(n for n in set(news) if news.count(n) > 1)
        print("DUPLICATE DESTINATIONS:", dupes)
        return 1
    out = os.path.join(REPO, "modularization/physical-modules/move-map.tsv")
    with open(out, "w") as f:
        for old, new in sorted(rows):
            f.write(f"{old}\t{new}\t{group_of(new)}\n")
    per = {}
    for _, n in rows:
        per[group_of(n)] = per.get(group_of(n), 0) + 1
    print(f"mapped {len(rows)} moves (+{stays} stay-put tests) -> {out}")
    for g in sorted(per):
        print(f"  {g}: {per[g]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
