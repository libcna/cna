#!/usr/bin/env python3
"""Renderer-identity registry gate (plans/MODULARIZATION_PLAN.md §2.3).

CNA has exactly 25 public renderer identities (plans/plan_renderer_cleanup.md). This check mechanically compares
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

# "48 public renderer identities", "45 families / 48 public identities", "45 implementation
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

# Canonical public identities: (cmake selection name, enum name, C ABI value). 25 entries.
#
# The C ABI value is the ONLY stable numeric identity contract CNA publishes. The C++ enum's
# ordinals are not one and never were: they are dense and were renumbered when SKIA was retired in
# 2026-08, while every surviving C value stayed put. So this column is pinned here, per identity,
# rather than derived from a position in a list -- a table whose numbers come from its own ordering
# cannot catch a renumbering, which is the single most damaging thing that can happen to this file.
IDENTITIES = [
    ("SDL_RENDERER", "SdlRenderer", 1),
    ("OPENGLES2", "OpenGLES2", 2),
    ("OPENGLES3", "OpenGLES3", 3),
    ("OPENGL33", "OpenGL33", 4),
    ("WEBGL1", "WebGL1", 5),
    ("WEBGL2", "WebGL2", 6),
    ("VULKAN", "Vulkan", 8),
    ("WEBGPU", "WebGPU", 9),
    ("HEADLESS", "Headless", 11),
    ("SOFTWARE", "Software", 12),
    ("STUB", "Stub", 13),
    ("DIRECTX11", "DirectX11", 14),
    ("DIRECTX12", "DirectX12", 15),
    ("DIRECT2D", "Direct2D", 16),
    ("CANVAS", "Canvas", 17),
    ("HTML_DOM", "HtmlDom", 18),
    ("FREEDIRECT", "FreeDirect", 21),
    ("DIRECTX9", "DirectX9", 22),
    ("SDL_GPU", "SdlGpu", 31),
    ("OPENGL4", "OpenGL4", 33),
    ("GDI", "Gdi", 40),
    ("METAL", "Metal", 42),
    ("FNA3D", "Fna3d", 43),
    ("SVG_DOM", "SvgDom", 44),
    ("PORTABLEGL", "PortableGL", 46),
]

# Retired identities and their permanently reserved C ABI values. A retired value is never
# reassigned -- not to a new renderer, and not to the same renderer if it is ever restored with
# different semantics -- so this table only ever grows. SKIA (19) was retired in 2026-08; the other
# twenty-five on 2026-09-17 (plans/plan_renderer_cleanup.md, docs/removed-renderers.md).
#
# It is pinned here as well as in cmake/RendererIdentities.cmake on purpose. The CMake list exists
# to REFUSE a retired selector at configure time; this one exists to make the refusal itself
# checkable, and to make reuse of a value a test failure rather than a code review's job. The
# precedent is concrete: eleven identities were retired on 2026-08-30 and restored on 2026-09-04,
# and nothing mechanical would have stopped that restoration from taking different numbers.
RETIRED_IDENTITIES = {
    "BGFX": 7, "MAGNUM": 10, "SKIA": 19, "BLEND2D": 20,
    "DIRECTX1": 23, "DIRECTX2": 24, "DIRECTX3": 25, "DIRECTX5": 26, "DIRECTX6": 27,
    "DIRECTX7": 28, "DIRECTX8": 29, "DIRECTX10": 30, "OPENGLES1": 32, "OPENGL1": 34,
    "OPENGL2": 35, "WICKED": 36, "SOKOL": 37, "DILIGENT": 38, "GLIDE": 39, "LLGL": 41,
    "OPENVG": 45, "TINYGL": 47, "IGL": 48, "PIXIJS": 49, "NANOVG": 50, "RLGL": 51,
}

# The first value never assigned to any identity, live or retired. A new renderer takes this one.
NEXT_FREE_ABI_VALUE = 52


def enum_identities():
    path = os.path.join(REPO, "modules", "core", "include", "CNA", "GraphicsRendererType.hpp")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"enum class GraphicsRendererType\s*\{(.*?)\n\s*\};", text, re.S)
    if not body:
        sys.exit("cannot locate enum class GraphicsRendererType")
    stripped = re.sub(r"/\*.*?\*/|//[^\n]*", "", body.group(1), flags=re.S)
    return re.findall(r"\b([A-Za-z_]\w*)\b", stripped)


def cmake_identities():
    """The CMake selection list: cmake/RendererIdentities.cmake's CNA_RENDERER_PUBLIC_IDENTITIES.

    cmake/RendererSelection.cmake must publish exactly that list as CNA_GRAPHICS_RENDERER's STRINGS
    rather than a literal copy of its own, or the two could drift.
    """
    path = os.path.join(REPO, "cmake", "RendererIdentities.cmake")
    text = open(path, encoding="utf-8").read()
    m = re.search(r"set\(CNA_RENDERER_PUBLIC_IDENTITIES\s(.*?)\)", text, re.S)
    if not m:
        sys.exit("cannot locate CNA_RENDERER_PUBLIC_IDENTITIES in cmake/RendererIdentities.cmake")
    selection = open(os.path.join(REPO, "cmake", "RendererSelection.cmake"), encoding="utf-8").read()
    if not re.search(r"set_property\(CACHE CNA_GRAPHICS_RENDERER PROPERTY STRINGS "
                     r"\$\{CNA_RENDERER_PUBLIC_IDENTITIES\}\)", selection):
        sys.exit("cmake/RendererSelection.cmake no longer publishes CNA_RENDERER_PUBLIC_IDENTITIES "
                 "as the CNA_GRAPHICS_RENDERER STRINGS property")
    return re.findall(r"\b([A-Z][A-Z0-9_]*)\b", re.sub(r"#[^\n]*", "", m.group(1)))


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

    for cmake_name, _enum_name, _value in identities:
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

    for cmake_name in sorted(set(mapping) - {c for c, _e, _v in identities}):
        problems.append(
            f"{cmake_name}: cmake/RendererRegistry.cmake maps an identity that is not in the "
            f"canonical table. Remove it, or add it to IDENTITIES here and to both registries.")
    return problems


def c_abi_values():
    """The CNA_GRAPHICS_RENDERER_<NAME> constants and what CNA_GRAPHICS_RENDERER_MAXIMUM aliases."""
    path = os.path.join(REPO, "modules", "c-api", "include", "CNA", "C", "graphics.h")
    text = open(path, encoding="utf-8").read()
    values = {name: int(value) for name, value in re.findall(
        r"#define\s+CNA_GRAPHICS_RENDERER_([A-Z0-9_]+)\s+UINT32_C\((\d+)\)", text)}
    maximum = re.search(
        r"#define\s+CNA_GRAPHICS_RENDERER_MAXIMUM\s+CNA_GRAPHICS_RENDERER_([A-Z0-9_]+)", text)
    return values, (maximum.group(1) if maximum else None)


def cmake_retired():
    """cmake/RendererIdentities.cmake's CNA_RENDERER_RETIRED_IDENTITIES, as {name: value}."""
    path = os.path.join(REPO, "cmake", "RendererIdentities.cmake")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"set\(CNA_RENDERER_RETIRED_IDENTITIES\s(.*?)\)", text, re.S)
    if not body:
        sys.exit("cannot locate CNA_RENDERER_RETIRED_IDENTITIES in cmake/RendererIdentities.cmake")
    stripped = re.sub(r"#[^\n]*", "", body.group(1))
    return {name: int(value) for name, value in re.findall(r"([A-Z][A-Z0-9_]*)=(\d+)", stripped)}


