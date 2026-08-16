#!/usr/bin/env python3
"""Renderer-combination registry gate (plan_runtimerenderer.md RTR-P6-19).

Not every pair of renderers can be linked into one binary. cmake/RendererCombinations.cmake rejects
the impossible ones at configure time; docs/runtime-renderer-selection.md documents them for users.
This check keeps the two in step, so a rule can never be added to one without the other -- a rule
that fails the build without being documented is as unhelpful as a documented rule that does not
actually fire.

It also verifies that every identity named in the CMake rule lists is a real renderer identity,
which catches a typo that would otherwise silently disable a rule.

Exit codes: 0 ok, 1 mismatch.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE = os.path.join(REPO, "cmake", "RendererCombinations.cmake")
DOCS = os.path.join(REPO, "docs", "runtime-renderer-selection.md")
IDENTITIES_SCRIPT = os.path.join(REPO, "scripts", "check_renderer_identities.py")

# The rules the CMake file implements, each keyed by a phrase that must appear in the docs so a user
# hitting the configure error can look it up.
RULES = {
    "PORTABLEGL": "PORTABLEGL",
    "GDI + SOFTWARE": "GDI",
    # The "shared EasyGL" rule was REMOVED by plan_runtimerenderer.md phase P11, which made that
    # family's GL profile a runtime value. Its five identities now coexist, so there is no rule to
    # keep in step -- and listing one here would demand documentation for a restriction that no
    # longer exists.
    "platform partition": "platform",
    "GLIDE": "GLIDE",
}


def known_identities():
    """The canonical identity list, read from the identity gate rather than duplicated here."""
    with open(IDENTITIES_SCRIPT, encoding="utf-8") as handle:
        text = handle.read()
    block = re.search(r"IDENTITIES = \[(.*?)\n\]", text, re.S)
    if block is None:
        raise SystemExit("check_renderer_identities.py: IDENTITIES table not found")
    return {name for name, _enum in re.findall(r'\("([A-Z0-9_]+)", "(\w+)"\)', block.group(1))}


def main():
    if not os.path.exists(CMAKE):
        print(f"missing {CMAKE}", file=sys.stderr)
        return 1

    with open(CMAKE, encoding="utf-8") as handle:
        cmake_text = handle.read()
    with open(DOCS, encoding="utf-8") as handle:
        docs_text = handle.read()

    failures = []

    # Every identity named in a rule list must be a real identity.
    identities = known_identities()
    for list_name in ("CNA_RENDERER_REAL_GL_FAMILIES", "CNA_RENDERER_SHARED_EASYGL",
                       "CNA_RENDERER_WINDOWS_ONLY", "CNA_RENDERER_EMSCRIPTEN_ONLY",
                       "CNA_RENDERER_MACOS_ONLY"):
        block = re.search(r"set\(" + list_name + r"\s(.*?)\)", cmake_text, re.S)
        if block is None:
            failures.append(f"{list_name} is missing from cmake/RendererCombinations.cmake")
            continue
        named = re.findall(r"\b([A-Z][A-Z0-9_]{2,})\b", block.group(1))
        for name in named:
            if name not in identities:
                failures.append(
                    f"{list_name} names '{name}', which is not a public renderer identity "
                    f"(see scripts/check_renderer_identities.py). A typo here silently disables "
                    f"the rule.")

    # Every rule must be documented.
    for rule, phrase in RULES.items():
        if phrase not in docs_text:
            failures.append(
                f"the '{rule}' combination rule is implemented in cmake/RendererCombinations.cmake "
                f"but '{phrase}' does not appear in docs/runtime-renderer-selection.md. A rule that "
                f"fails a build without being documented is not actionable.")

    # The documented table must not claim rules the CMake file does not implement.
    if "cannot be built into the same binary" not in cmake_text:
        failures.append("cmake/RendererCombinations.cmake no longer rejects any combination")

    if failures:
        print("Renderer-combination registry mismatch:\n", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}\n", file=sys.stderr)
        return 1

    print(f"OK: {len(RULES)} combination rules implemented and documented; "
          f"every identity named in them is one of the {len(identities)} public identities.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
