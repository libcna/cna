#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Completeness gate for the Input public-API signature freeze (CNA-FIX-L17).

`PublicApiInputSignatureFreezeTests.cpp` pins a signature for each public Input member it names,
but a freeze cannot notice what it does not name: a member, overload, constructor or nested type
added later compiles silently. This gate closes that gap. Clang parses every public Input header;
for each public class/struct in `Microsoft::Xna::Framework::Input` and `...::Input::Touch`, nested
ones included, it counts the public declarations of each member name (overloads separately) and
requires the freeze to name that member at least as many times:

  * methods, operators and static data members/events: `&Type::name` occurrences;
  * constructors (other than copy/move): `std::is_constructible_v<Type, ...>` /
    `std::is_default_constructible_v<Type>` assertions;
  * deleted constructors and members named INTERNAL_* or *ForTests are excluded, as the freeze's
    own header comment excludes them.

It also requires each static data member whose type is a System::MulticastAction (an event) to be
pinned by a `std::is_same_v<decltype(Type::name), ...>` assertion, and each `noexcept` method to be
pinned by a `noexcept(...)` assertion, because a function-pointer conversion drops `noexcept`.

Exit 0 when complete, 1 with the missing entries listed, 2 when Clang cannot parse the headers,
77 (a CTest skip) when no clang++ is installed.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HEADER_ROOT = REPO / "modules/input/include/Microsoft/Xna/Framework/Input"
FREEZE = REPO / "modules/input/tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp"
NAMESPACES = ("Microsoft::Xna::Framework::Input", "Microsoft::Xna::Framework::Input::Touch")


def includes(sharp_root: Path) -> list[str]:
    roots = sorted(REPO.glob("modules/*/include")) + sorted(sharp_root.glob("modules/*/include"))
    return ["-I" + str(root) for root in roots]


def json_objects(raw: str) -> list[dict]:
    decoder = json.JSONDecoder()
    objects, index = [], 0
    while True:
        start = raw.find("{", index)
        if start < 0:
            return objects
        try:
            value, end = decoder.raw_decode(raw, start)
        except json.JSONDecodeError:
            index = start + 1
            continue
        objects.append(value)
        index = end


def excluded(name: str) -> bool:
    return name.startswith("INTERNAL_") or name.endswith("ForTests")


def collect(node: dict, scope: str, members: dict[str, Counter], events: set, noexcepts: set) -> None:
    """Records the public members of the record `node`, whose qualified name is `scope`."""
    access = "private" if node.get("tagUsed") == "class" else "public"
    counts = members.setdefault(scope, Counter())
    for child in node.get("inner", []):
        kind = child.get("kind")
        if kind == "AccessSpecDecl":
            access = child.get("access", access)
            continue
        if access != "public" or child.get("isImplicit"):
            continue
        name = child.get("name", "")
        if kind == "CXXRecordDecl" and child.get("completeDefinition") and name:
            collect(child, scope + "::" + name, members, events, noexcepts)
        elif kind == "CXXConstructorDecl":
            params = [p for p in child.get("inner", []) if p.get("kind") == "ParmVarDecl"]
            own = scope.rsplit("::", 1)[-1]
            special = len(params) == 1 and re.search(
                rf"\b{re.escape(own)}\s*&", params[0].get("type", {}).get("qualType", ""))
            if not child.get("explicitlyDeleted") and not special:
                counts["<ctor>"] += 1
        elif kind == "CXXMethodDecl" and not excluded(name) and not child.get("explicitlyDeleted"):
            counts[name] += 1
            if "noexcept" in child.get("type", {}).get("qualType", ""):
                noexcepts.add((scope, name))
        elif kind == "VarDecl" and not excluded(name):
            counts[name] += 1
            if "MulticastAction" in child.get("type", {}).get("qualType", ""):
                events.add((scope, name))


