#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-330: the mission's own definition of done, executed.

The mission that opened this plan ends with twenty-six conditions, in seven groups, that must all
hold before the local work can be called complete. Written as prose they are twenty-six claims
somebody has to believe; run from here they are twenty-six checks against the same measured files
and the same test sources everything else in this plan is checked against.

Each row names **where its evidence lives**, and the check is that the evidence is really there and
really says what the row claims:

  * `parity`  -- a count read from the generated parity report's own summary, which is itself the
    join of the frozen inventory with the map;
  * `inputs`  -- the eighteen-extension matrix, read directly;
  * `tests`   -- named gtest cases that must exist in the tree; a renamed or deleted test fails the
    audit rather than silently unproving a row;
  * `ctests`  -- named ctests that must be registered in CMake, for the gates that are not gtest;
  * `files`   -- artefacts that must exist, for the documents and fixtures a row rests on;
  * `gate`    -- another gate script, run for its exit status.

What this cannot do is re-run the whole suite, and it does not pretend to: a test that exists but
fails is a red suite, and the suite is where that is answered. This answers the other question --
whether every one of the twenty-six has something behind it at all.

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
import parity_report  # noqa: E402  (path is set above on purpose)


def test_names_in(repo, names):
    """Every named gtest case that is really declared somewhere under the tests trees."""
    found = set()
    wanted = {n.split(".", 1)[1] if "." in n else n: n for n in names}
    for root in ("modules", "tests"):
        for base, _, files in os.walk(os.path.join(repo, root)):
            for name in files:
                if not name.endswith(".cpp"):
                    continue
                with open(os.path.join(base, name), encoding="utf-8", errors="replace") as handle:
                    text = handle.read()
                for short, full in wanted.items():
                    suite = full.split(".", 1)[0] if "." in full else None
                    pattern = r"TEST(?:_F|_P)?\(\s*%s\s*,\s*%s\s*\)" % (
                        re.escape(suite) if suite else r"\w+", re.escape(short))
                    if re.search(pattern, text):
                        found.add(full)
    return found


def ctest_names_in(repo, names):
    """Every named ctest really registered by a CMake file in this tree."""
    text = ""
    for base, _, files in os.walk(os.path.join(repo, "cmake")):
        for name in files:
            if name.endswith(".cmake"):
                with open(os.path.join(base, name), encoding="utf-8", errors="replace") as handle:
                    text += handle.read()
    with open(os.path.join(repo, "CMakeLists.txt"), encoding="utf-8", errors="replace") as handle:
        text += handle.read()
    return {n for n in names if re.search(r"add_test\(\s*NAME\s+%s\b" % re.escape(n), text)}


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
                len(argument), "all declared" if not missing else "not found: " + ", ".join(missing))))
        elif kind == "ctests":
            found = ctest_names_in(repo, argument)
            missing = sorted(set(argument) - found)
            answers.append((not missing, "%d ctest(s), %s" % (
                len(argument), "all registered" if not missing
                else "not registered: " + ", ".join(missing))))
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
             "> **Generated** by `tools/xna-pipeline-oracle/final_audit.py`. Twenty-six conditions,"
             " in the mission's own order and words, each checked against the file or the test that"
             " is its evidence. Do not edit by hand. The ctest `XnaPipelineFinalAuditIsGreen` runs"
             " it. Task log: `plans/plan_xnapipeline_parity.md` `XNAPP-330`.\n",
             "A check answers whether a row's evidence exists and says what the row claims. Whether"
             " that evidence *passes* is the test suite's own question, and section 33.1 of the plan"
             " records the suite's baseline.\n",
             "| # | Group | Condition | | Evidence |",
             "|---:|---|---|---|---|"]
    for index, ((group, statement, _, _), (ok, evidence)) in enumerate(zip(CHECKS, answers), 1):
        lines.append("| %d | %s | %s | %s | %s |" % (
            index, group, statement, "PASS" if ok else "**FAIL**", evidence))
    passed = sum(1 for ok, _ in answers if ok)
    lines.append("")
    lines.append("**%d of %d conditions hold.**" % (passed, len(answers)))
    lines.append("")
    return "\n".join(lines) + "\n"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", required=True)
    ap.add_argument("--output", default=None)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv[1:])

    reference = os.path.join(args.repo, "tests", "reference", "xna40")
    inventory = parity_report.load(os.path.join(reference, "content-pipeline-api.json"))
    pmap = parity_report.load(os.path.join(reference, "content-pipeline-parity-map.json"))
    inputs = parity_report.load(os.path.join(reference, "content-pipeline-inputs.json"))
    _, _, summary = parity_report.build_report(inventory, pmap, inputs)

    answers = run(args.repo, summary, inputs)
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
    print("final audit: %d of %d conditions hold" % (len(answers) - len(failed), len(answers)))
    return 1 if failed or stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
