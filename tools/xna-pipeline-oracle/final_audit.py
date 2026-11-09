#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""XNAPP-330: source/metadata evidence for the campaign's acceptance checklist.

The campaign named twenty-six acceptance conditions. This script checks their recorded counts,
status flags, paths and source patterns. It does not establish that every condition holds at the
current revision; test execution and semantic qualification are separate evidence.

Each row identifies the kind of evidence this script inspects:

  * `parity`  -- a count read from the generated parity report's own summary, which is itself the
    join of the frozen inventory with the map;
  * `inputs`  -- the eighteen-extension matrix, read directly;
  * `tests`   -- gtest declaration patterns in C++ source, without compilation or execution;
  * `ctests`  -- add_test name patterns in CMake source, without evaluating configuration;
  * `files`   -- artefacts that must exist, for the documents and fixtures a row rests on;
  * `gate`    -- another gate script, run for its exit status.

What this cannot do is re-run the whole suite, and it does not pretend to: a test that exists but
fails is a red suite, and the suite is where that is answered. This answers the other question --
whether each condition has its requested metadata/source witness. Regex matches can include
unknown conditional branches; a positive match is not proof that a configured binary runs it.
Comments, literals and literal false branches are excluded cheaply. Optional configured and
validated execution layers never establish assertion coverage or semantic runtime qualification.

Usage:
    final_audit.py --repo <root> [--output docs/xna-content-pipeline-final-audit.md] [--check]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from evidence_layers import add_arguments, report_layers, source_text
import parity_report  # noqa: E402  (path is set above on purpose)


def test_names_in(repo, names):
    """Finds requested TEST/TEST_F/TEST_P patterns in modules/tests C++ source text."""
    found = set()
    for root in ("modules", "tests"):
        for base, _, files in os.walk(os.path.join(repo, root)):
            for name in files:
                if not name.endswith(".cpp"):
                    continue
                with open(os.path.join(base, name), encoding="utf-8", errors="replace") as handle:
                    text = source_text(handle.read())
                for full in names:
                    short = full.split(".", 1)[-1]
                    suite = full.split(".", 1)[0] if "." in full else None
                    pattern = r"TEST(?:_F|_P)?\(\s*%s\s*,\s*%s\s*\)" % (
                        re.escape(suite) if suite else r"\w+", re.escape(short))
                    if re.search(pattern, text):
                        found.add(full)
    return found


def ctest_names_in(repo, names):
    """Finds requested add_test names in CMake text, without evaluating conditions."""
    text = ""
    for base, _, files in os.walk(os.path.join(repo, "cmake")):
        for name in files:
            if name.endswith(".cmake"):
                with open(os.path.join(base, name), encoding="utf-8", errors="replace") as handle:
                    text += source_text(handle.read(), cmake=True) + "\n"
    with open(os.path.join(repo, "CMakeLists.txt"), encoding="utf-8", errors="replace") as handle:
        text += source_text(handle.read(), cmake=True) + "\n"
    return {n for n in names if re.search(r'add_test\(\s*NAME\s+"?%s(?:"|\s|\))' % re.escape(n), text)}


