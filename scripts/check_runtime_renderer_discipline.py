#!/usr/bin/env python3
"""Runtime-dispatch discipline gate (plan_runtimerenderer.md RTR-P1-6).

Renderer selection used to leak into the XNA layer as `#ifdef CNA_RENDERER_<X>` blocks. Phase P1
removed every window-creation one by routing those decisions through GraphicsRendererDescriptor;
phase P3 removed the rest, behind IGraphicsRenderer virtuals (queries that have a device) and
GraphicsRendererDescriptor::adapterQueries hooks (GraphicsAdapter queries, which run before one
exists). modules/graphics/src is now free of them entirely.

This check pins that: any `CNA_RENDERER_*` reappearing in modules/graphics/src fails here. The
ALLOWED table is empty and is meant to stay that way -- a renderer-specific behaviour belongs
behind a virtual or a hook, not behind the preprocessor.

Also verifies the complementary invariants that make a renderer runtime-SELECTABLE rather than
merely compilable, each of which PIXIJS shipped without and nothing caught:

  * every renderer family owns exactly one descriptor translation unit, so no family can be added
    without its pre-construction contract;
  * every public identity has an entry in cmake/RendererRegistry.cmake's identity -> namespace map,
    and that entry's namespace and descriptor accessor really exist in a descriptor unit -- the
    whole chain identity -> namespace -> accessor -> descriptor -> factory, checked end to end,
    because a break anywhere in it means the renderer cannot reach the generated registry;
  * no family defines the bare CNA::Internal::Renderers::CreateGraphicsRenderer that design
    decision 4 moved into the family namespaces -- the one symbol whose duplication is what made
    multi-renderer builds impossible in the first place;
  * cmake/RendererSelection.cmake announces an identity's own CNA_RENDERER_<X> macro by appending
    to _cna_identity_defines, never with a directory-scoped add_compile_definitions() that would
    leak a non-default identity's macro across the whole project.

Exit codes: 0 ok, 1 violation.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRAPHICS_SRC = os.path.join(REPO, "modules", "graphics", "src")
RENDERERS = os.path.join(REPO, "modules", "renderers")
REGISTRY_CMAKE = os.path.join(REPO, "cmake", "RendererRegistry.cmake")
SELECTION_CMAKE = os.path.join(REPO, "cmake", "RendererSelection.cmake")
IDENTITIES_SCRIPT = os.path.join(REPO, "scripts", "check_renderer_identities.py")

# Remaining, deliberately tolerated compile-time renderer branches in the XNA layer, with the task
# that removes each. Every entry here is a known debt, not an accepted pattern.
#   file (repo-relative) -> (max occurrences, removing task)
ALLOWED = {
    # Empty as of RTR-P3: every renderer-specific behaviour in the XNA layer now goes through an
    # IGraphicsRenderer virtual (texture/render-target queries, which have a device) or a
    # GraphicsRendererDescriptor adapter hook (GraphicsAdapter queries, which run before one
    # exists). Do not add entries here -- add a virtual or a hook instead.
}

# Window creation is fully descriptor-driven as of P1: none of these may reappear in the XNA layer.
WINDOW_MACROS = re.compile(
    r"CNA_RENDERER_(EASYGL|OPENGL1|OPENGL2|OPENGL4|OPENGLES1|OPENVG|MAGNUM|VULKAN|SOKOL|"
    r"DILIGENT|METAL|BGFX|LLGL|FNA3D|HEADLESS|SOFTWARE|STUB|PORTABLEGL)\b")

MACRO = re.compile(r"CNA_RENDERER_[A-Z0-9_]+")


def check_xna_layer():
    failures = []
    for root, _dirs, files in os.walk(GRAPHICS_SRC):
        for name in files:
            if not name.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(root, name)
            rel = os.path.relpath(path, REPO)
            with open(path, encoding="utf-8") as handle:
                text = handle.read()

            hits = MACRO.findall(text)
            if not hits:
                continue

            window_hits = sorted(set(WINDOW_MACROS.findall(text)))
            if window_hits:
                failures.append(
                    f"{rel}: window-creation renderer macro(s) reintroduced into the XNA layer: "
                    f"{', '.join('CNA_RENDERER_' + h for h in window_hits)}. "
                    f"Window/video-subsystem decisions belong in that family's "
                    f"GraphicsRendererDescriptor (plan_runtimerenderer.md design decision 2).")

            budget, task = ALLOWED.get(rel, (0, None))
            if len(hits) > budget:
                if task:
                    failures.append(
                        f"{rel}: {len(hits)} CNA_RENDERER_* occurrences, allowed {budget} "
                        f"(pending {task}). Do not add more -- the allowlist shrinks, never grows.")
                else:
                    failures.append(
                        f"{rel}: {len(hits)} CNA_RENDERER_* occurrences in the XNA layer, none "
                        f"allowed. Route renderer-specific behaviour through an IGraphicsRenderer "
                        f"virtual (plan_runtimerenderer.md design decision 9).")
    return failures


def check_descriptor_coverage():
    failures = []
    families = sorted(
        name for name in os.listdir(RENDERERS)
        if os.path.isdir(os.path.join(RENDERERS, name, "src")))
    for family in families:
        src = os.path.join(RENDERERS, family, "src")
        descriptors = [f for f in os.listdir(src) if f.endswith("RendererDescriptor.cpp")]
        if len(descriptors) != 1:
            failures.append(
                f"modules/renderers/{family}/src: expected exactly one *RendererDescriptor.cpp "
                f"(the family's pre-construction contract), found {len(descriptors)}.")
    return failures, len(families)


def public_identities():
    """The canonical identity list, read from the identity gate rather than duplicated here."""
    with open(IDENTITIES_SCRIPT, encoding="utf-8") as handle:
        block = re.search(r"IDENTITIES = \[(.*?)\n\]", handle.read(), re.S)
    if block is None:
        raise SystemExit("check_renderer_identities.py: IDENTITIES table not found")
    return [name for name, _enum in re.findall(r'\("([A-Z0-9_]+)", "(\w+)"\)', block.group(1))]


def family_sources(suffixes=(".cpp", ".mm")):
    """Every renderer-family implementation unit, as (repo-relative path, text)."""
    for family in sorted(os.listdir(RENDERERS)):
        src = os.path.join(RENDERERS, family, "src")
        if not os.path.isdir(src):
            continue
        for name in sorted(os.listdir(src)):
            if not name.endswith(suffixes):
                continue
            path = os.path.join(src, name)
            with open(path, encoding="utf-8") as handle:
                yield os.path.relpath(path, REPO), handle.read()


# `std::unique_ptr<IGraphicsRenderer> [Family::]CreateGraphicsRenderer(` -- exactly the factory,
# so CreateGraphicsRendererForProfile and CreateGraphicsRendererImpl do not match.
FACTORY = re.compile(
    r"std::unique_ptr<IGraphicsRenderer>\s+(?:([A-Za-z_]\w*)::)?CreateGraphicsRenderer\s*\(")
NAMESPACE_OPEN = re.compile(r"\bnamespace\s+([A-Za-z_][\w:]*)?\s*\{")


def namespace_scopes(text):
    """Maps each offset range in @p text to the C++ namespace open there.

    Brace-aware on purpose rather than "nearest preceding `namespace` line": every family's
    factory unit opens `namespace CNA::Internal::Renderers` and then, INSIDE it, writes a
    one-line `namespace <Family> { ...declaration; }` before defining the factory. That closed
    one-liner is the nearest preceding namespace text while not being the enclosing scope at
    all, so a line-based reading calls the shared namespace a family one and passes a genuine
    violation -- which is exactly what it did when this check was first written.

    @param text The translation unit's source.
    @return A list of (start, end, fully-qualified namespace) triples, innermost last.
    """
    # Comments and literals are removed first: a `namespace` or a brace inside either is text,
    # not structure.
    stripped = re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
                      lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)

    scopes = []
    stack = []      # (name, depth at which this namespace's brace was opened)
    depth = 0
    index = 0
    while index < len(stripped):
        char = stripped[index]
        if char == "{":
            opener = NAMESPACE_OPEN.search(stripped, max(0, index - 200), index + 1)
            depth += 1
            if opener is not None and opener.end() == index + 1:
                stack.append((opener.group(1) or "", depth))
                scopes.append([opener.start(), None,
                               "::".join(name for name, _ in stack if name)])
        elif char == "}":
            if stack and stack[-1][1] == depth:
                stack.pop()
                for scope in reversed(scopes):
                    if scope[1] is None:
                        scope[1] = index
                        break
            depth -= 1
        index += 1

    for scope in scopes:
        if scope[1] is None:
            scope[1] = len(stripped)
    return scopes


def check_factory_namespacing():
    """Design decision 4: the factory lives in the FAMILY namespace, never the shared one."""
    failures = []
    for rel, text in family_sources():
        scopes = namespace_scopes(text)
        for match in FACTORY.finditer(text):
            if match.group(1):
                continue  # written qualified, e.g. `Canvas::CreateGraphicsRenderer`
            if not _is_definition(text, match.end() - 1):
                continue  # a declaration; the descriptor unit legitimately carries one
            enclosing = ""
            for start, end, name in scopes:
                if start <= match.start() < end:
                    enclosing = name  # scopes are emitted outermost-first, so the last wins
            if enclosing == "CNA::Internal::Renderers" or not enclosing:
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{rel}:{line}: defines CNA::Internal::Renderers::CreateGraphicsRenderer -- the "
                    f"single shared factory symbol plan_runtimerenderer.md design decision 4 "
                    f"replaced. Two renderer archives defining it cannot link into one binary, "
                    f"which is the whole reason multi-renderer builds were impossible. Define "
                    f"CNA::Internal::Renderers::<Family>::CreateGraphicsRenderer instead and take "
                    f"its address from that family's GraphicsRendererDescriptor::create.")
    return failures


def _is_definition(text, open_paren_index):
    """True when the signature starting at @p open_paren_index is followed by a body."""
    depth = 0
    for index in range(open_paren_index, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[index + 1:index + 200].lstrip().startswith("{")
    return False


def registry_map():
    """cmake/RendererRegistry.cmake's identity -> (namespace, accessor) table."""
    with open(REGISTRY_CMAKE, encoding="utf-8") as handle:
        text = handle.read()
    block = re.search(r"set\(_map\n(.*?)\n\s*list\(FIND", text, re.S)
    if block is None:
        raise SystemExit("cmake/RendererRegistry.cmake: the identity -> namespace _map was not found")
    mapping = {}
    for identity, entry in re.findall(r"([A-Z][A-Z0-9_]*)\s+([A-Za-z][\w|]*)", block.group(1)):
        namespace, _, accessor = entry.partition("|")
        mapping[identity] = (namespace, accessor or "GetDescriptor")
    return mapping


