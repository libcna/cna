#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Builds every committed glTF fixture to Model XNB and checks the result against a manifest.

plans/plan_xnapipeline.md XNAP-59. The Model route's test matrix was covered by unit tests plus an
ad-hoc sweep somebody ran once and wrote a sentence about. A sweep nobody can re-run is a claim,
not a test: it cannot notice a fixture that stopped building, a refusal that started saying
something else, or a fixture added later whose Model-XNB outcome nobody looked at.

This is that sweep, committed. For every `*.glb` in the corpus it runs the real `cna-content` to
`--format xnb` and compares the outcome with `model-xnb-corpus.json`:

  * a fixture the manifest says builds must build, and its `.xnb` must then be accepted by the
    independent specification parser (`xnb_conformance.py`) -- CNA's own writer agreeing with
    itself proves nothing;
  * a fixture the manifest says is refused must be refused, and by the *category* recorded for it,
    so a topology refusal turning into a crash-shaped import failure is a regression rather than a
    still-red row;
  * a fixture in neither list fails the gate. That is the point: a `.glb` added tomorrow has an
    unreviewed Model-XNB outcome until somebody looks at it and records which it is.

The corpus is derived from the directory, never from a hard-coded count, so adding or removing a
fixture changes what runs without editing this file.

Usage:
    python3 tools/xnb/model_corpus_sweep.py --content-tool <path-to-cna-content> [--update]

`--update` rewrites the manifest from what the corpus does *now*. It is a convenience for the
reviewer, who then reads the diff; it is not a way to make the gate green.

Exit code 0 when every fixture matches the manifest, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
CORPUS = REPOSITORY / "tests" / "assets" / "gltf"
MANIFEST = CORPUS / "model-xnb-corpus.json"
CONFORMANCE = REPOSITORY / "tools" / "xnb" / "xnb_conformance.py"

# One recognizable phrase per refusal category. The phrase is the part of the diagnostic that
# carries the *reason*; the rest (paths, counts, names) is deliberately not pinned, because a
# refusal is contractual and its prose is not.
CATEGORIES = {
    # The source is malformed or exceeds a hard limit, and the glTF importer says so. Shared with
    # the CNB route: these never reach an XNB writer.
    "import-refused": (
        "Import (CNA.GltfImporter)",
    ),
    # KHR_draco_mesh_compression in extensionsRequired with no Draco decoder compiled in. This one
    # is configuration-dependent: with -DCNA_ENABLE_DRACO=ON these fixtures build instead.
    "draco-required": (
        "Import (CNA.GltfImporter)",
        "KHR_draco_mesh_compression",
    ),
    # A required extension CNA does not implement, other than Draco.
    "extension-required": (
        "Import (CNA.GltfImporter)",
        "extensionsRequired",
    ),
    # An XNA Model mesh part is always a triangle list. Refused by the XNB writer, not the
    # importer, because the CNB route represents the topology honestly.
    "topology-not-triangles": (
        "Write (CNA.XnbModelWriter)",
        "triangle list",
    ),
    # glTF material variants, which CNB schema 1 has no representation for.
    "material-variants": (
        "material variants",
    ),
    # The source describes more than one Model and has not opted into child assets.
    "multi-model-source": (
        "generateChildAssets",
    ),
}


def load_manifest() -> dict:
    if not MANIFEST.is_file():
        return {"builds": [], "refuses": {}}
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def write_manifest(builds: list[str], refuses: dict[str, str]) -> None:
    document = {
        "comment": [
            "plans/plan_xnapipeline.md XNAP-59: the expected Model-XNB outcome of every committed",
            "glTF fixture. tools/xnb/model_corpus_sweep.py (ctest CnaXnbModelCorpusSweep) checks",
            "the corpus against it. A fixture in neither list fails the gate until somebody",
            "records which it is; regenerate with --update and read the diff.",
            "Refusal categories are defined in the sweep tool, not here.",
        ],
        "builds": sorted(builds),
        "refuses": dict(sorted(refuses.items())),
    }
    MANIFEST.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def build_fixture(tool: Path, fixture: Path, workspace: Path) -> tuple[bool, str, Path]:
    """Builds one fixture to Model XNB in its own tree. Returns (succeeded, output, xnb path)."""
    source = workspace / "src"
    output = workspace / "out"
    source.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(fixture, source / "model.glb")
    completed = subprocess.run(
        [str(tool), "build", str(source), "-o", str(output), "--format", "xnb", "--quiet"],
        capture_output=True,
        text=True,
        check=False,
    )
    text = completed.stdout + completed.stderr
    return completed.returncode == 0, text, output / "model.xnb"