def header_surface(sharp_root: Path) -> tuple[dict[str, Counter], set, set]:
    members: dict[str, Counter] = {}
    events: set = set()
    noexcepts: set = set()
    # One TU over every public header. The filter dumps each decl whose qualified name matches and
    # does not descend into it, so a record arrives with its nested records inside it, and the
    # nested `...::Input::Touch` namespace arrives as one NamespaceDecl holding its records.
    umbrella = "\n".join(f'#include "{header}"' for header in sorted(HEADER_ROOT.rglob("*.hpp")))
    command = ["clang++", "-std=c++23", "-DCNA_RENDERER_HEADLESS", "-fsyntax-only",
               "-x", "c++", "-Xclang", "-ast-dump=json",
               "-Xclang", "-ast-dump-filter=" + NAMESPACES[0] + "::",
               *includes(sharp_root), "-"]
    result = subprocess.run(command, input=umbrella, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("clang could not parse the public Input headers:\n"
                           + result.stderr[-2000:])

    def visit(node: dict, inside_touch: bool) -> None:
        kind = node.get("kind")
        if kind == "NamespaceDecl":
            if node.get("name") not in ("Touch", "Input", "Framework", "Xna", "Microsoft"):
                return  # detail/internal namespaces are not public surface
            for child in node.get("inner", []):
                visit(child, inside_touch or node.get("name") == "Touch")
        elif kind == "CXXRecordDecl" and node.get("completeDefinition") and node.get("name"):
            if node["name"] not in members:
                collect(node, node["name"], members, events, noexcepts)

    for node in json_objects(result.stdout):
        visit(node, False)
    return members, events, noexcepts


def freeze_surface(text: str) -> tuple[Counter, Counter, set, set]:
    code = re.sub(r"//[^\n]*", "", text)
    member = r"((?:[A-Za-z_]\w*::)+(?:operator\s*(?:\[\]|\(\)|[^\s(),;\]]+)|~?\w+))"
    # A member pointer (`&Type::name`) pins a method, operator or static member.
    pointers = Counter(re.sub(r"\s+", "", key)
                       for key in re.findall(r"&\s*" + member + r"\s*[)\];,]", code))
    # `decltype(Type::name)` pins a static data member's or event's exact type.
    typed = re.findall(r"decltype\(\s*([\w:]+)\s*\)", code)
    pointers.update(typed)
    # An assignability trait pins a declared operator=.
    for match in re.finditer(r"std::is_(?:nothrow_|trivially_)?(?:copy_|move_)?assignable_v<\s*([\w:]+)", code):
        pointers[match.group(1) + "::operator="] += 1
    constructors = Counter()
    for match in re.finditer(r"std::is_(?:default_|nothrow_)?constructible_v<\s*([\w:]+)", code):
        constructors[match.group(1)] += 1
    events = set(re.findall(r"std::is_same_v<\s*decltype\(\s*([\w:]+)\s*\)", code))
    # `noexcept(std::declval<Type&>().name(...))` or `noexcept(Type::name(...))` pins noexcept;
    # a nothrow assignability trait pins it for operator=.
    noexcepts = {f"{owner}::{name}" for owner, name in re.findall(
        r"noexcept\(\s*std::declval<\s*(?:const\s+)?([\w:]+)\s*&*\s*>\(\)\.(\w+)", code)}
    noexcepts |= {f"{owner}::{name}" for owner, name in re.findall(
        r"noexcept\(\s*([\w:]+)::(\w+)\(", code)}
    noexcepts |= {f"{owner}::operator=" for owner in re.findall(
        r"std::is_nothrow_(?:copy_|move_)?assignable_v<\s*([\w:]+)", code)}
    return pointers, constructors, events, noexcepts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sharp-runtime", type=Path, default=REPO.parent / "sharp-runtime",
                        help="sharp-runtime checkout providing System/* headers")
    parser.add_argument("--freeze", type=Path, default=FREEZE,
                        help="the signature-freeze translation unit to measure")
    arguments = parser.parse_args()
    if shutil.which("clang++") is None:
        print("signature-freeze completeness: skipped, clang++ is not installed", file=sys.stderr)
        return 77
    if not arguments.sharp_runtime.is_dir():
        print(f"signature-freeze completeness: no sharp-runtime at {arguments.sharp_runtime}",
              file=sys.stderr)
        return 2
    try:
        members, events, noexcepts = header_surface(arguments.sharp_runtime)
    except RuntimeError as error:
        print(f"signature-freeze completeness: {error}", file=sys.stderr)
        return 2
    pointers, constructors, frozen_events, frozen_noexcepts = freeze_surface(
        arguments.freeze.read_text(encoding="utf-8"))

    missing = []
    for scope, counts in sorted(members.items()):
        for name, declared in sorted(counts.items()):
            if name == "<ctor>":
                pinned = constructors[scope]
                label = f"{scope} constructors"
            else:
                pinned = pointers[f"{scope}::{name}"]
                label = f"{scope}::{name}"
            if pinned < declared:
                missing.append(f"  {label}: {declared} public declaration(s), {pinned} frozen")
    for scope, name in sorted(events):
        if f"{scope}::{name}" not in frozen_events:
            missing.append(f"  {scope}::{name}: event type not pinned by std::is_same_v<decltype(...)>")
    for scope, name in sorted(noexcepts):
        if f"{scope}::{name}" not in frozen_noexcepts:
            missing.append(f"  {scope}::{name}: noexcept not pinned by a noexcept(...) assertion")

    types = len(members)
    total = sum(sum(counts.values()) for counts in members.values())
    if missing:
        print(f"signature-freeze completeness: {len(missing)} gap(s) across {types} public Input "
              f"types ({total} declarations):")
        print("\n".join(missing))
        return 1
    print(f"signature-freeze completeness: {types} public Input types, {total} declarations, "
          "all frozen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