def check_registry_map():
    """Every identity reaches the generated registry, through a namespace/accessor that exists.

    This is the gap PIXIJS fell through: it was a complete public identity -- enum entry, CMake
    STRINGS member, its own family directory -- while having no map entry at all, so configuring it
    could only ever have died inside cna_renderer_identity_to_namespace(). The chain is checked end
    to end here rather than at its first link, because a namespace that is spelled but not defined,
    or an accessor no descriptor unit declares, fails equally late and equally confusingly.
    """
    failures = []
    mapping = registry_map()

    declared = {}   # namespace -> set of descriptor accessors it defines
    for _rel, text in family_sources():
        for namespace in re.findall(r"\bnamespace\s+CNA::Internal::Renderers::(\w+)", text):
            declared.setdefault(namespace, set())
        for namespace in re.findall(r"\bnamespace\s+CNA::Internal::Renderers::(\w+)", text):
            for accessor in re.findall(
                    r"const\s+GraphicsRendererDescriptor&\s+(GetDescriptor\w*)\s*\(\s*\)", text):
                declared[namespace].add(accessor)

    for identity in public_identities():
        if identity not in mapping:
            failures.append(
                f"renderer identity {identity} has no entry in cmake/RendererRegistry.cmake's "
                f"identity -> namespace map, so it can never appear in the generated runtime "
                f"registry -- configuring it fails inside cna_renderer_identity_to_namespace(). "
                f"A new identity needs an entry there as well as in GraphicsRendererType.hpp and "
                f"scripts/check_renderer_identities.py.")
            continue
        namespace, accessor = mapping[identity]
        if namespace not in declared:
            failures.append(
                f"renderer identity {identity} maps to namespace "
                f"CNA::Internal::Renderers::{namespace}, which no renderer family's source "
                f"declares. The generated registry would not link.")
        elif accessor not in declared[namespace]:
            failures.append(
                f"renderer identity {identity} maps to "
                f"CNA::Internal::Renderers::{namespace}::{accessor}(), which that family's "
                f"descriptor unit does not define. The generated registry would not link.")

    seen = {}
    for identity, entry in mapping.items():
        if entry in seen:
            failures.append(
                f"renderer identities {seen[entry]} and {identity} both map to "
                f"{entry[0]}::{entry[1]}() -- two identities cannot share one descriptor.")
        seen[entry] = identity
    return failures