def classify(text: str) -> str | None:
    """Returns the refusal category @p text matches, preferring the most specific one."""
    matched = [
        name
        for name, phrases in CATEGORIES.items()
        if all(phrase in text for phrase in phrases)
    ]
    if not matched:
        return None
    # "import-refused" is a prefix of the two extension categories; the specific one wins.
    matched.sort(key=lambda name: len(CATEGORIES[name]), reverse=True)
    return matched[0]


def check_conformance(python: str, xnb: Path) -> str:
    """Runs the independent parser over @p xnb; returns an empty string when it accepts."""
    completed = subprocess.run(
        [python, str(CONFORMANCE), str(xnb)], capture_output=True, text=True, check=False
    )
    if completed.returncode == 0:
        return ""
    return (completed.stdout + completed.stderr).strip()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--content-tool", required=True, type=Path,
                        help="the cna-content executable to sweep with")
    parser.add_argument("--update", action="store_true",
                        help="rewrite the manifest from what the corpus does now")
    parser.add_argument("--python", default=sys.executable,
                        help="interpreter for the independent conformance parser")
    arguments = parser.parse_args(argv[1:])

    if not arguments.content_tool.is_file():
        print(f"{arguments.content_tool}: not an executable file.", file=sys.stderr)
        return 1
    fixtures = sorted(CORPUS.glob("*.glb"))
    if not fixtures:
        print(f"{CORPUS}: no .glb fixtures found; the corpus location must have changed.",
              file=sys.stderr)
        return 1

    manifest = load_manifest()
    expectedBuilds = set(manifest.get("builds", []))
    expectedRefuses = dict(manifest.get("refuses", {}))

    problems: list[str] = []
    builds: list[str] = []
    refuses: dict[str, str] = {}

    with tempfile.TemporaryDirectory(prefix="cna_model_corpus_") as scratch:
        for index, fixture in enumerate(fixtures):
            name = fixture.stem
            succeeded, text, xnb = build_fixture(
                arguments.content_tool, fixture, Path(scratch) / str(index))
            if succeeded:
                builds.append(name)
            else:
                category = classify(text)
                refuses[name] = category if category is not None else "unrecognized"

            if arguments.update:
                continue

            known = name in expectedBuilds or name in expectedRefuses
            if not known:
                problems.append(
                    f"{fixture.name}: no recorded Model-XNB outcome. A new fixture is "
                    f"unclassified until somebody looks at it: it "
                    f"{'builds' if succeeded else 'is refused'} today. Re-run with --update and "
                    f"review the diff.")
                continue
            if name in expectedBuilds and name in expectedRefuses:
                problems.append(f"{fixture.name}: listed as both building and refused.")
                continue

            if name in expectedBuilds:
                if not succeeded:
                    problems.append(
                        f"{fixture.name}: the manifest says this builds to Model XNB, and it no "
                        f"longer does:\n    {text.strip()}")
                    continue
                if not xnb.is_file():
                    problems.append(f"{fixture.name}: the build reported success and wrote no "
                                    f"{xnb.name}.")
                    continue
                rejection = check_conformance(arguments.python, xnb)
                if rejection:
                    problems.append(
                        f"{fixture.name}: the independent specification parser rejects the "
                        f"generated .xnb:\n    {rejection}")
                continue

            expected = expectedRefuses[name]
            if succeeded:
                problems.append(
                    f"{fixture.name}: the manifest says this is refused ({expected}), and it "
                    f"built. If that is the intended change, record it with --update.")
                continue
            actual = refuses[name]
            if actual != expected:
                problems.append(
                    f"{fixture.name}: expected refusal category '{expected}', got '{actual}':\n"
                    f"    {text.strip()}")

    if arguments.update:
        write_manifest(builds, refuses)
        print(f"{MANIFEST}: recorded {len(builds)} building and {len(refuses)} refused fixtures "
              f"out of {len(fixtures)}.")
        return 0

    missing = (expectedBuilds | set(expectedRefuses)) - {fixture.stem for fixture in fixtures}
    for name in sorted(missing):
        problems.append(f"{name}: recorded in the manifest, but no such fixture exists.")

    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"{len(problems)} model-corpus problem(s) over {len(fixtures)} fixtures.",
              file=sys.stderr)
        return 1
    print(f"{len(fixtures)} glTF fixtures: {len(builds)} build to Model XNB and are accepted by "
          f"the independent parser, {len(refuses)} are refused by their recorded category.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
