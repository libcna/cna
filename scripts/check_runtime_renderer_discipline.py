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

Also verifies the complementary invariant: every renderer family owns exactly one descriptor
translation unit, so no family can be added without its pre-construction contract.

Exit codes: 0 ok, 1 violation.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRAPHICS_SRC = os.path.join(REPO, "modules", "graphics", "src")
RENDERERS = os.path.join(REPO, "modules", "renderers")

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


def main():
    failures = check_xna_layer()
    coverage_failures, family_count = check_descriptor_coverage()
    failures += coverage_failures

    if failures:
        print("Runtime-dispatch discipline violations:\n", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}\n", file=sys.stderr)
        return 1

    print(f"OK: {family_count} renderer families each own one descriptor unit; "
          f"modules/graphics/src is free of CNA_RENDERER_* entirely.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
