#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Decide whether the experimental C ABI may be published, and refuse to let the answer go stale.

A release gate written as prose is a list of things somebody once believed. This one is a
declaration plus a measurement: ``release_gate.json`` records what each criterion *should* be, the
checks below measure what it *is*, and the two must agree.

The agreement is enforced **in both directions**, and the second one is the point:

* a criterion recorded as met that no longer is — the ordinary regression;
* a criterion recorded as blocked that has quietly become met — the failure mode a release gate
  actually dies of, because nobody re-reads a document that says "not yet".

Two criteria are owner decisions. They are not defects, they cannot be settled by whoever is
implementing, and they **block the release** until somebody with the authority rules on them. This
tool measures whether they are still open by reading ``limitations.json``; ruling on one there makes
this check fail until the gate declaration is updated to match, which is exactly the behavior a
decision record should have.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
DECLARATION_PATH = TOOLS / "release_gate.json"
LIMITATIONS_PATH = TOOLS / "limitations.json"
ABI_BASELINE_PATH = TOOLS / "abi_baseline.json"
DOC_DIR = REPO_ROOT / "docs" / "c-api"
DOC_PATH = DOC_DIR / "RELEASE_GATE.md"
C_API_DIR = REPO_ROOT / "modules" / "c-api"

# A criterion is one of these three. "met" and "not met" are measurements; "blocked" is a
# measurement too -- of whether a human has answered a question, not of whether code works.
MET = "met"
NOT_MET = "not met"
BLOCKED = "blocked"


def run_tool(script: str, *arguments: str) -> tuple[bool, str]:
    completed = subprocess.run(
        [sys.executable, str(TOOLS / script), *arguments],
        capture_output=True, text=True, cwd=REPO_ROOT)
    output = (completed.stdout + completed.stderr).strip().splitlines()
    return completed.returncode == 0, output[0] if output else ""


def check_compatibility_matrix_current() -> tuple[str, str]:
    ok, message = run_tool("generate_compatibility_matrix.py", "--check")
    if not ok:
        return NOT_MET, message or "the published matrix does not match its declaration"
    matrix = json.loads((TOOLS / "compatibility_matrix.json").read_text(encoding="utf-8"))
    cells = sum(len(toolchain["standards"]) for toolchain in matrix["toolchains"])
    return MET, f"{cells} declared cells across {len(matrix['toolchains'])} toolchains"


def check_abi_baseline_current() -> tuple[str, str]:
    if not ABI_BASELINE_PATH.exists():
        return NOT_MET, "no baseline has been recorded"
    ok, message = run_tool("generate_abi_baseline.py", "--check")
    if not ok:
        return NOT_MET, message or "the build disagrees with the recorded baseline"
    baseline = json.loads(ABI_BASELINE_PATH.read_text(encoding="utf-8"))
    return MET, (f"{len(baseline['structs'])} struct layouts and "
                 f"{len(baseline['exports'])} exported symbols recorded")


def check_coverage_has_no_planned_rows() -> tuple[str, str]:
    coverage = DOC_DIR / "COVERAGE.md"
    if not coverage.exists():
        return NOT_MET, "the inventory has not been generated"
    snapshot = re.search(
        r"\*\*([0-9,]+) implemented\*\*, \*\*([0-9,]+) partial\*\*, \*\*([0-9,]+) planned\*\*",
        coverage.read_text(encoding="utf-8"))
    if snapshot is None:
        return NOT_MET, "the inventory snapshot could not be read"
    implemented, partial, planned = (int(value.replace(",", "")) for value in snapshot.groups())
    if planned != 0:
        return NOT_MET, f"{planned} public symbols are still unmapped"
    return MET, f"{implemented} implemented, {partial} partial, 0 planned"


def check_limitations_current() -> tuple[str, str]:
    ok, message = run_tool("generate_limitations.py", "--check")
    return (MET, message) if ok else (NOT_MET, message or "the limitations matrix is stale")


