# SPDX-License-Identifier: MS-PL
"""Synthetic retained CTest artifacts, not runtime qualification evidence."""
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from evidence_layers import ctest_evidence as evidence


def capture(directory, *, name="Probe.Required", output="", kind="runtime", command=None,
            state="passed", disabled=False, configured_names=None):
    directory = Path(directory)
    directory.mkdir()
    test = {"name": name, "command": command or ["fixture-test"],
            "properties": [{"name": "DISABLED", "value": disabled}]}
    inventory = {"kind": "ctestInfo", "version": {"major": 1}, "tests": [test]}
    configured, selected, junit, log = [directory / file for file in
                                        ("configured.json", "selected.json", "ctest.xml", "ctest.log")]
    full = dict(inventory, tests=[{"name": entry, "command": ["fixture-test"],
                                  "properties": [{"name": "DISABLED", "value": False}]}
                                 for entry in configured_names]) if configured_names else inventory
    configured.write_text(json.dumps(full))
    selected.write_text(json.dumps(inventory))
    suite = ET.Element("testsuite", tests="1", failures=str(int(state == "failed")),
                       disabled=str(int(state == "not_run")), skipped=str(int(state == "skipped")))
    case = ET.SubElement(suite, "testcase", name=name, time="0.01",
                         status={"passed": "run", "failed": "fail", "skipped": "notrun", "not_run": "disabled"}[state])
    ET.SubElement(case, "system-out").text = output
    if state in ("failed", "skipped"):
        ET.SubElement(case, "failure" if state == "failed" else "skipped", message="fixture")
    junit.write_text(ET.tostring(suite, encoding="unicode"))
    status = {"passed": "Passed", "failed": "***Failed", "skipped": "***Skipped",
              "not_run": "***Not Run (Disabled)"}[state]
    log.write_text(f"Test project /fixture/build\n1/1 Test #1: {name} .... {status} 0.01 sec\n"
                   f"{0 if state == 'failed' else 100}% tests passed, {int(state == 'failed')} tests failed out of 1\n"
                   "Total Test time (real) = 0.01 sec\n")
    code = 8 if state == "failed" else 0
    source = {"path": "/fixture/source", "git_head": "a" * 40, "dirty": False}
    record = {"schema_version": 1, "source": source, "source_after": source,
              "build": {"path": "/fixture/build", "cache_sha256": "b" * 64,
                        "compiler": {"path": "/fixture/compiler", "id": "GNU", "version": "14"},
                        "cmake_cache": {"CMAKE_GENERATOR": "Ninja", "CMAKE_BUILD_TYPE": "Debug",
                                        "CNA_GRAPHICS_RENDERER": "SOFTWARE", "CNA_GRAPHICS_RENDERERS": "",
                                        "CNA_PLATFORM": "SDL3"}},
              "started_utc": "2026-11-02T00:00:00+00:00", "wall_seconds": 0.01,
              "evidence_kind": kind, "capture_errors": [], "ctest_returncode": code,
              "configured_returncode": 0, "selected_returncode": 0,
              "selection": {"regex": None, "label": None, "config": None},
              "commands": {key: ["ctest"] for key in ("configured", "selected", "run")},
              "artifacts": {key: evidence.artifact(path) for key, path in
                            zip(("configured", "selected", "junit", "log"), (configured, selected, junit, log))},
              "result": evidence.evaluate(configured, selected, junit, log, code)}
    path = directory / "record.json"
    path.write_text(json.dumps(record))
    return path
