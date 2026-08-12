#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
INPUT-AUDIT-002 - detect orphaned / untested Input types.

Cross-checks every Input type (the 26 public XNA headers + the internal CNA::Internal::Input sources)
against the test tree, reporting for each type whether it has a *dedicated* GoogleTest suite
(`TEST(<Type>Test, …)` or `TEST_F(<Type>…Test, …)`) and whether it is referenced by any test at all.
A type with no dedicated suite is a candidate gap (each becomes an INPUT-TEST-* task).

This is an inspection aid, not an authority: "referenced" is a textual name match, and some value types
are legitimately covered inside a sibling suite (e.g. the Touch value structs under TouchInputTests).
Read the flagged rows before filing a task.

Usage:
    python3 tools/input_parity/check_input_test_coverage.py [--out docs/input-test-coverage.md]
Run from the repo root. With no --out the report prints to stdout.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PUBLIC_DIR = os.path.join(REPO_ROOT, "include", "Microsoft", "Xna", "Framework", "Input")
INTERNAL_DIR = os.path.join(REPO_ROOT, "include", "CNA", "Internal", "Input")
TESTS_DIR = os.path.join(REPO_ROOT, "tests")

# Types that are deliberately exercised through a sibling suite rather than a same-named one, so a
# missing dedicated suite is expected, not a gap. Keyed by type -> covering suite/file (for the note).
KNOWN_COVERED_ELSEWHERE = {
    "TouchLocation": "TouchInputTests.cpp (TouchLocationTest)",
    "TouchCollection": "TouchInputTests.cpp (TouchInputTest)",
    "TouchPanelCapabilities": "TouchInputTests.cpp (TouchPanelCapabilitiesTest)",
    "GestureSample": "TouchInputTests.cpp (GestureSampleTest)",
    # 'Keys' has no KeysTest suite by design — it is the enum exercised by the exhaustive 160-entry
    # value table + reflection tests in KeyboardInputTests.cpp (INPUT-KBD-001) and driven through the
    # bridge in SdlInputBridgeKeyboardTests.cpp.
    "Keys": "KeyboardInputTests.cpp (exhaustive Keys value table, INPUT-KBD-001)",
    # Internal singletons/seams: verified but with no same-named suite (they are the substrate every
    # bridge/reset test drives, not a value type to test in isolation).
    "InputManager": "InputResetTests / SdlInputBridge* / SdlGamepadBackendTests (no same-named suite by design)",
    "ISdlGamepadBackend": "SdlGamepadBackendTests.cpp via the FakeSdlGamepadBackend seam",
    "GamePadAxis": "SdlInputBridge* / InputManager gamepad tests (internal enum)",
    "MouseButton": "SdlInputBridgeMouseTests / InputManager (internal enum)",
    # GetRawGamePadState(...) is asserted in SdlGamepadBackendTests.cpp (leftY etc.); the return value is
    # bound with `auto`, so the type name never appears literally, but the struct is exercised.
    "RawGamePadState": "SdlGamepadBackendTests.cpp via InputManager::GetRawGamePadState (bound as auto)",
    # INP-AUD-audit (2026-07-16): these legacy interfaces are exercised through a dedicated
    # fake-backend-driven suite, same as ISdlGamepadBackend above -- but the suite is named after the
    # concrete Fake*/CnaInput* type, not the "I"-prefixed interface, so suite_re's literal
    # `<TypeName>\w*Test` prefix match never fires. Confirmed real coverage exists for every one of
    # these before adding the exemption (not just a name collision at the same file).
    "ISdlHapticBackend": "SdlHapticBackendTests.cpp (FakeHapticTest) via the FakeSdlHapticBackend seam",
    "ISdlJoystickBackend": "SdlJoystickBackendTests.cpp (FakeJoystickTest) via the FakeSdlJoystickBackend seam",
    "ISystemDeviceBackend": "InputDevicesTests.cpp (CnaInputDevicesTest) / TouchEdgeCaseTests.cpp (TouchCapabilitiesEnumerationTest) via FakeSystemDeviceBackend",
    "ISystemMouseBackend": "MouseGlobalTests.cpp (MouseGlobalEXTTest) via a fake system-mouse-backend seam",
    "ISystemPowerBackend": "PowerTests.cpp (CnaInputPowerTest) via a fake system-power-backend seam",
    "ISystemSensorBackend": "SensorsTests.cpp (CnaInputSensorsTest) via a fake system-sensor-backend seam",
    # P9-027: both are enums, not classes, so their suite is named after the *class* whose behavior
    # they parameterize, not the enum itself. Confirmed real, exhaustive coverage exists (all
    # flags/values iterated) before adding the exemption.
    "KeyModifiersEXT": "KeyboardModStateTests.cpp (KeyboardModStateEXTTest) — every flag tested individually + combined",
    "TextInputTypeEXT": "TextInputEXTTests.cpp (TextInputEXTTest) — all 9 values iterated in "
                         "StartTextInputWithTypeWithoutWindowIsSafeNoOpForEveryType and "
                         "StartTextInputWithTypeRoundTripsThroughRealWindowForEveryType",
}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def types_in_header(path: str) -> list:
    text = strip_comments(open(path, encoding="utf-8").read())
    names = []
    for m in re.finditer(r"\b(?:enum\s+class|class|struct)\s+([A-Za-z_]\w*)", text):
        name = m.group(1)
        # skip obvious detail/impl helpers and forward-declared SDL handles
        if name.startswith("SDL") or name in names:
            continue
        names.append(name)
    return names