def check_installed_consumer_gate_exists() -> tuple[str, str]:
    example = C_API_DIR / "examples" / "c" / "hello_cna.c"
    project = C_API_DIR / "examples" / "c" / "CMakeLists.txt"
    if not example.exists() or not project.exists():
        return NOT_MET, "the C example or its standalone project is missing"
    if "find_package(CNA" not in project.read_text(encoding="utf-8"):
        return NOT_MET, "the example does not consume CNA as a package"
    lists = (C_API_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    if "CApi_InstalledConsumer" not in lists:
        return NOT_MET, "nothing builds the example from an installed prefix"
    return MET, "hello_cna is built from an installed prefix and run by CApi_InstalledConsumer"


def check_package_config_installed() -> tuple[str, str]:
    template = C_API_DIR / "cmake" / "CNAConfig.cmake.in"
    if not template.exists():
        return NOT_MET, "there is no package config template, so find_package cannot resolve"
    lists = (C_API_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    for needle, missing in (
            ("configure_package_config_file", "the package config is never configured"),
            ("write_basic_package_version_file", "no version file is generated"),
            ("COMPONENT CNACApi", "there is no C API install component")):
        if needle not in lists:
            return NOT_MET, missing
    return MET, "CNAConfig.cmake, a version file from abi.h, and a CNACApi component"


def check_required_documents_present(declaration: dict) -> tuple[str, str]:
    missing = [name for name in declaration["required_documents"]
               if not (DOC_DIR / name).exists() or (DOC_DIR / name).stat().st_size < 512]
    if missing:
        return NOT_MET, "missing or trivial: " + ", ".join(missing)
    return MET, f"{len(declaration['required_documents'])} documents present"


def check_safety_tests_present() -> tuple[str, str]:
    lists = (C_API_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    required = {
        "CApi_StressSmoke": C_API_DIR / "tests" / "pure_c" / "StressSmoke.c",
        "CApi_Utf8Oracle": C_API_DIR / "tests" / "cpp" / "Utf8OracleTest.cpp",
    }
    for test, source in required.items():
        if test not in lists or not source.exists():
            return NOT_MET, f"{test} is not registered or its source is gone"
    if not (C_API_DIR / "tests" / "fuzz" / "StringViewFuzz.cpp").exists():
        return NOT_MET, "the fuzz target is gone"
    return MET, "stress, exhaustive oracle and fuzz target all present"


def check_native_dependencies_shipped() -> tuple[str, str]:
    """The package must carry what CNA builds, and the consumer gate must prove it needs nothing."""
    lists = (C_API_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    if "libSDL3*.so*" not in lists:
        return NOT_MET, "the SDL3 runtime libraries are not installed with the C API component"
    if 'INSTALL_RPATH "$ORIGIN"' not in lists:
        return NOT_MET, "the installed library does not look beside itself for its dependencies"
    script = (C_API_DIR / "cmake" / "RunInstalledConsumer.cmake").read_text(encoding="utf-8")
    for needle, why in (
            ("rpath-link", "the consumer gate still tells the linker where SDL lives"),
            ("LD_LIBRARY_PATH", "the consumer gate still tells the loader where SDL lives")):
        if needle in script.replace("no -rpath-link", "").replace("no LD_LIBRARY_PATH", ""):
            return NOT_MET, why
    return MET, "SDL3 ships beside the library; the consumer needs no environment variable"


def check_static_configuration_available() -> tuple[str, str]:
    """A static build only counts if it publishes the same names the shared library does."""
    builder = TOOLS / "generate_static_archive.py"
    if not builder.exists():
        return NOT_MET, "nothing builds a static archive"
    text = builder.read_text(encoding="utf-8")
    if "--keep-global-symbols" not in text or "survived localization" not in text:
        return NOT_MET, "the static archive is built without localizing its internal symbols"
    lists = (C_API_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    if "cna_c_api_static" not in lists:
        return NOT_MET, "the build does not produce the static archive"
    example = (C_API_DIR / "examples" / "c" / "CMakeLists.txt").read_text(encoding="utf-8")
    if "CNA::CApiStatic" not in example:
        return NOT_MET, "no consumer links the static archive"
    config = (C_API_DIR / "cmake" / "CNAConfig.cmake.in").read_text(encoding="utf-8")
    if "CNACStaticTargets" not in config:
        return NOT_MET, "the installed package does not offer the static target"
    return MET, "CNA::CApiStatic ships and is linked and run by the consumer gate"


def check_owner_decision_still_open(subject: str) -> tuple[str, str]:
    limitations = json.loads(LIMITATIONS_PATH.read_text(encoding="utf-8"))
    for entry in limitations["environment"]:
        if entry["subject"] == subject:
            if entry["status"] == "open decision":
                return BLOCKED, "no decision has been recorded"
            return MET, f"recorded as \"{entry['status']}\""
    return NOT_MET, f"the limitations matrix no longer records \"{subject}\""


def measure(criterion: dict, declaration: dict) -> tuple[str, str]:
    check = criterion["check"]
    if check.startswith("owner_decision_still_open:"):
        return check_owner_decision_still_open(check.split(":", 1)[1])
    if check == "required_documents_present":
        return check_required_documents_present(declaration)
    handler = globals().get(f"check_{check}")
    if handler is None:
        raise SystemExit(f"{criterion['id']} names an unknown check: {check}")
    return handler()


def expected_state(recorded: str) -> str:
    return BLOCKED if recorded == "owner-decision" else recorded


def evaluate() -> dict:
    declaration = json.loads(DECLARATION_PATH.read_text(encoding="utf-8"))
    results = []
    disagreements = []
    for criterion in declaration["criteria"]:
        state, detail = measure(criterion, declaration)
        expected = expected_state(criterion["recorded"])
        if state != expected:
            disagreements.append(
                f"{criterion['id']}: recorded as \"{expected}\" but measures as \"{state}\" "
                f"({detail})")
        results.append({**criterion, "state": state, "detail": detail})
    ready = all(result["state"] == MET for result in results)
    return {
        "declaration": declaration,
        "results": results,
        "disagreements": disagreements,
        "ready": ready,
    }


def render(evaluation: dict) -> str:
    declaration = evaluation["declaration"]
    lines: list[str] = []
    add = lines.append

    add("# Experimental release gate")
    add("")
    add("Generated by `tools/c-api/check_release_gate.py` from `tools/c-api/release_gate.json`.")
    add("")
    add(f"**Release:** {declaration['release']['name']}")
    add("")
    add(declaration["release"]["note"])
    add("")
    add("## Verdict")
    add("")
    blocked = [result for result in evaluation["results"] if result["state"] == BLOCKED]
    failed = [result for result in evaluation["results"] if result["state"] == NOT_MET]
    if evaluation["ready"]:
        add("**Ready.** Every criterion below is met.")
    else:
        if failed and blocked:
            add(f"**Not ready.** {len(failed)} criteria are unmet, and {len(blocked)} await a "
                "decision that is not an implementer's to make.")
        elif failed:
            add(f"**Not ready.** {len(failed)} criteria are unmet.")
        else:
            add("**Not ready.** Every mechanical criterion is met. What remains is "
                f"{len(blocked)} decision(s) that no implementer may make alone.")
        add("")
        for result in blocked:
            add(f"- ⏸ **{result['title']}** — {result['detail']}.")
        for result in failed:
            add(f"- ❌ **{result['title']}** — {result['detail']}.")
    add("")
    add("This verdict is measured on every run, not written down once. A criterion recorded as met")
    add("that stops being met fails the check; so does a criterion recorded as blocked that has")
    add("quietly become met, because a gate nobody re-reads is how a project ships something it had")
    add("decided not to ship.")
    add("")
    add("## Criteria")
    add("")
    add("| | Criterion | What it requires | Measured |")
    add("|---|---|---|---|")
    symbols = {MET: "✅", NOT_MET: "❌", BLOCKED: "⏸"}
    for result in evaluation["results"]:
        requirement = result["requirement"].replace("|", "\\|")
        detail = result["detail"].replace("|", "\\|")
        add(f"| {symbols[result['state']]} | **{result['title']}** | {requirement} | {detail} |")
    add("")
    add("## Why each one is here")
    add("")
    for result in evaluation["results"]:
        add(f"### {symbols[result['state']]} {result['title']}")
        add("")
        add(f"*Evidence:* {result['evidence']}")
        add("")
        add(result["note"])
        add("")
    add("Regenerate with:")
    add("")
    add("```bash")
    add("python3 tools/c-api/check_release_gate.py --write")
    add("```")
    add("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--run", action="store_true", help="print the verdict")
    group.add_argument("--write", action="store_true", help="regenerate the published gate")
    group.add_argument("--check", action="store_true",
                       help="verify the record and the measurement agree, and the document is current")
    arguments = parser.parse_args()

    evaluation = evaluate()
    rendered = render(evaluation)

    if arguments.run:
        symbols = {MET: "  met", NOT_MET: "  NOT MET", BLOCKED: "  blocked"}
        for result in evaluation["results"]:
            print(f"{symbols[result['state']]:>10}  {result['id']:<26}  {result['detail']}")
        print()
        print("READY to publish the experimental release." if evaluation["ready"]
              else "NOT READY: see the criteria above.")
        return 0

    if arguments.write:
        DOC_PATH.write_text(rendered, encoding="utf-8")
        print(f"wrote {DOC_PATH.relative_to(REPO_ROOT)}: "
              f"{'ready' if evaluation['ready'] else 'not ready'}")
        return 0

    if evaluation["disagreements"]:
        print(f"{len(evaluation['disagreements'])} criteria disagree with the record:",
              file=sys.stderr)
        for entry in evaluation["disagreements"]:
            print(f"  {entry}", file=sys.stderr)
        print("Update tools/c-api/release_gate.json, or fix what regressed.", file=sys.stderr)
        return 1

    current = DOC_PATH.read_text(encoding="utf-8") if DOC_PATH.exists() else ""
    if current != rendered:
        print(
            f"{DOC_PATH.relative_to(REPO_ROOT)} is out of date; "
            "run check_release_gate.py --write",
            file=sys.stderr)
        return 1
    print(f"{DOC_PATH.relative_to(REPO_ROOT)} is current: "
          f"{'ready' if evaluation['ready'] else 'not ready'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
