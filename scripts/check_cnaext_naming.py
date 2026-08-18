#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plan_modern.md MOD-1900 -- the engine layer's naming rule, as a gate.

MOD-6 settled the one rule that differs from the XNA layer, because there is no XNA name to
preserve here: **verbs are lowerCamelCase**. Types and enum values stay UpperCamelCase, which is the
same as everywhere else and needs no gate.

Every UpperCamelCase function in `modules/graphics-ext/include/CNA/Graphics/` is therefore either a
deviation or a deliberate exception, and the point of this script is that it has to be one or the
other explicitly. The exceptions all have the same shape -- a name that belongs to XNA or .NET
rather than to this layer -- and each carries its reason below.

Usage: check_cnaext_naming.py [repo-root]
"""

import re
import sys
from pathlib import Path

# "Class::name" or a bare name for namespace-scope functions -> why it keeps an XNA/.NET spelling.
EXEMPT = {
    # Overrides of Microsoft::Xna::Framework::Graphics::Effect. The base class chose these names.
    "CRTEffect::GetTypeName": "override of an XNA base-class member",
    "CRTEffect::Clone": "override of an XNA base-class member",
    "CRTEffect::OnApply": "override of an XNA base-class member",
    "DepthEffect::GetTypeName": "override of an XNA base-class member",
    "DepthEffect::Clone": "override of an XNA base-class member",
    "DepthEffect::OnApply": "override of an XNA base-class member",
    # Deliberate mirrors of System.Object's members, spelled as the rest of CNA spells them.
    "PbrMaterial::GetHashCode": "mirrors System.Object.GetHashCode",
    "PbrMaterial::ToString": "mirrors System.Object.ToString",
    # Predates the engine layer and its naming rule, and is exposed through the C ABI
    # (modules/c-api/src/CnaCApiGraphicsExt.cpp), so the name is not this layer's to change.
    "AsciiPostProcessEffect::Draw": "predates MOD-6 and is part of the C ABI surface",
    "AsciiPostProcessEffect::GetLastGridDimensions": "predates MOD-6 and is part of the C ABI surface",
}

DECL = re.compile(
    r"^(?:CNAEXT\s+)?(?:\[\[nodiscard\]\]\s*)?(?:CNAEXT\s+)?"
    r"(?:static\s+|virtual\s+|explicit\s+|constexpr\s+|inline\s+)*"
    r"(?P<ret>[A-Za-z_][\w:<>,\s\*&]*?)\s+(?P<name>[A-Z]\w*)\s*\("
)
NOT_A_RETURN_TYPE = {"return", "else", "if", "using", "friend", "case", "delete", "new"}


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    headers = sorted((root / "modules/graphics-ext/include/CNA/Graphics").glob("*.hpp"))
    if not headers:
        print(f"check_cnaext_naming: no headers under {root}", file=sys.stderr)
        return 2

    problems = []
    exercised = set()
    for header in headers:
        owner = header.stem
        for number, line in enumerate(header.read_text(encoding="utf-8").split("\n"), start=1):
            text = line.strip()
            if not text or text.startswith(("//", "*", "/*", "#")):
                continue
            match = DECL.match(text)
            if not match or match.group("ret") in NOT_A_RETURN_TYPE:
                continue
            name = match.group("name")
            if name in (owner, "~" + owner) or name.startswith("operator"):
                continue
            qualified = f"{owner}::{name}"
            if qualified in EXEMPT:
                exercised.add(qualified)
                continue
            if name in EXEMPT:
                exercised.add(name)
                continue
            problems.append(
                f"{header.relative_to(root)}:{number}: {name} is UpperCamelCase -- MOD-6 spells "
                f"verbs lowerCamelCase here. Rename it, or add it to EXEMPT with its reason.")

    stale = sorted(set(EXEMPT) - exercised)
    for entry in stale:
        problems.append(f"EXEMPT lists {entry}, which no longer exists -- drop the entry.")

    if problems:
        print("MOD-1900: the engine layer's naming rule has drifted:")
        for problem in problems:
            print(f"  {problem}")
        return 1

    print(f"MOD-1900: {len(headers)} headers, every function lowerCamelCase apart from "
          f"{len(EXEMPT)} named XNA/.NET-shaped exceptions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