# The twenty-six, in the mission's own order and words. Each is (group, statement, kind, argument).
CHECKS = [
    ("API", "128/128 public and protected types, or a regenerated denominator",
     "parity", ("types", 128)),
    ("API", "705/705 relevant members, or a regenerated denominator",
     "parity", ("members", 705)),
    ("API", "27/27 enum values", "parity", ("enumValues", 27)),
    ("API", "MISSING API = 0", "parity", ("missing", 0)),

    ("Importers", "10/10 built-in importers", "parity", ("importers", 10)),
    ("Importers", "18/18 declared source extensions IMPLEMENTED+TESTED", "inputs", None),
    ("Importers", "`.xml` included via the generic registered-type architecture",
     "files", ["modules/content-pipeline/src/XnaXmlSourceContentPipeline.cpp",
               "modules/content-pipeline/include/CNA/Content/Pipeline/XnaXmlSourceContentPipeline.hpp",
               "modules/content-pipeline/tests/CNA/Content/Pipeline/XnaXmlSourceRouteTests.cpp"]),

    ("Processors", "12/12 built-in processors", "parity", ("processors", 12)),
    ("Processors", "47/47 processor properties", "parity", ("processorProperties", 47)),
    ("Processors", "every default measured and tested", "gate",
     ["processor_defaults_gate.py", "--inventory", "tests/reference/xna40/content-pipeline-api.json",
      "--test", "modules/content-pipeline/tests/Microsoft/Xna/Framework/Content/Pipeline/"
                "Processors/XnaProcessorDefaultsTests.cpp"]),
    # The three halves XNAPP-138 names: omitted values (every processor constructed and read),
    # explicit values (the per-extension processor legs, of which the texture one drives all
    # seventeen property cases), and the interactions the measurement actually found.
    ("Processors", "processor test matrix complete", "tests",
     ["XnaProcessorDefaults.TheThreeTextureProcessorsDifferOnlyWhereXnasDo",
      "XnaProcessorDefaults.TheAudioAndVideoProcessorsAnswerWhatXnasDid",
      "XnaTextureExtension.EveryTextureProcessorPropertyAnswersWhatXnaAnswers"]),

    ("Product", "`cna-content build Foo.contentproj` works", "tests",
     ["XnaContentProjectCommandLine.AProjectIsBuiltWithItsOwnImportersProcessorsAndNames",
      "XnaContentProjectCommandLine.AnItemPathSpelledTheWayMsBuildSpellsItResolves"]),
    ("Product", "copy items work", "tests",
     ["XnaContentProjectCommandLine.ContentAndNoneItemsAreCopiedAsTheirMetadataSays"]),
    ("Product", "representative content projects build end to end", "tests",
     ["ContentPipelineCMakeIntegrationTest.HelperBuildsAnXnaContentProject",
      "ContentPipelineCMakeIntegrationTest.HelperBuildsTheXnbContainerWhenAskedTo"]),
    ("Product", "a custom C++ pipeline project works, including custom XML content", "tests",
     ["XnaCustomPipelineAcceptanceTest.AUsersOwnRouteReachesAnXnbUnderItsOwnNames",
      "XnaCustomPipelineAcceptanceTest.AGameDefinedTypeArrivesFromXnaStyleXmlUnderItsOwnNames"]),

    ("Differential", "no OPEN differential cases", "tests",
     ["XnaDifferentialBuildTest.CnaAcceptsAndRefusesTheSameSourcesXnaDoes"]),
    ("Differential", "accepted decisions remain live only while differences remain", "files",
     ["tools/xna-pipeline-oracle/differential/decisions.json",
      "tools/xna-pipeline-oracle/differential/compare.py"]),
    ("Differential", "genuine XNA measurements preserved", "files",
     ["tests/reference/xna40/differential/differential-oracle.json",
      "tests/reference/xna40/differential-errors/differential-oracle.json",
      "tools/xna-pipeline-oracle/differential/corpus.json"]),

    ("Runtime", "every locally executable output family tested in a genuine XNA 4.0 runtime",
     "ctests", ["XnaPipelineGenuineRuntimeInterop", "XnaPipelineGenuineRuntimeInteropLzx",
                "XnaPipelineGenuineRuntimeBuiltFamilies"]),

    ("Quality", "the remaining parsers are fuzzed and sanitized", "tests",
     ["XnaTextureReaderHardening.NoMutatedDdsCrashesOrEscapesItsOwnRefusal",
      "XnaTextureReaderHardening.NoMutatedPortableFloatMapOrRadiancePictureEscapesItsOwnRefusal",
      "XnaContentProjectHardening.NoMutatedProjectEscapesItsOwnRefusal"]),
    ("Quality", "the determinism campaign is done", "tests",
     ["XnaBuildDeterminism.EveryRouteBuildsToTheSameBytesAtEveryWorkerCount",
      "XnaBuildDeterminism.ASecondProcessSkipsWhatTheFirstBuilt",
      "XnaBuildDeterminism.TheCnbContainerIsDeterministicToo"]),
    ("Quality", "the performance campaign is done", "tests",
     ["XnaRouteScaling.TheTextureRouteIsNotQuadraticInItsPixels",
      "XnaRouteScaling.TheCoordinatorIsNotQuadraticInTheNumberOfAssets"]),
    ("Quality", "the provenance gate is done", "ctests",
     ["CnaProvenanceGate", "CnaProvenanceGateSelfTest"]),
    ("Quality", "all parity gates are wired to ctest", "ctests",
     ["XnaPipelineParityReportIsCurrent", "XnaPipelineInputParityMatrixIsCurrent",
      "XnaPipelineParityGateIsGreen", "XnaPipelineInventoryIsFrozen",
      "XnaPipelineProcessorDefaultsMatchTheMeasurement", "XnaPipelineWhitespaceIsClean",
      "XnaPipelineComponentReferenceIsCurrent"]),
    ("Quality", "the documentation and migration guide are complete", "files",
     ["docs/xna-content-pipeline-migration.md", "docs/xna-content-pipeline-components.md",
      "docs/xna-content-pipeline-compat-api.md", "docs/xna-content-pipeline-parity-report.md",
      "docs/xna-intermediate-xml-format.md", "docs/xnb-interoperability.md",
      "docs/xma-encoder-backend.md", "docs/content-pipeline-benchmark.md"]),
    ("Quality", "the final audit is complete", "ctests",
     ["XnaPipelineFinalAuditIsGreen"]),
]


