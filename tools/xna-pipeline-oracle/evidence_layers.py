#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Shared, conservative source and CTest evidence layers for presence audits."""
from __future__ import annotations

import fnmatch
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "evidence"))
import ctest_evidence


def source_text(text: str, *, cmake: bool = False) -> str:
    """Remove comments/literals and literal false branches; unknown conditions stay unknown.

    This is a lexical convenience, never a preprocessor or configured-registration proof.
    CMake quoted arguments stay intact because add_test names may be quoted.
    """
    if cmake:
        text = re.sub(r'(?s)\#\[(=*)\[.*?\]\1\]|"(?:\\.|[^"\\])*"|#[^\n]*',
                      lambda m: m[0] if m[0].startswith('"') else "\n" * m[0].count("\n"), text)
        directive = re.compile(r'^\s*(if|elseif|else|endif)\s*\((.*?)\)\s*$', re.I)
    else:
        text = re.sub(r'\\\r?\n', '', text)
        tokens = r'R"([^\s()\\]{0,16})\(.*?\)\1"|/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27'
        text = re.sub(tokens, lambda m: " " + "\n" * m[0].count("\n"), text, flags=re.S)
        directive = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
    active, stack, lines = True, [], []
    for line in text.splitlines(keepends=True):
        match = directive.match(line.strip())
        if match:
            kind, expression = match.groups()
            kind = kind.lower()
            expression = expression.strip().strip('()').strip().upper()
            value = (False if expression in (("0", "FALSE", "OFF") if cmake else ("0",)) else
                     True if expression in (("1", "TRUE", "ON") if cmake else ("1",)) else None)
            if kind in ("if", "ifdef", "ifndef"):
                stack.append([active, value])
                active = active and value is not False
            elif stack and kind in ("elif", "elseif", "else"):
                parent, taken = stack[-1]
                branch = True if kind == "else" else value
                active = parent and taken is not True and branch is not False
                stack[-1][1] = True if taken is True or branch is True else None if taken is None or branch is None else False
            elif stack and kind == "endif":
                active = stack.pop()[0]
            lines.append("\n")
        else:
            lines.append(line if active else "\n")
    return "".join(lines)


def add_arguments(parser):
    parser.add_argument("--configured-inventory", type=Path,
                        help="CTest -N --show-only=json-v1 output; registration only")
    parser.add_argument("--execution-record", type=Path, action="append", default=[],
                        help="retained ctest_evidence.py record.json; repeat for required selections")
    parser.add_argument("--require-execution", action="store_true",
                        help="fail unless every required witness has validated execution evidence; not assertion qualification")
    parser.add_argument("--evidence-report", type=Path, help="write separate JSON layers (never a runtime coverage percentage)")


def _enabled(test):
    return not any(prop.get("name") == "DISABLED" and prop.get("value") not in (False, "FALSE", "OFF", "0", 0)
                   for prop in test.get("properties", []))


def _candidate(test, name):
    if test["name"] == name:
        return True
    for argument in test.get("command", []):
        if argument.startswith("--gtest_filter="):
            positive, _, negative = argument.split("=", 1)[1].partition("-")
            return (any(fnmatch.fnmatchcase(name, pattern) for pattern in positive.split(":")) and
                    not any(fnmatch.fnmatchcase(name, pattern) for pattern in negative.split(":") if pattern))
    return False


def execution_layers(required: dict[str, bool], args) -> dict:
    """Join exact required names to validated captures, never to source name matches.

    The bool marks witnesses requiring runtime-kind evidence. Aggregate gtest commands
    additionally need RUN/OK output for the exact case; even this cannot count assertions.
    """
    configured, captures, errors, identities, retained = [], [], [], [], []
    if args.configured_inventory:
        try:
            configured = ctest_evidence.inventory(args.configured_inventory)
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            errors.append(f"configured inventory: {error}")
    explicit = list(configured)
    for path in args.execution_record:
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
            result = ctest_evidence.validate(record)
            if result["outcome"] != "pass":
                errors.append(f"{path}: {result['outcome']}")
                continue
            inventory = ctest_evidence.inventory(Path(record["artifacts"]["configured"]["path"]))
            if captures and inventory != captures[0][1]:
                errors.append(f"{path}: configured inventories differ between captures")
            retained.append({"record": str(path.resolve()), "sha256": ctest_evidence.file_sha256(path),
                             "evidence_kind": record["evidence_kind"], "selection": record["selection"],
                             "outcome": result["outcome"], "executed": result["executed"]})
            identity = {key: record[key] for key in ("source", "build")}
            identity["configuration"] = record["selection"]["config"]
            if identities and identity != identities[0]:
                errors.append(f"{path}: source/build identity differs between captures")
            identities.append(identity)
            configured.extend(inventory)
            captures.append((record, inventory, result))
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            errors.append(f"{path}: {error}")
    # An independently supplied inventory must not be mixed with another configuration.
    if args.configured_inventory and captures:
        for _, inventory, _ in captures:
            if explicit != inventory:
                errors.append("explicit inventory differs from retained configured inventory")
                break
    registration, executed = {}, {}
    for name, runtime in required.items():
        registration[name] = sorted({test["name"] for test in configured if _enabled(test) and _candidate(test, name)})
        witnesses = []
        for record, inventory, result in captures:
            if runtime and record["evidence_kind"] != "runtime":
                continue
            enabled = {test["name"]: test for test in inventory if _enabled(test) and not
                       (runtime and any(prop.get("name") == "LABELS" and
                                        {"metadata", "declaration-presence"}.intersection(prop.get("value", []))
                                        for prop in test.get("properties", [])))}
            for case in result["tests"]:
                test = enabled.get(case["name"])
                if not test or case["outcome"] != "passed" or not _candidate(test, name):
                    continue
                output = case["output"]
                if re.search(r'\b(?:Running 0 tests|0 tests? (?:from .* )?ran)\b', output):
                    continue
                # A gtest filter can select zero cases even when the enclosing CTest passes.
                if "--gtest_filter=" in " ".join(test.get("command", [])) or case["name"] != name:
                    if not (re.search(r'^\[ RUN\s*\]\s+' + re.escape(name) + r'\s*$', output, re.M) and
                            re.search(r'^\[\s*OK\s*\]\s+' + re.escape(name) + r'(?:\s|$)', output, re.M)):
                        continue
                witnesses.append(case["name"])
        executed[name] = sorted(set(witnesses))
    return {
        "configured_registration": {"status": "observed" if configured else "not-established",
                                    "candidates": registration,
                                    "missing_or_disabled": [name for name in required if not registration[name]]},
        "actual_execution": {"status": "observed" if required and not errors and all(executed.values()) else "not-established",
                             "witnesses": executed, "missing": [name for name in required if not executed[name]],
                             "errors": errors, "captures": identities, "records": retained},
        "runtime_qualification": {"status": "not-established", "executed_assertions": None,
                                  "reason": "CTest counts test entries, not assertions, route calls or semantic acceptance. Source presence and execution are separate layers."},
    }


def report_layers(declaration, required, args):
    layers = {"declaration_presence": declaration, **execution_layers(required, args)}
    if args.evidence_report:
        args.evidence_report.write_text(json.dumps(layers, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"configured registration: {layers['configured_registration']['status']}; "
          f"actual execution: {layers['actual_execution']['status']}; "
          "runtime qualification: not-established (assertions unmeasured)")
    if args.require_execution and layers["actual_execution"]["status"] != "observed":
        print("execution requirement unmet: " + ", ".join(layers["actual_execution"]["missing"]))
        for error in layers["actual_execution"]["errors"]:
            print("  " + error)
    return layers
