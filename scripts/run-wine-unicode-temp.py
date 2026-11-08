#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""CNA-FIX-L27: compare content suites with ASCII and ACP-representable non-ASCII TEMP.

Run only inside the private runner. This creates no Wine prefix or build tree.
Both suites use the existing Wine wrapper's documented content/no-D3D9 mode.
This measures TEMP handling, not D3D9 pixel semantics. No test failure/abort is suppressed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


def write_windows_launcher(path):
    # Wine's argv conversion escapes embedded quotes as \". cmd /c does not
    # interpret those like a CRT argv parser. Pass only a batch filename through
    # argv; cmd reads its quotes directly from disk. ASCII variable references
    # preserve non-ASCII environment values without guessing a batch code page.
    lines = [
        "@echo off",
        "setlocal DisableDelayedExpansion",
        'set "TEMP=%CNA_L27_TEMP_ROOT%"',
        'set "TMP=%CNA_L27_TEMP_ROOT%"',
        '"%CNA_L27_BINARY%" --gtest_filter=* --gtest_fail_if_no_test_linked '
        '"--gtest_output=xml:%CNA_L27_REPORT%"',
        "exit /b %errorlevel%",
    ]
    path.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))


def windows_launch(launcher):
    # /c receives a filename, with no compound command or embedded quotes.
    if any(character in launcher for character in '"%\r\n'):
        raise ValueError("Windows launcher path cannot contain quotes, percent or newlines")
    return ["cmd.exe", "/d", "/v:off", "/c", launcher]


def read_outcomes(path):
    result = {}
    for suite in ET.parse(path).getroot().iter("testsuite"):
        for case in suite.findall("testcase"):
            key = (suite.attrib["name"], case.attrib["name"])
            if key in result:
                raise RuntimeError(f"duplicate test: {key}")
            result[key] = (case.attrib.get("status"), case.attrib.get("result"),
                           case.find("failure") is not None, case.find("skipped") is not None)
    return result


def check_outcomes(result, path):
    witness = ("WindowsTempQualification", "RequestedRootReachesTheSuiteAndRoundTripsThroughAcp")
    completed = sum(state == ("run", "completed", False, False) for state in result.values())
    if completed < 2 or result.get(witness) != ("run", "completed", False, False):
        raise RuntimeError(f"missing/pending/failed in-process TEMP witness: {path}")
    if any(state[2] for state in result.values()):
        raise RuntimeError(f"failed testcase in suite XML: {path}")
    return result


def outcomes(path):
    return check_outcomes(read_outcomes(path), path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True,
                        help="fresh evidence directory outside the source tree")
    parser.add_argument("--wine-runner", type=Path,
                        default=Path(__file__).with_name("run-wine-dxvk9.sh"))
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    build, work = args.build_dir.resolve(), args.work_dir.resolve()
    if work.is_relative_to(source):
        parser.error("TEMP evidence directory must be outside the source tree")
    work.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env["WINEPREFIX"] = env.get("CNA_D3D9_WINEPREFIX", str(Path.home() / ".wine-cna-d3d11"))
    # The caller may select x11 for host programs. Windows SDL
    # reaches that display through Wine's windows driver, not an x11 SDL driver.
    env.update(WINEDEBUG="-all", SDL_VIDEODRIVER="windows",
               SDL_AUDIODRIVER="dummy", SDL_ASSERT="abort")
    env.pop("CNA_D3D9_ALLOW_WINED3D", None)
    # This qualification has no DXVK marker requirement. Keep the wrapper's
    # pixel gate unchanged; opt into its documented no-D3D9 transport for both.
    env["CNA_D3D9_SKIP_DXVK_GATE"] = "1"
    receipt = {"passed": False, "runs": [], "errors": [], "comparisons": {},
               "wineprefix": env["WINEPREFIX"], "video_driver": env["SDL_VIDEODRIVER"], "limitation":
               "googletest ANSI-temp abort outside ACP remains unsuppressed; native MSVC not qualified"}
    try:
        for target in ("CnaContentTests", "CnaContentPipelineTests"):
            baseline = None
            windows_binary = subprocess.check_output(
                ["winepath", "-w", str(build / (target + ".exe"))], env=env, text=True).strip()
            for label, leaf in (("ascii", "l27-ascii"), ("nonascii", "l27-caf\u00e9")):
                temp = work / f"{target}-{leaf}"
                temp.mkdir()
                windows_temp = subprocess.check_output(
                    ["winepath", "-w", str(temp)], env=env, text=True).strip()
                report = work / f"l27-{target}-{label}.xml"
                windows_report = subprocess.check_output(
                    ["winepath", "-w", str(report)], env=env, text=True).strip()
                run_env = env | {"TEMP": windows_temp, "TMP": windows_temp,
                                 "CNA_L27_TEMP_ROOT": windows_temp,
                                 "CNA_L27_BINARY": windows_binary,
                                 "CNA_L27_REPORT": windows_report,
                                 "CNA_L27_REQUIRE_NONASCII": str(int(label == "nonascii"))}
                launcher = work / f"l27-{target}-{label}.cmd"
                write_windows_launcher(launcher)
                windows_launcher = subprocess.check_output(
                    ["winepath", "-w", str(launcher)], env=env, text=True).strip()
                command = [str(args.wine_runner.resolve()),
                           *windows_launch(windows_launcher)]
                log = work / f"l27-{target}-{label}.log"
                with log.open("w") as output:
                    run = subprocess.run(command, cwd=source, env=run_env,
                                         stdout=output, stderr=subprocess.STDOUT)
                receipt["runs"].append({"suite": target, "mode": label, "temp": windows_temp,
                                        "dxvk_gate_required": False, "launcher": str(launcher),
                                        "command": command, "exit": run.returncode, "log": str(log)})
                print(f"{target} {label}: exit {run.returncode}; {log}", flush=True)
                if run.returncode:
                    receipt["errors"].append(f"suite failed/aborted: {target} {label}; see {log}")
                if not report.is_file():
                    receipt["runs"][-1]["xml_status"] = "missing"
                    receipt["errors"].append(
                        f"no suite XML: {target} {label} (exit {run.returncode}); see {log}")
                    continue
                try:
                    measured = read_outcomes(report)
                except (OSError, ET.ParseError, RuntimeError) as error:
                    receipt["runs"][-1]["xml_status"] = "unreadable"
                    receipt["errors"].append(f"cannot read suite XML {report}: {error}; see {log}")
                    continue
                receipt["runs"][-1].update(xml_status="read", tests=len(measured),
                    failed_cases=[".".join(name) for name, state in measured.items() if state[2]],
                    skipped=sum(state[3] for state in measured.values()),
                    disabled=sum(state[0] == "notrun" for state in measured.values()),
                    completed=sum(state == ("run", "completed", False, False)
                                  for state in measured.values()))
                try:
                    check_outcomes(measured, report)
                except RuntimeError as error:
                    receipt["errors"].append(str(error))
                if baseline is None:
                    baseline = measured
                else:
                    receipt["comparisons"][target] = measured == baseline
                    if measured != baseline:
                        receipt["errors"].append(f"ASCII/non-ASCII test outcomes differ: {target}")
        if receipt["errors"]:
            for error in receipt["errors"]:
                print(error, file=sys.stderr)
            return 1
        receipt["passed"] = True
        return 0
    finally:
        (work / "l27-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
