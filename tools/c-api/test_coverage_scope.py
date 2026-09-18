#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL

"""Regression tests for the C API coverage scope, ownership and rule-resolution model.

Every test here exists because the corresponding defect actually shipped and stayed in the tree
long enough to be believed (`plans/plan_binding.md` CBIND-126):

* the Content Pipeline became its own module and 2,554 declarations silently became the property of
  a task closed on 2026-08-16;
* `modules/phone` was created and did the same with 57 more;
* `EXCLUDED_PATH_SEGMENTS` matched `Detail` but not the C++-idiomatic `detail`, so 316
  implementation-detail declarations entered the public matrix;
* `--approve-rule-symbols` rejected 34 rule pairs over 296 symbols whose ownership was already
  recorded, because it read the patterns and discarded `approved_symbols`.

These are deliberately fixture tests over small, hand-built inputs rather than a snapshot of the
repository: they must fail for the reason named in the test, and they must run without Doxygen. The
few that do read the repository assert an invariant, not a count, so they do not go stale every time
a header moves.

Run directly (`python3 tools/c-api/test_coverage_scope.py`) or through the CTest gate
`CApiCoverageScopeModel`.
"""

from __future__ import annotations

import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_coverage_inventory as gci  # noqa: E402


def symbol(
    header: str,
    qualified_name: str,
    *,
    kind: str = "method",
    signature: str = "()",
    access: str = "public",
) -> gci.Symbol:
    return gci.Symbol(
        header=header,
        line=1,
        kind=kind,
        qualified_name=qualified_name,
        signature=signature,
        display=f"`{qualified_name}`",
        access=access,
        type_text="void",
    )


def rule(
    rule_id: str,
    qualified_name_regex: str,
    approved: list[str],
    *,
    header_regex: str | None = None,
    kinds: tuple[str, ...] = (),
    status: str = "implemented",
    task: str = "CBIND-000",
) -> gci.Rule:
    return gci.Rule(
        rule_id=rule_id,
        qualified_name=re.compile(qualified_name_regex),
        signature=None,
        header=re.compile(header_regex) if header_regex else None,
        kinds=frozenset(kinds),
        mapping="mapping",
        tests="tests",
        status=status,
        task=task,
        approved_symbols=frozenset(approved),
    )


def fake_tree(modules: dict[str, list[str]], root: Path) -> Path:
    """Write a throwaway `modules/<name>/include/<path>` tree and return its root."""
    for name, headers in modules.items():
        for header in headers:
            path = root / "modules" / name / "include" / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("#pragma once\n", encoding="utf-8")
    return root