IDENTITY_DEFINE = re.compile(r"^\s*add_compile_definitions\(\s*(CNA_RENDERER_[A-Z0-9_]+)\s*\)")


def check_identity_define_scope():
    """RTR-P6-4: an identity macro is target-private data, not a directory-scoped definition."""
    failures = []
    known = {f"CNA_RENDERER_{identity}" for identity in public_identities()}
    with open(SELECTION_CMAKE, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            match = IDENTITY_DEFINE.match(line)
            if match is None or match.group(1) not in known:
                continue
            failures.append(
                f"cmake/RendererSelection.cmake:{number}: announces {match.group(1)} with "
                f"add_compile_definitions(). That is DIRECTORY scoped and this file is included "
                f"from the top-level CMakeLists.txt, so in a multi-renderer build the macro is "
                f"defined project-wide even when that identity is not the default -- breaking the "
                f"invariant that a defined CNA_RENDERER_<X> names the DEFAULT renderer, which "
                f"getCurrentGraphicsRendererType() and every renderer-gated test relies on. Use "
                f"list(APPEND _cna_identity_defines {match.group(1)}) instead "
                f"(plan_runtimerenderer.md RTR-P6-4).")
    return failures


def main():
    failures = check_xna_layer()
    coverage_failures, family_count = check_descriptor_coverage()
    failures += coverage_failures
    failures += check_factory_namespacing()
    failures += check_registry_map()
    failures += check_identity_define_scope()

    if failures:
        print("Runtime-dispatch discipline violations:\n", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}\n", file=sys.stderr)
        return 1

    print(f"OK: {family_count} renderer families each own one descriptor unit and a "
          f"family-namespaced factory; all {len(public_identities())} public identities reach the "
          f"generated registry; modules/graphics/src is free of CNA_RENDERER_* entirely.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