def c_api_identity_table():
    """The identity->enum pairs of RendererIdentities[] in modules/c-api/src/CnaCApiCoreExt.cpp."""
    path = os.path.join(REPO, "modules", "c-api", "src", "CnaCApiCoreExt.cpp")
    text = open(path, encoding="utf-8").read()
    body = re.search(r"RendererIdentities\{\{(.*?)\}\};", text, re.S)
    if not body:
        sys.exit("cannot locate the RendererIdentities table in modules/c-api/src/CnaCApiCoreExt.cpp")
    return re.findall(r"CNA_GRAPHICS_RENDERER_([A-Z0-9_]+)\s*,\s*"
                      r"CNA::GraphicsRendererType::(\w+)", body.group(1))


def check_abi_contract(identities):
    """The numeric identity contract: live values pinned, retired values reserved forever.

    This is the half of the registry that a rename or a reordering cannot express and a reader
    cannot check by eye. Four things are held together here:

      1. Every live identity's C ABI value is exactly the one pinned in IDENTITIES. Renumbering a
         surviving renderer silently breaks every compiled consumer of the C ABI -- the one thing
         in this file that cannot be fixed forward, because the old binaries are already out.
      2. graphics.h publishes a constant for each live identity and for NO retired one. A retired
         constant left behind is a value a consumer can still pass; the routes would refuse it, but
         the header would be promising otherwise.
      3. The retired table agrees, name for name and value for value, with the CMake copy that does
         the refusing. Two tables that disagree mean one of them is refusing the wrong thing.
      4. No live value collides with a retired value, and no live NAME reuses a retired name. This
         is the invariant that outlives everyone who remembers why: value 19 is Skia's forever, and
         a future renderer takes NEXT_FREE_ABI_VALUE rather than the first hole it finds.
    """
    problems = []
    published, maximum_alias = c_abi_values()
    retired_cmake = cmake_retired()
    live = {name: value for name, _enum, value in identities}

    for name, value in live.items():
        actual = published.get(name)
        if actual is None:
            problems.append(
                f"{name}: modules/c-api/include/CNA/C/graphics.h publishes no "
                f"CNA_GRAPHICS_RENDERER_{name}, so the identity is unreachable from C.")
        elif actual != value:
            problems.append(
                f"{name}: graphics.h says {actual}, the canonical table says {value}. A surviving "
                f"renderer's C ABI value must never be renumbered -- already-compiled consumers "
                f"carry the old number.")

    for name in sorted(RETIRED_IDENTITIES):
        if name in published:
            problems.append(
                f"{name}: graphics.h still publishes CNA_GRAPHICS_RENDERER_{name} for a retired "
                f"identity. The value stays reserved; the constant does not stay published.")
        if name in live:
            problems.append(
                f"{name}: is listed as retired and as a live identity. A retired name is not "
                f"reused -- see docs/removed-renderers.md.")

    if RETIRED_IDENTITIES != retired_cmake:
        only_here = {n: v for n, v in RETIRED_IDENTITIES.items() if retired_cmake.get(n) != v}
        only_cmake = {n: v for n, v in retired_cmake.items() if RETIRED_IDENTITIES.get(n) != v}
        problems.append(
            f"the retired table here and cmake/RendererIdentities.cmake's "
            f"CNA_RENDERER_RETIRED_IDENTITIES disagree. Here but not matching there: "
            f"{only_here or '{}'}; there but not matching here: {only_cmake or '{}'}. The CMake "
            f"list is what refuses a retired selector at configure time, so a disagreement means "
            f"one of the two is refusing the wrong set.")

    collisions = sorted(set(live.values()) & set(RETIRED_IDENTITIES.values()))
    for value in collisions:
        live_name = next(n for n, v in live.items() if v == value)
        retired_name = next(n for n, v in RETIRED_IDENTITIES.items() if v == value)
        problems.append(
            f"value {value} is used by the live identity {live_name} and is also the reserved "
            f"value of the retired {retired_name}. Retired values are never reassigned; a new "
            f"identity takes {NEXT_FREE_ABI_VALUE}.")

    assigned = set(live.values()) | set(RETIRED_IDENTITIES.values())
    if NEXT_FREE_ABI_VALUE in assigned:
        problems.append(
            f"NEXT_FREE_ABI_VALUE is {NEXT_FREE_ABI_VALUE}, which is already assigned. It must be "
            f"the first value no identity has ever had.")
    if assigned and max(assigned) >= NEXT_FREE_ABI_VALUE:
        problems.append(
            f"an identity uses value {max(assigned)}, at or above NEXT_FREE_ABI_VALUE "
            f"({NEXT_FREE_ABI_VALUE}). Raise it past every assigned value.")

    highest = max(live, key=lambda name: live[name]) if live else None
    if maximum_alias is None:
        problems.append("graphics.h does not define CNA_GRAPHICS_RENDERER_MAXIMUM as an alias of a "
                        "published identity constant.")
    elif maximum_alias != highest:
        problems.append(
            f"CNA_GRAPHICS_RENDERER_MAXIMUM aliases {maximum_alias}; the highest-valued live "
            f"identity is {highest} ({live[highest]}).")

    table = c_api_identity_table()
    expected = [(name, enum) for name, enum, _value in identities]
    if sorted(table) != sorted(expected):
        problems.append(
            f"modules/c-api/src/CnaCApiCoreExt.cpp's RendererIdentities table does not match the "
            f"canonical table: it has {len(table)} entries, {len(expected)} expected; "
            f"missing {sorted(set(expected) - set(table))}, unexpected "
            f"{sorted(set(table) - set(expected))}.")
    return problems