class ModuleScopeIsTotal(unittest.TestCase):
    """A module that nobody classified must stop the gate, not inherit a default."""

    def test_the_repository_classifies_every_publishing_module(self) -> None:
        root = gci.repository_root()
        gci.validate_module_scope(root)
        for module in gci.publishing_modules(root):
            self.assertIn(
                module,
                gci.MODULE_SCOPE,
                f"modules/{module} publishes headers but MODULE_SCOPE does not classify it",
            )

    def test_a_new_module_stops_the_gate(self) -> None:
        # The Content Pipeline regression: `modules/content-pipeline` appeared on 2026-09-03 and
        # nothing noticed. A module the table does not name must be refused by name.
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree(
                {"math": ["CNA/Kept.hpp"], "brand-new": ["CNA/New.hpp"]}, Path(temporary)
            )
            with mock.patch.dict(
                gci.MODULE_SCOPE, {"math": (gci.Scope.RUNTIME, "")}, clear=True
            ), mock.patch.dict(gci.OUT_OF_SCOPE_SUBTREES, {}, clear=True):
                with self.assertRaises(RuntimeError) as raised:
                    gci.validate_module_scope(root)
        self.assertIn("brand-new", str(raised.exception))
        self.assertIn("MODULE_SCOPE", str(raised.exception))

    def test_a_removed_module_stops_the_gate(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree({"math": ["CNA/Kept.hpp"]}, Path(temporary))
            with mock.patch.dict(
                gci.MODULE_SCOPE,
                {"math": (gci.Scope.RUNTIME, ""), "vanished": (gci.Scope.RUNTIME, "")},
                clear=True,
            ), mock.patch.dict(gci.OUT_OF_SCOPE_SUBTREES, {}, clear=True):
                with self.assertRaises(RuntimeError) as raised:
                    gci.validate_module_scope(root)
        self.assertIn("vanished", str(raised.exception))

    def test_an_exclusion_without_a_reason_stops_the_gate(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree({"secretive": ["CNA/Thing.hpp"]}, Path(temporary))
            with mock.patch.dict(
                gci.MODULE_SCOPE, {"secretive": (gci.Scope.OUT_OF_SCOPE, "")}, clear=True
            ), mock.patch.dict(gci.OUT_OF_SCOPE_SUBTREES, {}, clear=True):
                with self.assertRaises(RuntimeError) as raised:
                    gci.validate_module_scope(root)
        self.assertIn("without a recorded reason", str(raised.exception))


class OutOfScopeSurfaceStaysOut(unittest.TestCase):
    def test_the_build_time_pipeline_and_phone_are_declared_out_of_scope(self) -> None:
        for module in ("content-pipeline", "phone"):
            scope, reason = gci.MODULE_SCOPE[module]
            self.assertEqual(scope, gci.Scope.OUT_OF_SCOPE, module)
            self.assertIn("CBIND-117", reason, f"{module} must name the task that decided it")

    def test_discovery_admits_no_out_of_scope_header(self) -> None:
        included, excluded = gci.discover_headers(gci.repository_root())
        self.assertTrue(included)
        out_of_scope = {
            name for name, (scope, _) in gci.MODULE_SCOPE.items() if scope == gci.Scope.OUT_OF_SCOPE
        }
        for header in included:
            self.assertNotIn(
                header.parts[1],
                out_of_scope,
                f"{header} is in an out-of-scope module but entered the public inventory",
            )
            relative = Path(*header.parts[3:])
            self.assertIsNone(
                gci.out_of_scope_subtree(header.parts[1], relative),
                f"{header} is in an out-of-scope subtree but entered the public inventory",
            )
        self.assertTrue(excluded, "the exclusion list must not be silently empty")

    def test_an_out_of_scope_subtree_is_excluded_but_its_siblings_are_not(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree(
                {
                    "content": [
                        "CNA/Content/Pipeline/Compiler.hpp",
                        "CNA/Content/Loader.hpp",
                    ]
                },
                Path(temporary),
            )
            with mock.patch.dict(
                gci.MODULE_SCOPE, {"content": (gci.Scope.RUNTIME, "")}, clear=True
            ), mock.patch.dict(
                gci.OUT_OF_SCOPE_SUBTREES,
                {"content/CNA/Content/Pipeline": "build-time"},
                clear=True,
            ):
                included, excluded = gci.discover_headers(root)
        self.assertEqual([path.name for path in included], ["Loader.hpp"])
        self.assertEqual([path.name for path in excluded], ["Compiler.hpp"])


class InternalPathsAreExcludedInEveryCapitalization(unittest.TestCase):
    def test_detail_and_internal_match_regardless_of_case(self) -> None:
        for spelling in ("Detail", "detail", "DETAIL", "Internal", "internal", "INTERNAL"):
            with self.subTest(spelling=spelling):
                self.assertTrue(
                    gci.path_is_explicitly_internal(Path(f"CNA/Graphics/{spelling}/Thing.hpp")),
                    f"a {spelling}/ directory must never reach the public inventory",
                )

    def test_a_segment_that_merely_contains_the_word_is_public(self) -> None:
        # Narrowness matters: the rule is about a path segment, not a substring. `DetailLevel.hpp`
        # and a `Details/` directory are ordinary public API.
        for spelling in ("Details", "InternalFormat", "DetailLevel", "MyDetail"):
            with self.subTest(spelling=spelling):
                self.assertFalse(
                    gci.path_is_explicitly_internal(Path(f"CNA/Graphics/{spelling}/Thing.hpp"))
                )

    def test_the_repository_admits_no_lowercase_detail_header(self) -> None:
        included, _ = gci.discover_headers(gci.repository_root())
        for header in included:
            segments = {segment.lower() for segment in header.parts}
            self.assertNotIn("detail", segments, f"{header} is implementation detail")
            self.assertNotIn("internal", segments, f"{header} is implementation detail")


class UnfinishedWorkNamesAnUnfinishedTask(unittest.TestCase):
    """The CBIND-044 regression: `owner_task()` must not end at a completed task."""

    def test_the_last_resort_owner_is_not_a_completed_task(self) -> None:
        statuses = gci.parse_plan_task_status(gci.repository_root())
        self.assertIn(
            gci.UNMAPPED_RUNTIME_SURFACE_TASK,
            statuses,
            "the last-resort owner must have a row in the plan",
        )
        self.assertNotEqual(
            statuses[gci.UNMAPPED_RUNTIME_SURFACE_TASK],
            "complete",
            "a finished task cannot own declarations that nothing binds",
        )

    def test_an_unrecognised_runtime_module_lands_on_the_live_backlog(self) -> None:
        # Precisely the shape of the incident: a module `owner_task()` has never heard of.
        owner = gci.owner_task(
            symbol("modules/brand-new/include/CNA/Thing.hpp", "CNA::Thing::Method")
        )
        self.assertEqual(owner, gci.UNMAPPED_RUNTIME_SURFACE_TASK)
        statuses = gci.parse_plan_task_status(gci.repository_root())
        self.assertNotEqual(statuses.get(owner), "complete")

    def test_a_completed_task_owning_a_planned_row_fails_the_gate(self) -> None:
        statuses = gci.parse_plan_task_status(gci.repository_root())
        finished = next(task for task, state in statuses.items() if state == "complete")
        mappings = {
            "x": gci.Mapping(
                mapping="m", tests="t", status="planned", task=finished, rule_id=None
            )
        }
        with self.assertRaises(RuntimeError) as raised:
            gci.validate_planned_row_owners(gci.repository_root(), mappings)
        self.assertIn(finished, str(raised.exception))


class RuleOwnershipIsPerSymbol(unittest.TestCase):
    """Overlapping patterns are normal; overlapping *claims* are not."""

    def setUp(self) -> None:
        self.target = symbol("modules/math/include/CNA/Thing.hpp", "CNA::Thing::Method")
        self.contract = rule("whole-header", ".*", [], header_regex=r".*Thing\.hpp$")
        self.carve_out = rule("carve-out", r"^CNA::Thing::Method$", [])

    def _with(self, contract_approved: list[str], carve_approved: list[str]) -> list[gci.Rule]:
        return [
            rule("whole-header", ".*", contract_approved, header_regex=r".*Thing\.hpp$"),
            rule("carve-out", r"^CNA::Thing::Method$", carve_approved),
        ]

    def test_the_rule_that_approved_the_symbol_owns_it(self) -> None:
        for owner_index, owner_id in ((0, "whole-header"), (1, "carve-out")):
            with self.subTest(owner=owner_id):
                approvals: list[list[str]] = [[], []]
                approvals[owner_index] = [self.target.stable_id]
                rules = self._with(*approvals)
                for ignore in (False, True):
                    resolved = gci.resolve_rules(self.target, rules, ignore_approval=ignore)
                    self.assertEqual([entry.rule_id for entry in resolved], [owner_id])

    def test_an_overlap_nobody_approved_is_refused_by_name(self) -> None:
        rules = self._with([], [])
        with self.assertRaises(RuntimeError) as raised:
            gci.resolve_rules(self.target, rules, ignore_approval=True)
        message = str(raised.exception)
        self.assertIn("whole-header", message)
        self.assertIn("carve-out", message)

    def test_two_rules_approving_one_symbol_is_refused(self) -> None:
        rules = self._with([self.target.stable_id], [self.target.stable_id])
        for ignore in (False, True):
            with self.subTest(ignore_approval=ignore):
                with self.assertRaises(RuntimeError) as raised:
                    gci.resolve_rules(self.target, rules, ignore_approval=ignore)
                self.assertIn("approved by more than one", str(raised.exception))

    def test_rule_order_does_not_decide_ownership(self) -> None:
        forward = self._with([], [self.target.stable_id])
        resolved = gci.resolve_rules(self.target, list(reversed(forward)), ignore_approval=True)
        self.assertEqual([entry.rule_id for entry in resolved], ["carve-out"])


class AlreadyBoundApisCarryTheirMapping(unittest.TestCase):
    """Rows that an exported route already answers must not be reported as missing bindings."""

    def test_every_rule_names_a_mapping_a_task_and_evidence(self) -> None:
        rules = gci.load_rules(gci.repository_root() / "tools" / "c-api" / "coverage_mappings.json")
        self.assertGreater(len(rules), 500)
        for entry in rules:
            with self.subTest(rule=entry.rule_id):
                self.assertTrue(entry.mapping.strip(), "a rule must say what the C mapping is")
                self.assertTrue(entry.tests.strip(), "a rule must name its evidence")
                self.assertRegex(entry.task, r"^(CBIND|MOD)-")

    def test_the_recorded_dispositions_are_present_and_approved(self) -> None:
        # Workstreams F and G: declarations with no meaningful C form are classified explicitly,
        # never bound with a route invented to make a percentage green.
        rules = {
            entry.rule_id: entry
            for entry in gci.load_rules(
                gci.repository_root() / "tools" / "c-api" / "coverage_mappings.json"
            )
        }
        for rule_id in (
            "graphics-state-value-semantics",
            "graphics-state-copy-assignment",
            "graphics-resource-move-semantics",
            "vertex-declaration-value-semantics",
            "graphics-state-pod-dispose",
            "graphics-test-peer-friendship",
            "buffer-internal-set-data-helpers",
        ):
            with self.subTest(rule=rule_id):
                self.assertIn(rule_id, rules)
                self.assertEqual(rules[rule_id].status, "not-applicable")
                self.assertTrue(
                    rules[rule_id].approved_symbols,
                    "a disposition covers reviewed declarations or it covers nothing",
                )


class GeneratedDataCannotGoStaleUnnoticed(unittest.TestCase):
    def test_the_summary_carries_the_full_inventory_hash(self) -> None:
        summary = (gci.repository_root() / "docs" / "c-api" / "COVERAGE.md").read_text(
            encoding="utf-8"
        )
        self.assertRegex(summary, r"Full inventory SHA-256: `[0-9a-f]{64}`")
        self.assertIn("## Out of runtime C API scope", summary)

    def test_the_coverage_gate_is_registered_with_ctest(self) -> None:
        probes = (gci.repository_root() / "cmake" / "Tests" / "ModuleProbes.cmake").read_text(
            encoding="utf-8"
        )
        self.assertIn("CApiCoverageMatrix", probes)
        self.assertIn("CApiCoverageScopeModel", probes)


if __name__ == "__main__":
    unittest.main(verbosity=2)