def run(repo, summary, inputs):
    """Answers every check, as (ok, evidence) in the order above."""
    answers = []
    for group, statement, kind, argument in CHECKS:
        if kind == "parity":
            key, expected = argument
            # The report's summary answers each count as (implemented, denominator); a row holds
            # when the two are equal *and* equal to what the mission named, so a regenerated
            # denominator shows up as a difference rather than passing silently.
            got = {
                "types": summary["types"],
                "members": summary["members"],
                "enumValues": summary["enumValues"],
                "missing": (summary["missingTypes"] + summary["missingMembers"] +
                            summary["missingEnumValues"], 0),
                "importers": summary["importers"],
                "processors": summary["processors"],
                "processorProperties": summary["properties"],
            }[key]
            implemented, denominator = got
            if key == "missing":
                answers.append((implemented == expected,
                                "%d MISSING across types, members and enum values" % implemented))
            else:
                answers.append((implemented == denominator == expected,
                                "%d/%d (the mission named %d)"
                                % (implemented, denominator, expected)))
        elif kind == "inputs":
            rows = inputs["extensions"]
            done = [e for e, row in rows.items()
                    if row.get("cna", {}).get("status") == "IMPLEMENTED+TESTED"]
            answers.append((len(done) == len(rows) == 18,
                            "%d/%d extensions IMPLEMENTED+TESTED" % (len(done), len(rows))))
        elif kind == "files":
            missing = [p for p in argument if not os.path.exists(os.path.join(repo, p))]
            answers.append((not missing, "%d artefact(s), %s" % (
                len(argument), "all present" if not missing else "missing " + ", ".join(missing))))
        elif kind == "tests":
            found = test_names_in(repo, argument)
            missing = sorted(set(argument) - found)
            answers.append((not missing, "%d named test(s), %s" % (
                len(argument), "declaration patterns found" if not missing else "not found: " + ", ".join(missing))))
        elif kind == "ctests":
            found = ctest_names_in(repo, argument)
            missing = sorted(set(argument) - found)
            answers.append((not missing, "%d ctest(s), %s" % (
                len(argument), "CMake declaration patterns found" if not missing
                else "source pattern absent: " + ", ".join(missing))))
        elif kind == "gate":
            command = [sys.executable, os.path.join(HERE, argument[0])]
            for one in argument[1:]:
                command.append(os.path.join(repo, one) if "/" in one else one)
            finished = subprocess.run(command, capture_output=True, text=True)
            answers.append((finished.returncode == 0,
                            "%s exited %d" % (argument[0], finished.returncode)))
        else:  # pragma: no cover - the table is written here, not by a user
            answers.append((False, "unknown check kind " + kind))
    return answers


