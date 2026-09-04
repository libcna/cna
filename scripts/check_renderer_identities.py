#!/usr/bin/env python3
"""Renderer-identity registry gate (plans/MODULARIZATION_PLAN.md §2.3).

CNA has exactly 44 public renderer identities. This check mechanically compares
the authoritative registries -- the public GraphicsRendererType enum, the
CNA_GRAPHICS_RENDERER cmake selection list, and the runtime renderer registry --
against the canonical identity table below. Any addition, removal or rename of a
public identity fails here until the table (and therefore the documented public
count) is deliberately updated.

The RUNTIME registry (cmake/RendererRegistry.cmake plus each family's descriptor
translation unit) is checked because being a valid identity in the first two
lists says nothing about whether a build can actually instantiate the renderer.
PIXIJS was added to the enum and the cmake STRINGS list, and this check reported
all 49 identities as fine, while cna_renderer_identity_to_namespace("PIXIJS")
was a hard configure error -- so `-DCNA_GRAPHICS_RENDERER=PIXIJS` could not be
configured at all. A list that only says a name is spelled the same in two places
cannot catch that; check_runtime_registry() below closes it by following each
identity through to the C++ accessor that is supposed to return its descriptor.

It also checks the DOCUMENTED count, in the handful of documents that state one
(plans/plan_runtimerenderer.md RTR-P13-8). A count written into prose is a fact with no
owner: TINYGL, IGL and PIXIJS were each added without it, so documents went on
saying 46 and 47 while the registry said 49, and a reader has no way to tell which
number is the live one. Correcting them by hand does not hold either -- the pass
that fixed four such documents still left three wrong, which is what this check
was written to stop. Prefer not stating a number at all; where a document really
wants one, this keeps it true.

Exit codes: 0 ok, 1 mismatch.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Documents that may state a whole-registry count, and whether one must actually be VISIBLE to the
# patterns below. Each entry is a decision: a document is better off describing the registry than
# restating its size, so adding here should be rarer than removing.
#
# The True/False matters more than it looks, and exists because tightening the patterns created a
# new way to be silently wrong. They are deliberately narrow -- "public ... identities",
# "implementation families" -- so that a legitimate sub-count ("11 renderer families need it",
# "SDL renderer/GPU (5 families)") is not reported as a stale total. The cost of that precision is
# recall: a count phrased any other way is not checked at all, and a check that cannot see a count
# leaves it exactly as unowned as one nobody wrote down. That is not hypothetical -- the first
# attempt at plans/plan_platform.md's rule 5 wrote "is **49** today" and this check sailed straight past
# it.
#
# So a document marked True must keep stating its count in the canonical phrasing. Rewording it
# into something this check cannot see is itself a failure, reported as such.
COUNTED_DOCUMENTS = {
    "docs/runtime-renderer-selection.md": True,
    "docs/renderer-expansion-candidates.md": True,
    "docs/physical-modules.md": True,
    # plans/plan_platform.md states the count in three load-bearing places -- rule 5 ("no task may
    # reduce renderer coverage"), PLAT-76's allowlist evidence and the definition of done -- and
    # every one of them read "46" for three identities after the count moved. It is the document
    # a reviewer checks the boundary against, so a stale number there is worse than elsewhere.
    "plans/plan_platform.md": True,
    # These two deliberately state no total: both used to, and both now name the registry instead,
    # which is the outcome this check recommends in its own failure message. Listed rather than
    # dropped so that a count reappearing in them is still checked.
    "AUDIT.md": False,
    "CHECKLIST.md": False,
}

# "44 public renderer identities", "45 families / 44 public identities", "45 implementation
# families". Deliberately anchored on the words that mean the WHOLE registry: a legitimate
# sub-count ("the five GL identities of the easygl family") does not match, because it never
# says "public".
#
# The leading \b is load-bearing rather than decorative: without it the family pattern read the
# "12" out of "the d3d11 and d3d12 families only" and reported the repo as having 12 renderer
# families. Found by running this against the clean tree before trusting it, which is the only
# reason it is not still there.
#
# The family pattern needs a whole-registry marker for the same reason the identity pattern needs
# "public", and "renderer families" is NOT one -- it is how sub-counts are phrased too. Adding
# plans/plan_platform.md produced three false positives in one run, each a true statement:
#
#   "SDL renderer/GPU (5 families)"                 the families touching a native window
#   "(11 renderer families need it)"                the families needing IPlatformGlContext
#   "nine across the four PLAT-76 renderer families"  the allowlisted four
#
# So the marker is "implementation families", the phrase already used for the real claim. The last
# example also exposed a second defect: `\b(\d+)` happily captured the 76 of "PLAT-76", because \b
# matches after a hyphen. Same shape as the "d3d12" near-miss above, one lookbehind away from
# reporting the repository as having 76 renderer families. A count is only checkable if the words
# around it say it is a count of everything.
IDENTITY_COUNT = re.compile(r"(?<![-\w])(\d+)\s+public\s+(?:renderer\s+)?identities")
FAMILY_COUNT = re.compile(r"(?<![-\w])(\d+)\s+implementation\s+families\b")

# Canonical public identities: (cmake selection name, enum name). 49 entries.
IDENTITIES = [
    ("SDL_RENDERER", "SdlRenderer"),
    ("OPENGLES2", "OpenGLES2"),
    ("OPENGLES3", "OpenGLES3"),
    ("OPENGL33", "OpenGL33"),
    ("WEBGL1", "WebGL1"),
    ("WEBGL2", "WebGL2"),
    ("BGFX", "Bgfx"),
    ("VULKAN", "Vulkan"),
    ("WEBGPU", "WebGPU"),
    ("MAGNUM", "Magnum"),
    ("HEADLESS", "Headless"),
    ("SOFTWARE", "Software"),
    ("STUB", "Stub"),
    ("DIRECTX11", "DirectX11"),
    ("DIRECTX12", "DirectX12"),
    ("DIRECT2D", "Direct2D"),
    ("CANVAS", "Canvas"),
    ("HTML_DOM", "HtmlDom"),
    ("BLEND2D", "Blend2D"),
    ("FREEDIRECT", "FreeDirect"),
    ("DIRECTX9", "DirectX9"),
    ("DIRECTX1", "DirectX1"),
    ("DIRECTX2", "DirectX2"),
    ("DIRECTX3", "DirectX3"),
    ("DIRECTX5", "DirectX5"),
    ("DIRECTX6", "DirectX6"),
    ("DIRECTX7", "DirectX7"),
    ("DIRECTX8", "DirectX8"),
    ("DIRECTX10", "DirectX10"),
    ("SDL_GPU", "SdlGpu"),
    ("OPENGLES1", "OpenGLES1"),
    ("OPENGL4", "OpenGL4"),
    ("OPENGL1", "OpenGL1"),
    ("OPENGL2", "OpenGL2"),
    ("GLIDE", "Glide"),
    ("GDI", "Gdi"),
    ("METAL", "Metal"),
    ("FNA3D", "Fna3d"),
    ("SVG_DOM", "SvgDom"),
    ("OPENVG", "OpenVg"),
    ("PORTABLEGL", "PortableGL"),
    ("TINYGL", "TinyGL"),
    ("PIXIJS", "PixiJs"),
    ("NANOVG", "NanoVg"),
]


def enum_identities():
    path = os.path.join(REPO, "modules", "core", "include", "CNA", "GraphicsRendererType.hpp")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"enum class GraphicsRendererType\s*\{(.*?)\n\s*\};", text, re.S)
    if not body:
        sys.exit("cannot locate enum class GraphicsRendererType")
    stripped = re.sub(r"/\*.*?\*/|//[^\n]*", "", body.group(1), flags=re.S)
    return re.findall(r"\b([A-Za-z_]\w*)\b", stripped)


def cmake_identities():
    path = os.path.join(REPO, "cmake", "RendererSelection.cmake")
    text = open(path, encoding="utf-8").read()
    m = re.search(
        r"set_property\(CACHE CNA_GRAPHICS_RENDERER PROPERTY STRINGS((?:\s+\"[A-Z0-9_]+\")+)\)",
        text)
    if not m:
        sys.exit("cannot locate CNA_GRAPHICS_RENDERER STRINGS property")
    return re.findall(r"\"([A-Z0-9_]+)\"", m.group(1))


def family_count():
    """Renderer implementation families, counted the same way the discipline gate counts them."""
    renderers = os.path.join(REPO, "modules", "renderers")
    return sum(1 for name in os.listdir(renderers)
               if os.path.isdir(os.path.join(renderers, name, "src")))


def registry_map():
    """The identity -> "<namespace>[|<accessor>]" map from cmake/RendererRegistry.cmake."""
    path = os.path.join(REPO, "cmake", "RendererRegistry.cmake")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"set\(_map\n(.*?)\)\n", text, re.S)
    if not body:
        sys.exit("cannot locate the _map set() in cmake/RendererRegistry.cmake")
    stripped = re.sub(r"#[^\n]*", "", body.group(1))
    tokens = stripped.split()
    if len(tokens) % 2 != 0:
        sys.exit("cmake/RendererRegistry.cmake's _map has an odd number of tokens")
    return dict(zip(tokens[0::2], tokens[1::2]))


def renderer_sources():
    """Every production translation unit under modules/renderers/<family>/src, read once."""
    root = os.path.join(REPO, "modules", "renderers")
    sources = {}
    for family in sorted(os.listdir(root)):
        src = os.path.join(root, family, "src")
        if not os.path.isdir(src):
            continue
        for base, _dirs, names in os.walk(src):
            for name in names:
                if name.endswith((".cpp", ".mm")):
                    path = os.path.join(base, name)
                    sources[path] = open(path, encoding="utf-8", errors="replace").read()
    return sources


def check_runtime_registry(identities):
    """Follows every identity into the runtime registry and the C++ that has to back it.

    Three things have to line up before a build can instantiate a renderer at runtime, and each
    has been wrong independently in this repo's history:

      1. cmake/RendererRegistry.cmake maps the identity to an implementing namespace. Missing, this
         is a FATAL_ERROR at configure time for anyone selecting that identity.
      2. Some renderer translation unit DEFINES that namespace's descriptor accessor. Without it
         the generated registry names a symbol nothing provides and the link fails.
      3. The same namespace declares its own CreateGraphicsRenderer. plans/plan_runtimerenderer.md
         design decision 4 moved the factory out of the shared CNA::Internal::Renderers namespace
         precisely so several renderer archives can link into one binary; a family left behind in
         the shared namespace both fails to satisfy its own descriptor and collides with every
         other such family in a multi-renderer build.
    """
    problems = []
    mapping = registry_map()
    sources = renderer_sources()

    for cmake_name, _enum_name in identities:
        entry = mapping.get(cmake_name)
        if entry is None:
            problems.append(
                f"{cmake_name}: cmake/RendererRegistry.cmake has no implementing namespace. "
                f"Configuring -DCNA_GRAPHICS_RENDERER={cmake_name} is a hard CMake error until it "
                f"is registered there, whatever the enum and the STRINGS list say.")
            continue
        namespace, _, accessor = entry.partition("|")
        accessor = accessor or "GetDescriptor"

        definition = re.compile(
            r"const\s+GraphicsRendererDescriptor\s*&\s*" + re.escape(accessor) + r"\s*\(\s*\)")
        factory = re.compile(
            r"std::unique_ptr\s*<\s*IGraphicsRenderer\s*>\s*CreateGraphicsRenderer\s*\(")
        namespace_open = re.compile(
            r"namespace\s+CNA::Internal::Renderers::" + re.escape(namespace) + r"\b")

        in_namespace = [text for text in sources.values() if namespace_open.search(text)]
        if not in_namespace:
            problems.append(
                f"{cmake_name}: cmake/RendererRegistry.cmake maps it to "
                f"CNA::Internal::Renderers::{namespace}, but no renderer translation unit opens "
                f"that namespace.")
            continue
        if not any(definition.search(text) for text in in_namespace):
            problems.append(
                f"{cmake_name}: no translation unit in CNA::Internal::Renderers::{namespace} "
                f"defines {accessor}(), which the generated registry calls.")
        if not any(factory.search(text) for text in in_namespace):
            problems.append(
                f"{cmake_name}: CNA::Internal::Renderers::{namespace} has no family-scoped "
                f"CreateGraphicsRenderer. plans/plan_runtimerenderer.md design decision 4 requires the "
                f"factory to live in the family's own namespace, not the shared one.")

    for cmake_name in sorted(set(mapping) - {c for c, _ in identities}):
        problems.append(
            f"{cmake_name}: cmake/RendererRegistry.cmake maps an identity that is not in the "
            f"canonical table. Remove it, or add it to IDENTITIES here and to both registries.")
    return problems


def documented_counts(identities, families):
    """Reports every stated count that disagrees with the registry, and every one gone invisible."""
    problems = []
    for relative, must_be_visible in COUNTED_DOCUMENTS.items():
        path = os.path.join(REPO, relative)
        if not os.path.exists(path):
            continue
        seen = 0
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                for match in IDENTITY_COUNT.finditer(line):
                    seen += 1
                    if int(match.group(1)) != identities:
                        problems.append(
                            f"{relative}:{number}: says {match.group(1)} public renderer "
                            f"identities; there are {identities}. Either correct it, or -- better "
                            f"-- drop the number and name the registry, so the fact has an owner.")
                for match in FAMILY_COUNT.finditer(line):
                    seen += 1
                    if int(match.group(1)) != families:
                        problems.append(
                            f"{relative}:{number}: says {match.group(1)} implementation families; "
                            f"there are {families} (directories under modules/renderers with a "
                            f"src/).")
        if must_be_visible and seen == 0:
            problems.append(
                f"{relative}: is listed as stating a whole-registry count, and this check can no "
                f"longer see one. A count it cannot see is as unowned as one nobody wrote down. "
                f"State it as '<N> public renderer identities' and/or '<N> implementation "
                f"families', or move this file to the no-count entries in "
                f"{os.path.basename(__file__)} if the number was removed on purpose.")
    return problems


def main():
    expected_cmake = [c for c, _ in IDENTITIES]
    expected_enum = [e for _, e in IDENTITIES]
    ok = True

    actual_enum = enum_identities()
    if actual_enum != expected_enum:
        ok = False
        print("GraphicsRendererType enum diverges from the canonical identity table:")
        print(f"  expected ({len(expected_enum)}): {expected_enum}")
        print(f"  actual   ({len(actual_enum)}): {actual_enum}")

    # The STRINGS property is a UI list -- its member SET is the identity registry, its
    # ordering is cosmetic (and has historically differed from the enum's order).
    actual_cmake = cmake_identities()
    if sorted(actual_cmake) != sorted(expected_cmake) or len(actual_cmake) != len(expected_cmake):
        ok = False
        print("CNA_GRAPHICS_RENDERER STRINGS diverge from the canonical identity table:")
        print(f"  expected ({len(expected_cmake)}): {sorted(expected_cmake)}")
        print(f"  actual   ({len(actual_cmake)}): {sorted(actual_cmake)}")

    runtime = check_runtime_registry(IDENTITIES)
    if runtime:
        ok = False
        print("Runtime renderer registry does not back every public identity:")
        for problem in runtime:
            print(f"  - {problem}")

    families = family_count()
    stale = documented_counts(len(IDENTITIES), families)
    if stale:
        ok = False
        print("Documented renderer counts disagree with the registry:")
        for problem in stale:
            print(f"  - {problem}")

    if ok:
        print(f"OK: {len(IDENTITIES)} public renderer identities preserved in the enum, the cmake "
              f"selection list and the runtime registry, over {families} implementation families; "
              f"every documented count agrees")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
