#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL

"""Partition the C API coverage backlog into categories that mean something.

Read-only. This is **not** a gate and nothing depends on it; it exists so the numbers in
``docs/c-api/COVERAGE_AUDIT.md`` can be reproduced from the tree instead of trusted.

The question it answers is the one ``generate_coverage_inventory.py`` deliberately does not:
a ``planned`` row means *no rule claims this symbol*, which is not the same as *no C route
exists*. Conflating the two is how a backlog of 3,895 rows reads as 3,895 missing bindings.

Categories, and the evidence each rests on:

  H1  already bound          an exported ``cna_*`` route in the symbol's own family answers it
  H2  genuinely missing      no exported route family for the type, or none matching the member
  H3  no C form              C++ value semantics, a POD's Dispose, or the build-time pipeline
  H4  tooling artifact       the symbol should never have entered the inventory
  H5  test seam              a friend/test-access/``*Internal`` declaration, not public behaviour
  H6  unowned scope          in the inventory by accident; no task claims it
  H7  unknown                evidence genuinely insufficient

**How wrong the export probe is, measured.** CBIND-126 verified all 83 of the rows this tool put in
H1 against the public C headers and the route implementations: **41 were genuinely bound and 42 were
not** -- 38 had no route at all and 4 were C++ move constructors. The probe matches a name token
inside the type's own route family and never checks whether the route carries the arguments the
overload is *about*, so a typed transfer whose C route takes only ``CNA_Color*`` reads as answered
by the route that shares its name. Treat H1 as *candidates to verify*, never as a count of rows that
need no work; H2 is correspondingly a lower bound. The verdicts themselves now live in
``tools/c-api/coverage_mappings.json``, which is evidence rather than inference.

The export probe is a heuristic and is documented as one. It was calibrated against a case the
plan already recorded an answer for -- ``CBIND-080``, which ``plans/plan_binding.md`` records as
"it needed no new C code" -- and it independently returns zero genuinely-missing rows for that
task. Three naming conventions had to be taught to it before it did: ``Dispose`` is bound as
``_destroy``, digits break naive snake_case (``RenderTarget2D`` -> ``render_target2d``), and a few
derived types are reached through a base handle on purpose (``CNA_IndexBufferCreateInfo.dynamic``
constructs a ``DynamicIndexBuffer``). Verdicts that rest on that last rule are reported separately
rather than folded into H1, because they are rulings rather than measurements.

Usage:
    python3 tools/c-api/audit_coverage_backlog.py            # full partition
    python3 tools/c-api/audit_coverage_backlog.py --missing  # list the genuinely missing APIs
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_coverage_inventory as gci  # noqa: E402


# Derived types the C API reaches through a base handle by documented design, rather than by
# omission. Each was confirmed in the public C headers, not guessed:
# CNA_IndexBufferCreateInfo/CNA_VertexBufferCreateInfo carry a `dynamic` flag, and
# cna_render_target_destroy releases either render-target kind.
BASE_HANDLED = {
    "DynamicIndexBuffer": ["IndexBuffer"],
    "DynamicVertexBuffer": ["VertexBuffer"],
    "RenderTarget2D": ["Texture2D", "RenderTarget"],
    "RenderTargetCube": ["TextureCube", "RenderTarget"],
}
# Types whose base family exists but which have no C presence of their own. Crediting these to the
# base would assert a C caller can create one, and none can.
BASE_NOT_HANDLED = {"StorageTexture2D", "Texture2DArray", "Texture3D"}

LIFE_VERBS = ("create", "destroy", "release", "clone")
TEST_SEAM = re.compile(r"(TestPeer|TestAccess|TestHook|Peer)$|Internal$")
TEMPLATE_ARGS = re.compile(r"<[^<>]*(?:<[^<>]*>[^<>]*)*>")

H1 = "H1  already bound; only the mapping rule is missing"
H2 = "H2  genuinely missing C binding"
H3_PIPE = "H3  build-time-only Content Pipeline (the C API does not link it)"
H3_VALUE = "H3  C++ value semantics (operator=, copy ctor); no C form"
H3_POD = "H3  POD bound by _init; Dispose has no C form"
H4 = "H4  tooling artifact: lowercase detail/ escapes the exclusion list"
H5 = "H5  test peer / friend / *Internal seam; no public behaviour"
H6 = "H6  Microsoft::Phone; in scope by accident, owned by no task"
H7 = "H7  unknown"

ORDER = [H3_PIPE, H2, H4, H1, H6, H3_VALUE, H5, H3_POD, H7]


def name_variants(name: str) -> list[str]:
    """Every plausible C spelling of a C++ type or member name."""
    a = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    a = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", a).lower()
    b = re.sub(r"_([0-9])_?([a-z])\b", r"\1\2", a)
    c = re.sub(r"([a-z])([0-9])", r"\1_\2", b)
    return list(dict.fromkeys([a, b, c, name.lower()]))


def collapse_template(qualified_name: str) -> str:
    previous = None
    while previous != qualified_name:
        previous, qualified_name = qualified_name, TEMPLATE_ARGS.sub("<>", qualified_name)
    return qualified_name


def logical_identity(symbol: gci.Symbol) -> str:
    """One callable thing a binder would design a route for.

    Overloads of a name, specializations of a template and a get/set property pair are each one
    logical API, however many declarations Doxygen reports for them.
    """
    qualified = collapse_template(symbol.qualified_name)
    scope, _, member = qualified.rpartition("::")
    match = re.match(r"^(get|set|is)([A-Z].*?)(Property)?$", member)
    if match and scope:
        member = "<prop>" + match.group(2)
    return f"{scope}::{member}" if scope else member


def module_of(symbol: gci.Symbol) -> str:
    return symbol.header.split("/")[1]


def is_build_time_pipeline(symbol: gci.Symbol) -> bool:
    module = module_of(symbol)
    if module == "content-pipeline":
        return True
    return module == "content" and ("Pipeline" in symbol.header or "Import" in symbol.header)


class ExportProbe:
    def __init__(self, exports: list[str]) -> None:
        self.exports = sorted(exports)
        self.export_set = set(exports)

    def family(self, type_name: str) -> list[tuple[str, str]]:
        """Exports whose name begins with this type's route prefix, with the remainder."""
        found: list[tuple[str, str]] = []
        for variant in name_variants(type_name):
            prefix = f"cna_{variant}_"
            found += [(e, e[len(prefix):]) for e in self.exports if e.startswith(prefix)]
        return found

    def lifetime(self, type_name: str) -> list[str]:
        return [
            f"cna_{variant}_{verb}"
            for variant in name_variants(type_name)
            for verb in LIFE_VERBS
            if f"cna_{variant}_{verb}" in self.export_set
        ]

    def classify(self, symbol: gci.Symbol) -> tuple[str, str]:
        scope, _, member = symbol.qualified_name.rpartition("::")
        short = scope.rsplit("::", 1)[-1] if scope else ""
        bare = member.lstrip("~")

        if TEST_SEAM.search(bare):
            return H5, "friend / test-access / *Internal declaration"
        if bare == "operator=":
            return H3_VALUE, "C++ assignment operator; the C ABI has no assignment form"
        if symbol.kind == "constructor" and short and re.search(
            rf"const\s+{re.escape(short)}\s*&(?!&)", symbol.signature
        ):
            return H3_VALUE, "C++ copy constructor; C copies through explicit routes"
        # CBIND-126: the move constructor was missed, because only the `const T&` spelling was
        # tested. It went to the lifetime branch instead, where a create/destroy route exists for
        # the type and so it read as already bound -- four rows of pure C++ value semantics counted
        # as C coverage.
        if symbol.kind == "constructor" and short and re.search(
            rf"{re.escape(short)}\s*&&", symbol.signature
        ):
            return H3_VALUE, "C++ move constructor; a C handle moves by assignment"
        if not short:
            return H7, "free declaration with no enclosing type"

        property_match = re.match(r"^(get|set|is)([A-Z].*?)(Property)?$", bare)
        tokens = name_variants(property_match.group(2) if property_match else bare)

        if symbol.kind in ("constructor", "destructor") or bare == "Dispose":
            if hit := self.lifetime(short):
                return H1, f"exported lifetime route {hit[0]}"
            if init := [
                f"cna_{v}_init" for v in name_variants(short) if f"cna_{v}_init" in self.export_set
            ]:
                return H3_POD, f"bound as a POD initialised by {init[0]}; a POD has no create/destroy"
            for base in BASE_HANDLED.get(short, []):
                if hit := self.lifetime(base):
                    return H1, f"reached through the base handle by design ({hit[0]})"
            return H2, f"no create/destroy route for {short}"

        own = self.family(short)
        if hit := [e for e, rest in own if any(t in rest for t in tokens)]:
            return H1, f"exported route {hit[0]}"
        if not own and short not in BASE_NOT_HANDLED:
            for base in BASE_HANDLED.get(short, []):
                if hit := [e for e, rest in self.family(base) if any(t in rest for t in tokens)]:
                    return H1, f"answered through the base handle by design ({hit[0]})"
        if not own:
            return H2, f"no exported route family for {short} at all"
        return H2, f"{short} has {len(own)} exported routes but none answers {tokens[0]}"


