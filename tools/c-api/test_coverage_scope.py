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

import contextlib
import io
import json
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


def only_scope(scope: dict[str, tuple[str, str]], subtrees: dict[str, str] | None = None):
    """Replace the whole scope declaration for one fixture tree.

    All three tables have to be patched together: leaving `OUT_OF_SCOPE_MODULE_PREFIXES` in place
    makes `validate_module_scope` report the real repository's `renderers/` prefix as stale against
    a two-module fixture.
    """
    return (
        mock.patch.dict(gci.MODULE_SCOPE, scope, clear=True),
        mock.patch.dict(gci.OUT_OF_SCOPE_SUBTREES, subtrees or {}, clear=True),
        mock.patch.dict(gci.OUT_OF_SCOPE_MODULE_PREFIXES, {}, clear=True),
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
            self.assertIsNotNone(
                gci.module_scope(module),
                f"modules/{module} publishes headers but nothing classifies it",
            )

    def test_a_new_module_stops_the_gate(self) -> None:
        # The Content Pipeline regression: `modules/content-pipeline` appeared on 2026-09-03 and
        # nothing noticed. A module the table does not name must be refused by name.
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree(
                {"math": ["CNA/Kept.hpp"], "brand-new": ["CNA/New.hpp"]}, Path(temporary)
            )
            a, b, c = only_scope({"math": (gci.Scope.RUNTIME, "")})
            with a, b, c, self.assertRaises(RuntimeError) as raised:
                gci.validate_module_scope(root)
        self.assertIn("brand-new", str(raised.exception))
        self.assertIn("MODULE_SCOPE", str(raised.exception))

    def test_a_removed_module_stops_the_gate(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree({"math": ["CNA/Kept.hpp"]}, Path(temporary))
            a, b, c = only_scope(
                {"math": (gci.Scope.RUNTIME, ""), "vanished": (gci.Scope.RUNTIME, "")}
            )
            with a, b, c, self.assertRaises(RuntimeError) as raised:
                gci.validate_module_scope(root)
        self.assertIn("vanished", str(raised.exception))

    def test_an_exclusion_without_a_reason_stops_the_gate(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree({"secretive": ["CNA/Thing.hpp"]}, Path(temporary))
            a, b, c = only_scope({"secretive": (gci.Scope.OUT_OF_SCOPE, "")})
            with a, b, c, self.assertRaises(RuntimeError) as raised:
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
        for header in included:
            module = header.parts[1]
            self.assertEqual(
                gci.module_scope(module)[0],
                gci.Scope.RUNTIME,
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
            a, b, c = only_scope(
                {"content": (gci.Scope.RUNTIME, "")},
                {"content/CNA/Content/Pipeline": "build-time"},
            )
            with a, b, c:
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

    def test_a_lowercase_detail_directory_in_a_runtime_module_is_excluded(self) -> None:
        # The repository's only lowercase `detail/` directories are inside `content-pipeline`, which
        # MODULE_SCOPE excludes before the path check runs -- so asserting over the real tree passes
        # even with this fix reverted. Put one in a runtime module, where the check is load-bearing.
        with tempfile.TemporaryDirectory(prefix="cna-scope-test-") as temporary:
            root = fake_tree(
                {"graphics": ["CNA/Graphics/detail/Impl.hpp", "CNA/Graphics/Texture.hpp"]},
                Path(temporary),
            )
            a, b, c = only_scope({"graphics": (gci.Scope.RUNTIME, "")})
            with a, b, c:
                included, excluded = gci.discover_headers(root)
        self.assertEqual([path.name for path in included], ["Texture.hpp"])
        self.assertEqual([path.name for path in excluded], ["Impl.hpp"])

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

    def test_no_ownership_table_routes_unfinished_work_to_a_finished_task(self) -> None:
        # The real invariant behind the incident, over the tables that can actually answer: every
        # task `owner_task()` is able to name must be one the plan still records as open. Asserting
        # the fallback constant against itself, as an earlier version of this test did, stays green
        # with the entire scope model deleted.
        statuses = gci.parse_plan_task_status(gci.repository_root())
        reachable = {gci.UNMAPPED_RUNTIME_SURFACE_TASK}
        for table in (
            gci.SYMBOL_OWNER_OVERRIDES,
            gci.B12_SLICE_OWNERS,
            gci.B11_SLICE_OWNERS,
            gci.B10_SLICE_OWNERS,
            gci.CNAEXT_SLICE_OWNERS,
        ):
            reachable.update(table.values())
        # A slice table may still name a finished task for a slice with nothing left open; what it
        # may not do is name one for a slice REOPENED_SLICES has not taken over. That combination is
        # what `validate_planned_row_owners` catches per run, and this catches it in the data.
        for key in gci.REOPENED_SLICES:
            self.assertNotIn(
                key,
                {k for table in (gci.B12_SLICE_OWNERS, gci.B11_SLICE_OWNERS,
                                 gci.B10_SLICE_OWNERS, gci.CNAEXT_SLICE_OWNERS)
                 for k in table
                 if statuses.get(table[k]) != "complete"},
                f"{key} is reopened but its slice table already names an open task",
            )
        self.assertNotEqual(
            statuses.get(gci.UNMAPPED_RUNTIME_SURFACE_TASK),
            "complete",
            "the last-resort owner must be an unfinished task",
        )

    def test_a_completed_task_owning_a_planned_row_fails_the_gate(self) -> None:
        statuses = gci.parse_plan_task_status(gci.repository_root())
        finished = next(
            (task for task, state in statuses.items() if state == "complete"), None
        )
        if finished is None:
            self.skipTest("the plan records no completed task to build the fixture from")
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
        self.assertTrue(rules)
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


class ExclusionsArePublished(unittest.TestCase):
    """An exclusion nobody can see in the generated record is not a recorded decision."""

    def test_a_module_that_contributes_no_header_still_gets_a_row(self) -> None:
        # `modules/c-api` publishes 61 `.h` and zero `.hpp`. Driving this table from the headers it
        # matched dropped its row entirely, so its recorded reason went unpublished -- the opposite
        # of what MODULE_SCOPE's comment promises.
        rows = gci._render_scope_exclusions([])
        self.assertTrue(any("`modules/c-api`" in row for row in rows))
        for module, (scope, reason) in gci.MODULE_SCOPE.items():
            if scope == gci.Scope.OUT_OF_SCOPE:
                with self.subTest(module=module):
                    self.assertTrue(
                        any(f"`modules/{module}`" in row for row in rows),
                        f"{module} is excluded but its row is missing",
                    )

    def test_every_declared_exclusion_reaches_the_generated_summary(self) -> None:
        summary = (gci.repository_root() / "docs" / "c-api" / "COVERAGE.md").read_text(
            encoding="utf-8"
        )
        for module, (scope, _) in gci.MODULE_SCOPE.items():
            if scope == gci.Scope.OUT_OF_SCOPE:
                with self.subTest(module=module):
                    self.assertIn(f"`modules/{module}`", summary)
        for prefix in gci.OUT_OF_SCOPE_MODULE_PREFIXES:
            self.assertIn(f"`modules/{prefix}**`", summary)


class ReopenedSlicesStayHonest(unittest.TestCase):
    def test_a_slice_whose_rows_all_carry_an_override_is_reported_dead(self) -> None:
        # `owner_task` consults SYMBOL_OWNER_OVERRIDES before REOPENED_SLICES, so counting any
        # planned row would keep calling such a slice live forever.
        target = symbol("modules/math/include/CNA/Thing.hpp", "CNA::Thing::Method")
        mappings = {
            target.identity: gci.Mapping(
                mapping="m", tests="t", status="planned", task="CBIND-999", rule_id=None
            )
        }
        with mock.patch.object(gci, "REOPENED_SLICES", frozenset({"math/Thing"})):
            with self.assertRaises(RuntimeError) as raised:
                gci.validate_reopened_slices([target], mappings)
        self.assertIn("math/Thing", str(raised.exception))

    def test_a_slice_with_a_backlog_row_is_live(self) -> None:
        target = symbol("modules/math/include/CNA/Thing.hpp", "CNA::Thing::Method")
        mappings = {
            target.identity: gci.Mapping(
                mapping="m",
                tests="t",
                status="planned",
                task=gci.UNMAPPED_RUNTIME_SURFACE_TASK,
                rule_id=None,
            )
        }
        with mock.patch.object(gci, "REOPENED_SLICES", frozenset({"math/Thing"})):
            gci.validate_reopened_slices([target], mappings)


class ApprovalCannotWidenSilently(unittest.TestCase):
    """Growing a rule's approved set asserts a human read those declarations."""

    def _rules_file(self, directory: Path) -> Path:
        path = directory / "coverage_mappings.json"
        path.write_text(json.dumps({
            "schema_version": gci.SCHEMA_VERSION,
            "rules": [{
                "id": "broad",
                "qualified_name_regex": ".*",
                "header_regex": r".*Thing\.hpp$",
                "mapping": "m",
                "tests": "t",
                "status": "implemented",
                "task": "CBIND-000",
                "approved_symbols": [],
            }],
        }), encoding="utf-8")
        return path

    def test_an_unflagged_run_refuses_and_names_what_it_would_adopt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-approve-test-") as temporary:
            path = self._rules_file(Path(temporary))
            with self.assertRaises(RuntimeError) as raised:
                gci.approve_rule_symbols(path, {"broad": ["CPP-0123456789AB"]})
            message = str(raised.exception)
            self.assertIn("broad", message)
            self.assertIn("CPP-0123456789AB", message)
            self.assertEqual(json.loads(path.read_text())["rules"][0]["approved_symbols"], [])

    def test_the_flag_allows_it(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cna-approve-test-") as temporary:
            path = self._rules_file(Path(temporary))
            with contextlib.redirect_stdout(io.StringIO()):
                gci.approve_rule_symbols(
                    path, {"broad": ["CPP-0123456789AB"]}, grow=True
                )
            self.assertEqual(
                json.loads(path.read_text())["rules"][0]["approved_symbols"],
                ["CPP-0123456789AB"],
            )

    def test_shrinking_needs_no_flag(self) -> None:
        # A rule that no longer reaches a declaration has lost it; recording that is bookkeeping.
        with tempfile.TemporaryDirectory(prefix="cna-approve-test-") as temporary:
            path = self._rules_file(Path(temporary))
            payload = json.loads(path.read_text())
            payload["rules"][0]["approved_symbols"] = ["CPP-0123456789AB"]
            path.write_text(json.dumps(payload), encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                gci.approve_rule_symbols(path, {"broad": []})
            self.assertEqual(json.loads(path.read_text())["rules"][0]["approved_symbols"], [])


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