def collect_types(directory: str) -> dict:
    """type name -> relative header path"""
    out = {}
    if not os.path.isdir(directory):
        return out
    for root, _dirs, files in os.walk(directory):
        for f in sorted(files):
            if f.endswith(".hpp"):
                p = os.path.join(root, f)
                for name in types_in_header(p):
                    out.setdefault(name, os.path.relpath(p, REPO_ROOT))
    return out


def load_tests() -> list:
    """[(relpath, text), ...] for every test .cpp"""
    out = []
    for root, _dirs, files in os.walk(TESTS_DIR):
        for f in sorted(files):
            if f.endswith(".cpp"):
                p = os.path.join(root, f)
                out.append((os.path.relpath(p, REPO_ROOT), open(p, encoding="utf-8").read()))
    return out


def analyse(types: dict, tests: list):
    rows = []
    for name in sorted(types):
        suite_re = re.compile(r"\bTEST(?:_F)?\(\s*" + re.escape(name) + r"\w*Test\b")
        dedicated = None
        ref_files = 0
        for relpath, text in tests:
            if suite_re.search(text):
                dedicated = dedicated or relpath
            # word-boundary textual reference
            if re.search(r"\b" + re.escape(name) + r"\b", text):
                ref_files += 1
        rows.append((name, types[name], dedicated, ref_files))
    return rows


def render(rows_public, rows_internal) -> str:
    out = []
    out.append("# Input source → test coverage (INPUT-AUDIT-002)")
    out.append("")
    out.append("> **Related input docs (INP-0003):** [plan](../plan_input.md) · "
               "[backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · "
               "[member-parity matrix](input-member-parity-matrix.md) · "
               "[frozen API + tier glossary](input-public-api-frozen.md) · "
               "[test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · "
               "[platform notes](platform-input-notes.md) · "
               "[manual results](input-manual-verification-results.md) · "
               "[demo checklist](demo-input-checklist.md)")
    out.append("")
    out.append("> **Generated** by `tools/input_parity/check_input_test_coverage.py`. Maps each Input")
    out.append("> type to whether it has a dedicated `TEST(<Type>Test, …)` suite and how many test files")
    out.append("> reference it. Inspection aid — a value type covered inside a sibling suite is not a real")
    out.append("> gap (see the `note` column). Do not hand-edit; re-run the script.")
    out.append("")

    gaps = []

    def section(title, rows):
        out.append(f"## {title}")
        out.append("")
        out.append("| Type | Header | Dedicated suite | Test files | Note |")
        out.append("|------|--------|-----------------|-----------|------|")
        for name, header, dedicated, refs in rows:
            note = ""
            if dedicated:
                suite = "yes"
            elif name in KNOWN_COVERED_ELSEWHERE:
                suite = "no"
                note = "covered via " + KNOWN_COVERED_ELSEWHERE[name]
            elif refs > 0:
                suite = "no"
                note = f"referenced in {refs} test file(s), no same-named suite — review"
                gaps.append((name, header, refs))
            else:
                suite = "no"
                note = "**NO TEST REFERENCE** — orphaned"
                gaps.append((name, header, refs))
            out.append(f"| `{name}` | `{header}` | {suite} | {refs} | {note} |")
        out.append("")

    section("Public XNA Input types", rows_public)
    section("Internal (`CNA::Internal::Input`) types", rows_internal)

    out.insert(6, "")
    summary = ["## Gaps (candidate INPUT-TEST-* tasks)", ""]
    if not gaps:
        summary.append("None — every Input type has a dedicated suite or a documented sibling-suite cover.")
    else:
        for name, header, refs in gaps:
            kind = "orphaned (no test reference)" if refs == 0 else f"no dedicated suite ({refs} refs)"
            summary.append(f"- `{name}` (`{header}`) — {kind}")
    summary.append("")
    out[6:6] = summary
    return "\n".join(out) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)

    public = collect_types(PUBLIC_DIR)
    internal = collect_types(INTERNAL_DIR)
    # a type declared in both (shouldn't happen) counts as public
    for k in public:
        internal.pop(k, None)

    tests = load_tests()
    rows_public = analyse(public, tests)
    rows_internal = analyse(internal, tests)
    md = render(rows_public, rows_internal)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(md)
        n_gap = md.count("**NO TEST REFERENCE**") + md.count("no same-named suite")
        print(f"wrote {args.out}: {len(public)} public + {len(internal)} internal types",
              file=sys.stderr)
    else:
        sys.stdout.write(md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
