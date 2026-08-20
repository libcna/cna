#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plan_modern.md MOD-1902 -- the const-correctness / [[nodiscard]] sweep, as a gate.

A one-time read of 49 headers decays the moment the next header lands, so the sweep that closed
MOD-1902 runs as a ctest instead of living in a commit message.

Two rules, both narrow enough to be mechanical:

1. **Every value-returning accessor is [[nodiscard]].** An accessor is a member declaration whose
   name begins with get/is/has and whose return type is not void. Discarding one of those is always
   a bug -- it does nothing at all.
2. **Every value-returning accessor that takes no argument is const.** A zero-argument getter that
   mutates is either misnamed or hiding state. A `static` member is excused from this one rather
   than exempted by name: a static member function cannot be const, so the rule has nothing to ask
   of it. Rule 1 still applies -- discarding a static accessor's return value is just as pointless.

Both rules have deliberate exemptions, listed in EXEMPT below with the reason in the entry itself.
Adding to that list is a decision; the point of the gate is that it has to be made explicitly.

Usage: check_cnaext_nodiscard.py [repo-root]
"""

import re
import sys
from pathlib import Path

# name -> why it is allowed to break rule 2. Rule 1 has no exemptions.
EXEMPT_NONCONST = {
    # Free functions in the CNA::Graphics namespace, not members -- "const" is not a thing they can
    # be. They match the accessor pattern only because of their names.
    "getEngineLayerVersion": "free function, not a member",
    "getEngineLayerVersionString": "free function, not a member",
    # The non-const half of a const/non-const overload pair. Handing out a mutable reference to
    # owned state is the whole point of these two, and both do have a const sibling.
    "getTargetPool": "non-const half of an overload pair (PostProcessChain)",
    "getSettings": "non-const half of an overload pair (RenderPipeline)",
}

STATIC_DECL = re.compile(r"^(?:CNAEXT\s+)?(?:\[\[nodiscard\]\]\s*)?(?:CNAEXT\s+)?static\b")

ACCESSOR = re.compile(
    r"^(?:CNAEXT\s+)?(?:\[\[nodiscard\]\]\s*)?(?:CNAEXT\s+)?"
    r"(?:static\s+|virtual\s+|explicit\s+|constexpr\s+|inline\s+)*"
    r"(?P<ret>[A-Za-z_][\w:<>,\s\*&]*?)\s+"
    r"(?P<name>(?:get|is|has)[A-Z]\w*)\s*\((?P<args>[^)]*)\)\s*"
    r"(?P<const>const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:=\s*0)?\s*;"
)


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    headers = sorted((root / "modules/graphics-ext/include").rglob("*.hpp"))
    if not headers:
        print(f"check_cnaext_nodiscard: no headers under {root}", file=sys.stderr)
        return 2

    problems = []
    checked = 0
    for header in headers:
        for number, line in enumerate(header.read_text(encoding="utf-8").split("\n"), start=1):
            text = line.strip()
            if not text or text.startswith(("//", "*", "/*", "#")):
                continue
            match = ACCESSOR.match(text)
            if not match or match.group("ret") == "void":
                continue
            checked += 1
            where = f"{header.relative_to(root)}:{number}"
            if "[[nodiscard]]" not in text:
                problems.append(f"{where}: accessor without [[nodiscard]] -- {text}")
            if (not match.group("const") and not match.group("args").strip()
                    and not STATIC_DECL.match(text)):
                if match.group("name") not in EXEMPT_NONCONST:
                    problems.append(f"{where}: zero-argument accessor is not const -- {text}")

    if problems:
        print("MOD-1902: the engine layer's accessor conventions have drifted:")
        for problem in problems:
            print(f"  {problem}")
        print("\nFix the declaration, or add a named exemption with its reason to this script.")
        return 1

    print(f"MOD-1902: {checked} accessors across {len(headers)} headers, all "
          f"[[nodiscard]] and const where they take no argument.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
