#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Retain and validate one CTest execution, without replacing existing reports."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
from build_provenance import file_sha256, git_bytes, read_cache

SCHEMA_VERSION = 1
STATES = ("passed", "failed", "skipped", "not_run")


def inventory(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("kind") != "ctestInfo" or data.get("version", {}).get("major") != 1:
        raise ValueError("expected CTest JSON inventory version 1")
    tests = data["tests"]
    names = [test["name"] for test in tests]
    if any(not isinstance(name, str) or not name for name in names) or len(set(names)) != len(names):
        raise ValueError("inventory has empty or duplicate test names")
    return tests


def junit_cases(path: Path) -> list[dict]:
    suite = ET.parse(path).getroot()
    if suite.tag != "testsuite":
        raise ValueError("expected CTest testsuite root")
    cases = []
    failures = errors = disabled = skipped = 0
    for case in suite.findall("testcase"):
        name, status = case.get("name"), case.get("status")
        duration = float(case.attrib["time"])
        if not name or not math.isfinite(duration) or duration < 0:
            raise ValueError("invalid testcase name or time")
        failure, error, skip = case.find("failure"), case.find("error"), case.find("skipped")
        if sum(item is not None for item in (failure, error, skip)) > 1:
            raise ValueError(f"conflicting result elements: {name}")
        failures += failure is not None
        errors += error is not None
        disabled += status == "disabled"
        skipped += skip is not None and status != "disabled"
        if status == "disabled":
            state = "not_run"
        elif skip is not None and status in ("run", "notrun"):
            state = "skipped"
        elif status == "notrun":
            state = "not_run"
        elif status == "fail" and (failure is not None or error is not None):
            state = "failed"
        elif status == "run" and failure is None and error is None:
            state = "passed"
        else:
            raise ValueError(f"unsupported or contradictory testcase status: {name}: {status}")
        reason_node = skip if skip is not None else failure if failure is not None else error
        reason = None
        if reason_node is not None:
            reason = " ".join(filter(None, [reason_node.get("message"), reason_node.text])).strip() or None
        output = case.findtext("system-out", "")
        cases.append({"name": name, "outcome": state, "seconds": duration,
                      "reason": reason, "output": output, "ctest_status": status})
    names = [case["name"] for case in cases]
    if len(names) != len(set(names)):
        raise ValueError("duplicate JUnit testcase names")
    expected = {"tests": len(cases), "failures": failures, "disabled": disabled, "skipped": skipped}
    if errors or "errors" in suite.attrib:
        expected["errors"] = errors
    for key, value in expected.items():
        if int(suite.attrib[key]) != value:
            raise ValueError(f"JUnit {key} count disagrees with testcase elements")
    return cases


def check_log(path: Path, cases: list[dict]) -> float:
    log = path.read_text(encoding="utf-8", errors="replace")
    if not re.search(r"^Test project .+$", log, re.M):
        raise ValueError("log has no Test project header")
    if not cases:
        if "No tests were found!!!" not in log:
            raise ValueError("empty selection lacks CTest no-tests diagnostic")
        return 0.0
    footer = re.findall(r"^Total Test time \(real\) =\s*(\d+(?:\.\d+)?) sec\s*$", log, re.M)
    summary = re.findall(r"^\d+% tests passed, (\d+) tests failed out of (\d+)\s*$", log, re.M)
    if len(footer) != 1 or len(summary) != 1:
        raise ValueError("log is missing a unique completion summary/time (possibly truncated)")
    failed = sum(case["outcome"] == "failed" or
                 (case["outcome"] == "not_run" and case["ctest_status"] != "disabled") for case in cases)
    if tuple(map(int, summary[0])) != (failed, len(cases)):
        raise ValueError("log summary disagrees with JUnit")
    rows = re.findall(r"^\s*(\d+)/(\d+)\s+Test\s+#\d+:\s+(.+?)\s+\.{2,}\s*(.+)$", log, re.M)
    if len(rows) != len(cases) or {int(row[0]) for row in rows} != set(range(1, len(cases) + 1)):
        raise ValueError("log result rows are missing or duplicated")
    by_name = {case["name"]: case for case in cases}
    seen = set()
    for _, total, name, result in rows:
        if name not in by_name or name in seen or int(total) != len(cases):
            raise ValueError("log result names/count disagree with JUnit")
        seen.add(name)
        state = ("not_run" if "***Not Run" in result else "skipped" if "***Skipped" in result
                 else "passed" if result.startswith("Passed") else "failed")
        if state != by_name[name]["outcome"]:
            raise ValueError(f"log result disagrees with JUnit: {name}")
    return float(footer[0])


def evaluate(configured: Path, selected: Path, junit: Path, log: Path, returncode: int | None) -> dict:
    result = {"outcome": "incomplete", "validation_errors": [], "configured": None,
              "selected": None, "executed": 0, **dict.fromkeys(STATES, 0), "tests": [],
              "ctest_wall_seconds": None}
    try:
        all_tests, selection = inventory(configured), inventory(selected)
        result.update(configured=len(all_tests), selected=len(selection))
        if not {test["name"] for test in selection} <= {test["name"] for test in all_tests}:
            raise ValueError("selected tests are absent from configured inventory")
        cases = junit_cases(junit)
        result["tests"] = cases
        for case in cases:
            result[case["outcome"]] += 1
        result["executed"] = result["passed"] + result["failed"]
        if {case["name"] for case in cases} != {test["name"] for test in selection}:
            raise ValueError("JUnit names/count disagree with selected inventory")
        result["ctest_wall_seconds"] = check_log(log, cases)
        if returncode is None:
            raise ValueError("CTest did not finish")
        if not cases:
            result["outcome"] = "zero-tests-selected"
        elif result["failed"] or result["not_run"] or returncode != 0:
            result["outcome"] = "fail"
        elif result["skipped"]:
            result["outcome"] = "required-skip"
        else:
            result["outcome"] = "pass"
    except (OSError, ValueError, KeyError, TypeError, AttributeError, ET.ParseError) as error:
        result["validation_errors"].append(str(error))
    return result


def source_identity(source: Path) -> dict:
    return {"path": str(source), "git_head": git_bytes(source, "rev-parse", "HEAD").decode().strip(),
            "dirty": bool(git_bytes(source, "status", "--porcelain=v1", "--untracked-files=all"))}


def build_identity(build: Path) -> dict:
    cache = build / "CMakeCache.txt"
    values = {name: entry["value"] for name, entry in read_cache(cache).items()}
    facts = {name: value for name, value in values.items()
             if name.startswith("CNA_") or name in ("CMAKE_GENERATOR", "CMAKE_BUILD_TYPE",
                 "CMAKE_CONFIGURATION_TYPES", "CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER",
                 "CMAKE_TOOLCHAIN_FILE", "CMAKE_HOME_DIRECTORY", "CMAKE_CXX_FLAGS")}
    for name in ("CMAKE_BUILD_TYPE", "CNA_GRAPHICS_RENDERERS"):
        facts.setdefault(name, "")
    for name in ("CMAKE_HOME_DIRECTORY", "CMAKE_GENERATOR", "CMAKE_CXX_COMPILER",
                 "CNA_GRAPHICS_RENDERER", "CNA_PLATFORM"):
        if not values.get(name):
            raise ValueError(f"missing build cache fact: {name}")
    compiler = {"path": values["CMAKE_CXX_COMPILER"], "id": None, "version": None}
    # These are configure-time compiler facts, not the version of an arbitrary PATH compiler.
    for path in sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake")):
        text = path.read_text(encoding="utf-8")
        if f'set(CMAKE_CXX_COMPILER "{compiler["path"]}")' not in text:
            continue
        for key, field in (("CMAKE_CXX_COMPILER_ID", "id"), ("CMAKE_CXX_COMPILER_VERSION", "version")):
            match = re.search(rf'set\({key} "([^"]*)"\)', text)
            if match:
                compiler[field] = match[1]
        compiler["metadata_file"] = str(path)
    if not compiler["id"] or not compiler["version"]:
        raise ValueError("configured C++ compiler identity/version unavailable")
    return {"path": str(build), "cmake_cache": facts, "cache_sha256": file_sha256(cache), "compiler": compiler}


def artifact(path: Path) -> dict:
    return {"path": str(path), "sha256": file_sha256(path) if path.is_file() else None}


def check_metadata(record: dict) -> None:
    source, build = record["source"], record["build"]
    if not source["path"] or not re.fullmatch(r"[0-9a-f]{40,64}", source["git_head"]) or type(source["dirty"]) is not bool:
        raise ValueError("invalid source revision/dirty metadata")
    if record["source_after"] != source:
        raise ValueError("source identity changed during capture")
    if not build["path"] or not re.fullmatch(r"[0-9a-f]{64}", build["cache_sha256"]):
        raise ValueError("invalid build/cache metadata")
    for key in ("CMAKE_GENERATOR", "CMAKE_BUILD_TYPE", "CNA_GRAPHICS_RENDERER", "CNA_GRAPHICS_RENDERERS", "CNA_PLATFORM"):
        if not isinstance(build["cmake_cache"][key], str):
            raise ValueError(f"invalid cache fact: {key}")
    if any(not build["compiler"][key] for key in ("path", "id", "version")):
        raise ValueError("missing compiler facts")
    if not math.isfinite(record["wall_seconds"]) or record["wall_seconds"] < 0:
        raise ValueError("invalid wall time")
    datetime.fromisoformat(record["started_utc"])
    if record["evidence_kind"] not in ("metadata", "runtime"):
        raise ValueError("invalid evidence kind")
    for key in ("regex", "label", "config"):
        if record["selection"][key] is not None and not isinstance(record["selection"][key], str):
            raise ValueError(f"invalid selection: {key}")
    for key in ("configured", "selected"):
        if record[f"{key}_returncode"] != 0:
            raise ValueError(f"unsuccessful {key} inventory")
    for key in ("configured", "selected", "run"):
        if not record["commands"][key] or any(not isinstance(item, str) for item in record["commands"][key]):
            raise ValueError(f"missing/invalid {key} command")


def validate(record: dict) -> dict:
    """Recheck retained bytes and recompute the claim; this is integrity, not authentication."""
    problems = []
    try:
        if record["schema_version"] != SCHEMA_VERSION:
            raise ValueError("unsupported evidence schema version")
        check_metadata(record)
        for name, item in record["artifacts"].items():
            if not item["sha256"] or file_sha256(Path(item["path"])) != item["sha256"]:
                problems.append(f"missing/changed artifact: {name}")
        paths = record["artifacts"]
        result = evaluate(*(Path(paths[name]["path"]) for name in ("configured", "selected", "junit", "log")),
                          record["ctest_returncode"])
        if result != record["result"]:
            problems.append("stored result disagrees with recomputed result")
        problems.extend(record.get("capture_errors", []))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        problems.append(str(error))
        result = {"outcome": "incomplete", "validation_errors": []}
    if problems:
        result = dict(result, outcome="incomplete", validation_errors=result["validation_errors"] + problems)
    return result


def run(args: argparse.Namespace) -> Path:
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    build_facts = build_identity(build)
    source = source_identity(Path(build_facts["cmake_cache"]["CMAKE_HOME_DIRECTORY"]))
    # An exclusive directory prevents stale XML/log reuse, and preserves older evidence.
    output.mkdir(parents=True, exist_ok=False)
    paths = {name: output / filename for name, filename in (
        ("configured", "configured.json"), ("selected", "selected.json"),
        ("junit", "ctest.xml"), ("log", "ctest.log"))}
    base = [args.ctest, "--test-dir", str(build)]
    if args.config:
        base += ["-C", args.config]
    selection = (["-R", args.regex] if args.regex is not None else []) + (["-L", args.label] if args.label is not None else [])
    commands = {"configured": base + ["-N", "--show-only=json-v1"],
                "selected": base + ["-N", "--show-only=json-v1"] + selection,
                "run": base + selection + ["--output-on-failure", "--output-junit", str(paths["junit"]),
                                            "--output-log", str(paths["log"])]}
    record = {"schema_version": SCHEMA_VERSION, "evidence_kind": args.evidence_kind,
              "started_utc": datetime.now(timezone.utc).isoformat(), "source": source,
              "build": build_facts, "selection": {"regex": args.regex, "label": args.label, "config": args.config},
              "context": args.context, "commands": commands, "capture_errors": [],
              "environment": {name: os.environ[name] for name in ("CNA_GRAPHICS_RENDERER", "SDL_VIDEODRIVER",
                  "SDL_AUDIODRIVER", "DISPLAY", "WAYLAND_DISPLAY") if name in os.environ}, "ctest_returncode": None}
    start = time.monotonic()
    try:
        for name in ("configured", "selected"):
            with paths[name].open("wb") as stream:
                capture = subprocess.run(commands[name], stdout=stream, stderr=subprocess.PIPE)
            (output / f"{name}.stderr.log").write_bytes(capture.stderr)
            record[f"{name}_returncode"] = capture.returncode
            if capture.returncode:
                raise ValueError(f"CTest {name} inventory failed: {capture.returncode}")
            inventory(paths[name])
        with (output / "console.log").open("wb") as stream:
            capture = subprocess.run(commands["run"], stdout=stream, stderr=subprocess.STDOUT)
        record["ctest_returncode"] = capture.returncode
        record["source_after"] = source_identity(Path(source["path"]))
        if source != record["source_after"] or file_sha256(build / "CMakeCache.txt") != build_facts["cache_sha256"]:
            raise ValueError("source identity or CMake cache changed during capture")
    except (OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        record["capture_errors"].append(str(error) or type(error).__name__)
    record["wall_seconds"] = time.monotonic() - start
    record["artifacts"] = {name: artifact(path) for name, path in paths.items()}
    record["result"] = evaluate(*(paths[name] for name in ("configured", "selected", "junit", "log")),
                                record["ctest_returncode"])
    if record["capture_errors"]:
        record["result"]["outcome"] = "incomplete"
    record_path = output / "record.json"
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return record_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    capture = actions.add_parser("run", help="run CTest and retain a fresh evidence directory")
    capture.add_argument("--build-dir", type=Path, required=True)
    capture.add_argument("--output-dir", type=Path, required=True, help="must not exist")
    capture.add_argument("--ctest", default="ctest")
    capture.add_argument("-R", "--regex")
    capture.add_argument("-L", "--label")
    capture.add_argument("-C", "--config")
    capture.add_argument("--evidence-kind", choices=("metadata", "runtime"), default="metadata")
    capture.add_argument("--context", action="append", default=[], help="explicit driver/hardware/prerequisite note")
    check = actions.add_parser("validate", help="verify artifact hashes and recompute a stored claim")
    check.add_argument("record", type=Path)
    args = parser.parse_args(argv)
    try:
        path = run(args) if args.action == "run" else args.record
        record = json.loads(path.read_text(encoding="utf-8"))
        result = validate(record)
        print(json.dumps({"record": str(path), **{key: value for key, value in result.items() if key != "tests"}}, indent=2))
        return 0 if result["outcome"] == "pass" else 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(json.dumps({"outcome": "incomplete", "validation_errors": [str(error)]}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