def render(answers):
    lines = ["# XNA 4.0 Content Pipeline final audit\n",
             "> **Generated** by `tools/xna-pipeline-oracle/final_audit.py` from the campaign's"
             " twenty-six acceptance conditions (`XNAPP-330`). Do not edit by hand."
             " `XnaPipelineFinalAuditIsGreen` checks this generated file and its evidence predicates.\n",
             "A PASS means the row's **source/metadata predicate** succeeds. It does not declare"
             " that the recorded acceptance condition is currently fulfilled. The script neither"
             " runs the referenced gtests/interop/sanitizers nor verifies arbitrary semantic claims.\n",
             "## What the check kinds establish\n",
             "- `parity`: counts/statuses joined from the maintained parity map and inventory;"
             " no compiled API or runtime rediscovery.\n"
             "- `inputs`: eighteen recorded IMPLEMENTED+TESTED flags; no source-asset execution.\n"
             "- `files`: path existence, not content validity, freshness or documentation completeness.\n"
             "- `tests`: TEST/TEST_F/TEST_P declaration presence, not compiled or passing cases.\n"
             "- `ctests`: add_test name patterns, not configured registrations or passing jobs.\n"
             "- `gate`: executes the named helper; here it compares processor-default annotations"
             " with recorded measurements, without running the C++ assertions.\n",
             "Comments, string literals and literal `#if 0` branches are excluded from C/C++ scans;"
             " CMake scans exclude comments and literal `if(FALSE)`/`if(0)` branches. Unknown"
             " conditions, macros and build inclusion are not evaluated. Declaration presence is"
             " separate from configured CTest registration and actual execution.\n",
             "Use `--configured-inventory configured.json` for registration candidates, and repeat"
             " `--execution-record /path/to/record.json` to join retained runs from"
             " [ctest_evidence.py](execution-evidence.md). `--evidence-report layers.json` writes"
             " the separate layers; `--require-execution` fails unless every named witness below"
             " (except this self-audit) has validated passing execution. C++ cases and Runtime-group"
             " CTests require runtime-kind captures; metadata captures cannot substitute. Aggregate"
             " gtest commands also need exact RUN/OK case output, not a zero-case exit. Entries labelled"
             " `metadata`/`declaration-presence` cannot substitute for runtime witnesses. All captures"
             " must describe the same source/build/configuration identity and configured inventory; record paths,"
             " hashes and identities are retained.\n",
             "**Runtime qualification: not established by this audit**, even with passing execution."
             " CTest does not count assertions or prove semantic acceptance: an empty test body can"
             " pass. Missing assertion evidence stays unknown, never a zero-assertion qualification."
             " The helper gates, declarations and recorded status flags remain metadata; the"
             " self-audit is excluded from required execution to avoid circular evidence. Without"
             " optional artifacts, registration and execution are not established. Retained captures"
             " are historical observations, not proof that current binaries were rebuilt or that"
             " dirty source bytes are unchanged.\n",
             "GPU/window runs must use the private"
             " runner described in [content-pipeline.md](content-pipeline.md). Current defects and"
             " external qualification limits remain in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md),"
             " [the pipeline guide](content-pipeline.md) and"
             " [the media evidence](xna-content-pipeline-media.md). A green table does not close them.\n",
             "## Recorded acceptance conditions and their witnesses\n",
             "| # | Group | Recorded acceptance condition | Check kind | Evidence result | Evidence |",
             "|---:|---|---|---|---|---|"]
    for index, ((group, statement, kind, _), (ok, evidence)) in enumerate(zip(CHECKS, answers), 1):
        lines.append("| %d | %s | %s | `%s` | %s | %s |" % (
            index, group, statement, kind, "PASS" if ok else "**FAIL**", evidence))
    passed = sum(1 for ok, _ in answers if ok)
    lines.append("")
    lines.append("**%d of %d source/metadata checks pass.**" % (passed, len(answers)))
    return "\n".join(lines) + "\n"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", required=True)
    ap.add_argument("--output", default=None)
    ap.add_argument("--check", action="store_true")
    add_arguments(ap)
    args = ap.parse_args(argv[1:])

    reference = os.path.join(args.repo, "tests", "reference", "xna40")
    inventory = parity_report.load(os.path.join(reference, "content-pipeline-api.json"))
    pmap = parity_report.load(os.path.join(reference, "content-pipeline-parity-map.json"))
    inputs = parity_report.load(os.path.join(reference, "content-pipeline-inputs.json"))
    _, _, summary = parity_report.build_report(inventory, pmap, inputs)

    answers = run(args.repo, summary, inputs)
    required = {name: kind == "tests" or group == "Runtime"
                for group, _, kind, names in CHECKS if kind in ("tests", "ctests")
                for name in names if name != "XnaPipelineFinalAuditIsGreen"}
    layers = report_layers({"passed": sum(ok for ok, _ in answers), "total": len(answers),
                            "scope": "source/metadata predicates only"}, required, args)
    document = render(answers)
    output = args.output or os.path.join(args.repo, "docs", "xna-content-pipeline-final-audit.md")

    failed = [(index, CHECKS[index - 1][1])
              for index, (ok, _) in enumerate(answers, 1) if not ok]
    stale = False
    if args.check:
        try:
            with open(output, encoding="utf-8") as handle:
                committed = handle.read()
        except OSError as error:
            print("cannot read %s: %s" % (output, error))
            return 1
        stale = committed != document
        if stale:
            print("  problem: %s is not what this run writes; regenerate it" % output)
    else:
        with open(output, "w", encoding="utf-8") as handle:
            handle.write(document)
    for index, statement in failed:
        print("  FAIL %d: %s" % (index, statement))
    print("final audit: %d of %d source/metadata checks pass" %
          (len(answers) - len(failed), len(answers)))
    return 1 if failed or stale or (args.require_execution and
                                  layers["actual_execution"]["status"] != "observed") else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
