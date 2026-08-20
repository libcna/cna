#!/usr/bin/env python3
"""plans/plan_direct2d.md D2D-113/D2D-124: turn the Direct2D debug-layer log into a real gate.

Uploading `ReportLiveDeviceObjects` output as a CI artifact proves nothing on its own -- nobody
reads a green build's artifact. This parser consumes the verbose CTest log produced by the native
Direct2D workflow legs and fails when the run either

  * never actually enabled the diagnostics/debug layer it claims to verify,
  * produced a debug-layer message of severity ERROR or CORRUPTION, or
  * leaked a live D3D11/D2D/DXGI object that is not one of the documented, unavoidable runtime
    objects still alive at report time.

The contract is CNA's own emitter in `Direct2DRenderer::ReportLiveDeviceObjectsNoThrow`, not a
guess at raw `d3d11sdklayers` formatting: every classified line starts with the fixed
`[Direct2D D3D11 debug] severity=<S> category=<C> id=<I> ` prefix and keeps the debug layer's own
description text afterwards.

Usage:
  verify-direct2d-debug-log.py <log> [<log> ...]
  verify-direct2d-debug-log.py --self-test [<repository-root>]

`--self-test` runs the parser over the committed positive and negative fixtures in
`tests/fixtures/direct2d/` so the gate itself is regression-tested on Linux, without Windows.
"""

from __future__ import annotations

import pathlib
import re
import sys


# CNA reports live objects while its own ID3D11Device reference is still held (the report is the
# last thing that runs before `d3dDevice_.Reset()`), and D3D11's debug layer always counts its own
# immediate context and the device's internal refs. Those are unavoidable and carry no leak
# information; anything else is a real leak the gate must fail on.
ALLOWED_LIVE_INTERFACES = frozenset(
    {
        "ID3D11Device",
        "ID3D11DeviceContext",
        "ID3D11Debug",
        "ID3D11InfoQueue",
    }
)

FATAL_SEVERITIES = frozenset({"ERROR", "CORRUPTION"})

# CTest's verbose output prefixes every captured line with "<n>: ", so the marker is searched for
# anywhere in the line rather than anchored at its start.
DEBUG_MESSAGE_RE = re.compile(
    r"\[Direct2D D3D11 debug\] severity=(?P<severity>[A-Z]+) "
    r"category=(?P<category>-?\d+) id=(?P<id>-?\d+) (?P<description>.*)$"
)
# The debug layer's own live-object wording, e.g.
#   "Live ID3D11Device at 0x00000232A9C0B0F0, Refcount: 4"
#   "Live                  ID3D11Texture2D :      3"
LIVE_OBJECT_RE = re.compile(
    r"\bLive\s+(?P<interface>[A-Za-z_][A-Za-z0-9_]*)\s*(?:at\s+0x[0-9A-Fa-f]+\s*,\s*)?"
    r"(?::|Refcount:)\s*(?P<count>\d+)"
)
LIVE_SUMMARY_RE = re.compile(r"\bLive\s+Objects?\s+Summary\b", re.IGNORECASE)

CREATED_MARKER = "[Direct2D diagnostics] created driver="
REPORT_MARKER = "[Direct2D diagnostics] ReportLiveDeviceObjects hr="
REPORT_END_MARKER = "[Direct2D diagnostics] live-object report end"
EXCEPTION_MARKER = "[Direct2D diagnostics] live-object reporting raised an unexpected exception"


def check_log(text: str, name: str) -> list[str]:
    """Returns the list of gate violations found in one log."""
    errors: list[str] = []

    if CREATED_MARKER not in text:
        errors.append(
            f"{name}: no '{CREATED_MARKER}...' line -- CNA_DIRECT2D_DIAGNOSTICS was not active, "
            "so this log cannot demonstrate anything about the native runtime."
        )
    if REPORT_MARKER not in text:
        errors.append(
            f"{name}: no '{REPORT_MARKER}...' line -- CNA_DIRECT2D_DEBUG_LAYER was not active, so "
            "no live-object report was produced."
        )
    elif REPORT_END_MARKER not in text:
        errors.append(
            f"{name}: the live-object report started but never reached "
            f"'{REPORT_END_MARKER}' -- the log is truncated or the process died mid-report."
        )
    if EXCEPTION_MARKER in text:
        errors.append(f"{name}: live-object reporting raised an unexpected exception.")

    for lineno, line in enumerate(text.splitlines(), start=1):
        match = DEBUG_MESSAGE_RE.search(line)
        if match is None:
            continue
        severity = match.group("severity")
        description = match.group("description").strip()
        if severity in FATAL_SEVERITIES:
            errors.append(f"{name}:{lineno}: debug-layer {severity}: {description}")
            continue
        if LIVE_SUMMARY_RE.search(description):
            continue
        live = LIVE_OBJECT_RE.search(description)
        if live is None:
            continue
        interface = live.group("interface")
        count = int(live.group("count"))
        if count == 0:
            continue
        if interface not in ALLOWED_LIVE_INTERFACES:
            errors.append(
                f"{name}:{lineno}: unexpected live object {interface} (refcount {count}); only "
                f"{sorted(ALLOWED_LIVE_INTERFACES)} may still be alive when CNA reports."
            )
    return errors


def self_test(root: pathlib.Path) -> int:
    fixtures = root / "tests/fixtures/direct2d"
    if not fixtures.is_dir():
        print(f"self-test: missing fixture directory {fixtures}", file=sys.stderr)
        return 1

    failures: list[str] = []
    checked = 0
    for path in sorted(fixtures.glob("debug-log-*.log")):
        checked += 1
        errors = check_log(path.read_text(encoding="utf-8"), path.name)
        expects_clean = path.name.startswith("debug-log-clean-")
        if expects_clean and errors:
            failures.append(f"{path.name} must pass but reported: {errors}")
        if not expects_clean and not errors:
            failures.append(f"{path.name} must fail the gate but reported no violation")

    if checked == 0:
        failures.append(f"self-test found no debug-log-*.log fixtures in {fixtures}")
    if not any(p.name.startswith("debug-log-clean-") for p in fixtures.glob("debug-log-*.log")):
        failures.append("self-test needs at least one positive (debug-log-clean-*.log) fixture")
    if not any(p.name.startswith("debug-log-dirty-") for p in fixtures.glob("debug-log-*.log")):
        failures.append("self-test needs at least one negative (debug-log-dirty-*.log) fixture")

    for failure in failures:
        print(f"Direct2D debug-log gate self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"Direct2D debug-log gate self-test: {checked} fixture(s) classified correctly.")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[1] == "--self-test":
        root = pathlib.Path(argv[2]) if len(argv) > 2 else pathlib.Path(__file__).resolve().parents[1]
        return self_test(root.resolve())
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2

    errors: list[str] = []
    for argument in argv[1:]:
        path = pathlib.Path(argument)
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            errors.append(f"{argument}: unreadable ({error})")
            continue
        errors.extend(check_log(text, path.name))

    for error in errors:
        print(f"Direct2D debug-log gate: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"Direct2D debug-log gate: {len(argv) - 1} log(s) clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