def categorize(symbol: gci.Symbol, probe: ExportProbe) -> tuple[str, str]:
    if "/detail/" in symbol.header:
        return H4, "lowercase detail/ directory; EXCLUDED_PATH_SEGMENTS only matches 'Detail'"
    if is_build_time_pipeline(symbol):
        return H3_PIPE, "build-time-only module; not in the C API link closure"
    if module_of(symbol) == "phone":
        return H6, "Microsoft::Phone is not XNA and owner_task() has no rule for it"
    return probe.classify(symbol)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--missing", action="store_true",
                        help="list the genuinely missing logical APIs instead of the summary")
    arguments = parser.parse_args()

    root = gci.repository_root()
    headers, _excluded = gci.discover_headers(root)
    rules = gci.load_rules(root / "tools" / "c-api" / "coverage_mappings.json")
    with tempfile.TemporaryDirectory(prefix="cna-coverage-audit-") as temporary:
        xml_directory = gci.run_doxygen(root, headers, Path(temporary))
        symbols = gci.parse_symbols(root, xml_directory, headers)
    mappings = gci.map_symbols(symbols, rules)

    baseline = json.loads((root / "tools" / "c-api" / "abi_baseline.json").read_text())
    probe = ExportProbe(baseline["exports"])

    planned = [s for s in symbols if mappings[s.identity].status == "planned"]
    plan_status = gci.parse_plan_task_status(root)

    rows: defaultdict[str, list[gci.Symbol]] = defaultdict(list)
    reasons: dict[str, str] = {}
    for symbol in planned:
        category, why = categorize(symbol, probe)
        rows[category].append(symbol)
        reasons.setdefault(category, why)

    if arguments.missing:
        by_logical: dict[str, list[gci.Symbol]] = defaultdict(list)
        for symbol in rows[H2]:
            by_logical[logical_identity(symbol)].append(symbol)
        print(f"{len(rows[H2])} rows -> {len(by_logical)} genuinely missing logical APIs\n")
        for identity in sorted(by_logical):
            group = by_logical[identity]
            task = mappings[group[0].identity].task
            print(f"{task:12} {len(group):3} row(s)  {identity}")
        return 0

    print(f"public C++ symbols     : {len(symbols)}")
    print(f"planned rows           : {len(planned)}")
    complete = {t for t, s in plan_status.items() if s == "complete"}
    owned = [s for s in planned if mappings[s.identity].task in complete]
    print(f"  owned by ✅ tasks    : {len(owned)}")
    print(f"  owned by open tasks  : {len(planned) - len(owned)}")

    print(f"\n{'category':64}{'rows':>7}{'logical':>9}")
    for category in ORDER:
        group = rows.get(category)
        if not group:
            continue
        print(f"{category:64}{len(group):>7}{len({logical_identity(s) for s in group}):>9}")
    print(f"{'TOTAL':64}{len(planned):>7}")

    print(f"\n{'category':64}{'rows':>7}{'logical':>9}   (rows owned by ✅ tasks only)")
    owned_ids = {s.identity for s in owned}
    for category in ORDER:
        group = [s for s in rows.get(category, []) if s.identity in owned_ids]
        if not group:
            continue
        print(f"{category:64}{len(group):>7}{len({logical_identity(s) for s in group}):>9}")
    print(f"{'TOTAL':64}{len(owned):>7}")

    print("\nownership of planned rows, by task:")
    per_task = Counter(mappings[s.identity].task for s in planned)
    for task, count in per_task.most_common():
        mark = {"complete": "✅", "in progress": "🟨", "not started": "⬜"}.get(
            plan_status.get(task, ""), "??")
        print(f"  {task:12} {mark}  {count:5} row(s)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"coverage backlog audit error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