def check_no_retired_selectors(identities):
    """No retired identity may reappear as a selectable renderer.

    Checked against the two lists a user can actually reach: the CMake STRINGS set and the C++
    enum. A retired name in either is a renderer that can be selected again without anyone
    deciding to restore it -- which is precisely what happened between 2026-08-30 and 2026-09-04,
    when eleven identities came back.
    """
    problems = []
    live_cmake = set(cmake_identities())
    live_enum = set(enum_identities())
    enum_spelling = {name: enum for name, enum, _ in identities}

    for name in sorted(RETIRED_IDENTITIES):
        if name in live_cmake:
            problems.append(
                f"{name}: is retired but appears in CNA_RENDERER_PUBLIC_IDENTITIES, so "
                f"-DCNA_GRAPHICS_RENDERER={name} would be accepted again.")
        # The enum spells identities in UpperCamelCase; compare case- and underscore-insensitively
        # so BGFX matches Bgfx and HTML_DOM matches HtmlDom.
        flattened = name.replace("_", "").lower()
        for enumerator in live_enum:
            if enumerator.lower() == flattened and enumerator not in enum_spelling.values():
                problems.append(
                    f"{name}: is retired but GraphicsRendererType still declares {enumerator}.")
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
    expected_cmake = [c for c, _e, _v in IDENTITIES]
    expected_enum = [e for _c, e, _v in IDENTITIES]
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

    abi = check_abi_contract(IDENTITIES)
    if abi:
        ok = False
        print("The C ABI identity contract is broken:")
        for problem in abi:
            print(f"  - {problem}")

    resurrected = check_no_retired_selectors(IDENTITIES)
    if resurrected:
        ok = False
        print("A retired renderer identity is selectable again:")
        for problem in resurrected:
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
              f"every documented count agrees; {len(RETIRED_IDENTITIES)} retired identities stay "
              f"retired with their C ABI values reserved (next free value "
              f"{NEXT_FREE_ABI_VALUE})")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
