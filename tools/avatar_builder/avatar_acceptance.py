#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""The Avatar v3 acceptance command: one run that says whether Avatar v3 is whole in a build tree.

    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi                  # its default renderer
    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --all-renderers  # every one compiled in
    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --renderer SDL_GPU --renderer SOFTWARE

Stages, each of which must pass:

  catalog    catalog.json, the generator, the codec and CMake agree on the catalog version,
             revision and reader level; every id is unique; outfits, presets and rules name what exists
  docs       what docs/avatars.md, the limitations page and the generator's README state about the
             catalog (revision, counts, files, size) is what catalog.json holds
  generator  generate_avatar_catalog.py --check: the checked-in catalog is exactly what it generates
  tests      per renderer, the avatar test components below (codec, description, catalog, content and
             geometry, animation, renderer, editor, Guide editor, Gamer Services persistence,
             deterministic randomization): each must run at least its pinned number of tests, none may
             fail, and only the listed skips are allowed -- a component that stops running fails
  visual     per renderer that draws, avatar_visual_regression.py check against the goldens

Every test binary and the visual check run through tools/platform/run_gpu_tests_private.sh (a private
display, never the desktop). `--skip STAGE` leaves a stage out on purpose; the summary says so.
Writes acceptance.json and every stage's log to --out. Exit status 0 only when every stage passed.
"""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CATALOG = ROOT / "modules" / "gamer-services" / "assets" / "avatars" / "v3"
RUNNER = ROOT / "tools" / "platform" / "run_gpu_tests_private.sh"
NO_PIXELS = {"HEADLESS", "STUB"}

RANDOMIZATION = [
    "AvatarCatalogContentTest.RandomAvatarsAreRepeatableVariedAndBelievable",
    "AvatarDescriptionCodecTest.RandomDescriptorsAreAlwaysEncodable",
    "AvatarDescriptionCodecTest.RandomAvatarsHaveIndividualFaces",
    "AvatarDescriptionTest.CreateRandomReturnsAValidMeasuredAvatar",
    "AvatarDescriptionTest.CreateRandomWithBodyTypeHonoursTheBodyType",
    "AvatarDescriptionTest.CreateRandomProducesDifferentAvatars",
    "AvatarDescriptionTest.CreateRandomWithInvalidBodyTypeThrows",
    "AvatarEditorTest.WithoutAStoredAvatarItStartsFromARandomOne",
    "AvatarEditorTest.ShufflingAnAccessoryCategoryAlwaysWearsOne",
    "AvatarEditorTest.RandomizeKeepsTheBodyAndRevertAndSaveTrackTheStoredAvatar",
    "AvatarEditorTest.RandomAvatarsAreCoherent",
    "AvatarEditorTest.RandomizingACategoryChangesOnlyIt",
]
# (component, gtest patterns, the number of tests it has today). Raise a count when tests are added;
# lowering one is a decision to drop coverage and belongs in the same commit as the removal.
COMPONENTS = [
    ("codec and serialization", ["AvatarDescriptionCodecTest.*"], 18),
    ("AvatarDescription (XNA)", ["AvatarDescriptionTest.*", "AvatarBodyTypeTest.*"], 23),
    ("catalog", ["AvatarCatalogTest.*"], 18),
    ("catalog content and geometry", ["AvatarCatalogContentTest.*"], 15),
    ("animation and expressions", ["AvatarAnimationTest.*", "AvatarExpressionTest.*", "AvatarAnimationPresetTest.*", "AvatarBoneTest.*",
                                   "AvatarEyeTest.*", "AvatarMouthTest.*", "AvatarEyebrowTest.*"], 35),
    ("AvatarRenderer (XNA)", ["AvatarRendererTest.*", "AvatarRendererStateTest.*"], 35),
    ("editor", ["AvatarEditorTest.*"], 27),
    ("Guide editor", ["GuideAvatarEditorTest.*", "GuideAvatarEditorDeviceTest.*", "GuideAvatarEditorFramingTest.*"], 18),
    ("Gamer Services persistence", ["AvatarServiceTest.*", "AvatarChangedTest.*", "AvatarCatalogProcessTest.*",
                                    "LocalProfileStoreTest.MalformedEntriesAreSkippedAndALostAvatarIsReplaced",
                                    "LocalProfileStoreTest.ADevelopmentCatalogAvatarIsReplacedByANewBaselineAvatar",
                                    "LocalSignInTest.ALocalProfilesAvatarIsItsStoredDescription",
                                    "LocalSignInTest.SavingAnotherAvatarIntoTheProfileRaisesChanged"], 37),
    ("deterministic randomization", RANDOMIZATION, len(RANDOMIZATION)),
]
# Skips that are not a lost component: test -> (renderers it may skip on, or None for any; why).
ALLOWED_SKIPS = {
    "AvatarRendererTest.AFailedGpuUploadFailsEveryDrawCleanlyRatherThanHalfBuilding":
        (None, "needs a renderer that refuses vertex buffers; none of today's does"),
    "AvatarRendererTest.DrawsEveryPresetAndExpressionOnTheDispatcherDevice": ({"STUB"}, "STUB has no 3D pipeline"),
    "AvatarRendererTest.EveryFormat3FeatureDrawsAndANewDeviceGetsItsOwnResources": ({"STUB"}, "STUB has no 3D pipeline"),
}


def matches(name, patterns):
    return any(fnmatch.fnmatchcase(name, p) for p in patterns)


def cache_value(build, key):
    found = re.search(r"^%s:[A-Z]+=(.*)$" % re.escape(key), (build / "CMakeCache.txt").read_text(), re.M)
    return found.group(1).strip() if found else ""


# ----- catalog and documentation ----------------------------------------------------------------------

def stage_catalog():
    manifest = json.loads((CATALOG / "catalog.json").read_text())
    problems = []
    generator = (HERE / "generate_avatar_catalog.py").read_text()
    constant = lambda name: int(re.search(r"^%s = (\d+)" % name, generator, re.M).group(1))
    codec = (ROOT / "modules/gamer-services/include/CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp").read_text()
    assets = (ROOT / "modules/gamer-services/include/CNA/Internal/GamerServices/AvatarAssets.hpp").read_text()
    cmake = (ROOT / "modules/gamer-services/CMakeLists.txt").read_text()
    base = int(re.search(r"BaseCatalogVersion = (\d+);", codec).group(1))
    reader = int(re.search(r"CatalogReaderLevel=(\d+);", assets).group(1))
    shipped = re.search(r"set\(CNA_AVATAR_CATALOG_VERSIONS ([0-9 ]+)\)", cmake).group(1).split()
    if not manifest["catalogVersion"] == constant("CATALOG_VERSION") == base or shipped != [str(base)]:
        problems.append("catalog version: manifest %s, generator %s, codec BaseCatalogVersion %s, CMake %s"
                        % (manifest["catalogVersion"], constant("CATALOG_VERSION"), base, shipped))
    if manifest["revision"] != constant("CATALOG_REVISION"):
        problems.append("revision: manifest %s, generator %s" % (manifest["revision"], constant("CATALOG_REVISION")))
    if not manifest["reader"] == constant("READER") <= reader:
        problems.append("reader level: manifest %s, generator %s, CatalogReaderLevel %s" % (manifest["reader"], constant("READER"), reader))
    entries = [e for key in ("items", "featureItems", "faceStyles") for e in manifest[key]]
    ids = [e["id"] for e in entries]
    duplicated = sorted({i for i in ids if ids.count(i) > 1})
    if duplicated:
        problems.append("duplicate ids: %s" % duplicated)
    by_id = {e["id"]: e for e in entries}
    slots = {e["slot"] for e in entries}
    for outfit in manifest["outfits"]:
        for slot, item in outfit["items"].items():
            if item and (item not in by_id or by_id[item]["slot"] != slot):
                problems.append("outfit %s names %s %s" % (outfit["name"], slot, item))
    for entry in entries:
        for kind in entry.get("hides", []):
            if kind not in slots:
                problems.append("%s hides an unknown kind %s" % (entry["name"], kind))
        assets = entry.get("assets", {})
        for asset in (assets.values() if isinstance(assets, dict) else assets):
            if asset and not (CATALOG / asset).is_file():
                problems.append("%s names a missing file %s" % (entry["name"], asset))
    controls = {c["name"] for c in manifest["faceControls"]["male"]} | {"height", "build", "proportion"}
    for preset in manifest["facePresets"]:
        unknown = set(preset["values"]) - controls
        if unknown:
            problems.append("preset %s/%s sets unknown controls %s" % (preset["group"], preset["name"], sorted(unknown)))
    listed = {a["name"] for a in manifest["assets"]}
    on_disk = {p.name for p in CATALOG.iterdir()} - {"catalog.json"}
    if listed != on_disk:
        problems.append("catalog.json and the directory disagree: %s" % sorted(listed ^ on_disk)[:6])
    summary = "catalog v%d revision %d, reader %d; %d ids unique; %d outfits, %d presets, %d files resolve" % (
        manifest["catalogVersion"], manifest["revision"], manifest["reader"], len(ids), len(manifest["outfits"]),
        len(manifest["facePresets"]), len(listed))
    return problems, summary


def stage_docs():
    manifest = json.loads((CATALOG / "catalog.json").read_text())
    count = lambda key, slot: sum(1 for e in manifest[key] if e["slot"] == slot)
    files = list(CATALOG.iterdir())
    size_mb = round(sum(p.stat().st_size for p in files) / 1e6, 1)
    avatars = (ROOT / "docs/avatars.md").read_text()
    limits = (ROOT / "docs/gamer-services-known-limitations.md").read_text()
    readme = (HERE / "README.md").read_text()
    revision = manifest["revision"]
    # (document, pattern, expected values of its groups)
    claims = [
        ("docs/avatars.md", avatars, r'\*\*revision (\d+)\*\* \(manifest `"revision": (\d+)`\)', (revision, revision)),
        ("docs/avatars.md", avatars, r"(\d+) wardrobe items and accessories", (len(manifest["items"]),)),
        ("docs/avatars.md", avatars, r"(\d+) hairstyles \(", (count("items", "hair"),)),
        ("docs/avatars.md", avatars, r"(\d+) tops, (\d+) bottoms, (\d+) shoes, (\d+) glasses,\s+(\d+) headwear, (\d+) earrings, "
                                     r"(\d+) wristwear, (\d+) gloves and (\d+) rings",
         tuple(count("items", s) for s in ("top", "bottom", "shoes", "glasses", "hat", "earrings", "wristwear", "gloves", "rings"))),
        ("docs/avatars.md", avatars, r"(\d+) facial-hair styles \(stubble to full beards\) and (\d+) ear shapes",
         (count("featureItems", "facialHair"), count("featureItems", "ears"))),
        ("docs/avatars.md", avatars, r"(\d+) eyebrow styles", (count("faceStyles", "eyebrows"),)),
        ("docs/avatars.md", avatars, r"(\d+) eye shadow, (\d+)\s+eyeliner", (count("faceStyles", "eyeShadow"), count("faceStyles", "eyeliner"))),
        ("docs/avatars.md", avatars, r"(\d+) blush and (\d+) lip styles", (count("faceStyles", "blush"), count("faceStyles", "lips"))),
        ("docs/avatars.md", avatars, r"the editor's (\d+) named shapes", (len(manifest["facePresets"]),)),
        ("docs/avatars.md", avatars, r"\((\d+) catalog presets", (len(manifest["facePresets"]),)),
        ("docs/avatars.md", avatars, r"each of the (\d+) face features", (len(manifest["faceControls"]["male"]),)),
        ("docs/avatars.md", avatars, r"(\d+) complete looks", (len(manifest["outfits"]),)),
        ("docs/avatars.md", avatars, r"listing the other (\d+) files", (len(manifest["assets"]),)),
        ("docs/avatars.md", avatars, r"\((\d+) files, ([\d.]+) MB in\s+all\)", (len(files), size_mb)),
        ("docs/gamer-services-known-limitations.md", limits, r"shipped, at revision (\d+)", (revision,)),
        ("tools/avatar_builder/README.md", readme, r"It writes\s+revision (\d+) of catalog v3", (revision,)),
    ]
    problems = []
    for document, text, pattern, expected in claims:
        found = re.search(pattern, text)
        if not found:
            problems.append("%s no longer states /%s/ (update the claim or this check)" % (document, pattern))
            continue
        stated = tuple(float(g) if "." in g else int(g) for g in found.groups())
        if stated != tuple(expected):
            problems.append("%s states %s where catalog.json has %s: /%s/" % (document, stated, tuple(expected), pattern))
    return problems, "%d documented claims checked against catalog.json (revision %d)" % (len(claims), revision)


def stage_generator(out):
    log = out / "generator.log"
    result = subprocess.run([sys.executable, str(HERE / "generate_avatar_catalog.py"), "--check", "--jobs", "8"], cwd=ROOT,
                            capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr)
    return ([] if result.returncode == 0 else ["generate_avatar_catalog.py --check exited %d (%s)" % (result.returncode, log)],
            "the checked-in catalog is what the generator writes")


# ----- per renderer ----------------------------------------------------------------------------------

def private(command, renderer, log, timeout):
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", CNA_GRAPHICS_RENDERER=renderer)
    result = subprocess.run([str(RUNNER), "--exec", *command], cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)
    log.write_text(result.stdout + result.stderr)
    return result


def stage_tests(tests, renderer, out):
    patterns = sorted({p for _, ps, _ in COMPONENTS for p in ps})
    report = out / ("tests-%s.json" % renderer)
    report.unlink(missing_ok=True)
    log = out / ("tests-%s.log" % renderer)
    result = private([str(tests), "--gtest_filter=" + ":".join(patterns), "--gtest_output=json:" + str(report)], renderer, log, 3600)
    if not report.is_file():
        return ["%s wrote no test report (exit %d, %s)" % (tests.name, result.returncode, log)], "no report"
    problems = []
    if "graphics renderer: %s" % renderer not in result.stdout + result.stderr:
        problems.append("the tests never reported drawing with %s (%s)" % (renderer, log))
    outcome = {}
    for suite in json.loads(report.read_text())["testsuites"]:
        for case in suite["testsuite"]:
            name = "%s.%s" % (suite["name"], case["name"])
            outcome[name] = "failed" if case.get("failures") else "skipped" if case.get("result") == "SKIPPED" else "passed"
    for name, state in sorted(outcome.items()):
        if state == "failed":
            problems.append("FAILED " + name)
        elif state == "skipped":
            renderers, _ = ALLOWED_SKIPS.get(name, (set(), ""))
            if name not in ALLOWED_SKIPS or (renderers is not None and renderer not in renderers):
                problems.append("skipped, which is not an allowed skip on %s: %s" % (renderer, name))
    counts = []
    for component, component_patterns, pinned in COMPONENTS:
        mine = [n for n in outcome if matches(n, component_patterns)]
        accounted = [n for n in mine if outcome[n] == "passed" or (outcome[n] == "skipped" and n in ALLOWED_SKIPS)]
        counts.append("%s %d/%d" % (component, len(accounted), pinned))
        if len(accounted) < pinned:
            problems.append("%s: %d of its %d tests ran (%s)" % (component, len(accounted), pinned,
                                                                 "none" if not mine else "%d found" % len(mine)))
    passed = sum(1 for s in outcome.values() if s == "passed")
    skipped = sorted(n for n, s in outcome.items() if s == "skipped")
    return problems, "%d tests: %d passed, %d skipped%s; %s" % (
        len(outcome), passed, len(skipped), (" (" + ", ".join(n.split(".")[1] for n in skipped) + ")") if skipped else "",
        "; ".join(counts))


def stage_visual(program, renderer, out):
    log = out / ("visual-%s.log" % renderer)
    result = private([sys.executable, str(HERE / "avatar_visual_regression.py"), "check", "--program", str(program), "--renderer", renderer,
                      "--out", str(out / "visual")], renderer, log, 1800)
    text = result.stdout + result.stderr
    summary = next((l for l in text.splitlines() if l.startswith("avatar visual regression on")), "no summary")
    problems = [] if result.returncode == 0 else ["visual regression failed on %s (%s): %s" % (
        renderer, log, " | ".join(l.strip() for l in text.splitlines() if l.strip().startswith("- "))[:800])]
    return problems, summary.replace("avatar visual regression on %s: " % renderer, "")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build", type=Path, help="a configured and built CNA build tree")
    parser.add_argument("--renderer", action="append", default=[], help="renderer to accept on (repeatable); default: the tree's default")
    parser.add_argument("--all-renderers", action="store_true", help="every renderer compiled into the tree")
    parser.add_argument("--tests", type=Path, help="test binary (default: <build>/CnaTests)")
    parser.add_argument("--out", type=Path, help="report directory (default: <build>/avatar-acceptance)")
    parser.add_argument("--skip", action="append", default=[], choices=["generator", "visual"], help="leave a stage out on purpose")
    args = parser.parse_args()
    build = args.build.resolve()
    out = (args.out or build / "avatar-acceptance").resolve()
    out.mkdir(parents=True, exist_ok=True)
    default = cache_value(build, "CNA_GRAPHICS_RENDERER")
    compiled = [r for r in cache_value(build, "CNA_GRAPHICS_RENDERERS").split(";") if r] or [default]
    renderers = compiled if args.all_renderers else (args.renderer or [default])
    unknown = [r for r in renderers if r not in compiled]
    if unknown:
        sys.exit("%s compiled in %s, not %s" % (build.name, compiled, unknown))
    tests = (args.tests or build / "CnaTests").resolve()
    program = build / "cna_avatar_review"
    commit = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT, capture_output=True, text=True).stdout.strip()

    results = []

    def record(name, outcome):
        problems, summary = outcome
        results.append({"stage": name, "passed": not problems, "summary": summary, "problems": problems})
        print("  %-22s %s  %s" % (name, "PASS" if not problems else "FAIL", summary), flush=True)
        for problem in problems[:40]:
            print("      - " + problem)

    print("Avatar v3 acceptance: %s (default %s) at %s" % (build.name, default, commit), flush=True)
    record("catalog", stage_catalog())
    record("docs", stage_docs())
    if "generator" in args.skip:
        results.append({"stage": "generator", "passed": None, "summary": "left out (--skip generator)", "problems": []})
        print("  %-22s SKIPPED BY REQUEST" % "generator")
    else:
        record("generator", stage_generator(out))
    if not tests.is_file():
        record("tests", (["no test binary at %s (build CnaTests)" % tests], "not built"))
    for renderer in renderers:
        if tests.is_file():
            record("tests %s" % renderer, stage_tests(tests, renderer, out))
        if renderer in NO_PIXELS:
            results.append({"stage": "visual %s" % renderer, "passed": None, "summary": "not applicable: draws no pixels", "problems": []})
            print("  %-22s n/a   %s draws no pixels" % ("visual " + renderer, renderer))
        elif "visual" in args.skip:
            results.append({"stage": "visual %s" % renderer, "passed": None, "summary": "left out (--skip visual)", "problems": []})
            print("  %-22s SKIPPED BY REQUEST" % ("visual " + renderer))
        elif not program.is_file():
            record("visual %s" % renderer, (["no cna_avatar_review in %s (CNA_BUILD_EXAMPLES and a drawing default renderer build it; "
                                             "--skip visual leaves the stage out)" % build.name], "not built"))
        else:
            record("visual %s" % renderer, stage_visual(program, renderer, out))
    passed = all(r["passed"] is not False for r in results)
    (out / "acceptance.json").write_text(json.dumps({"build": str(build), "commit": commit, "renderers": renderers, "passed": passed,
                                                     "stages": results}, indent=1) + "\n")
    print("Avatar v3 acceptance %s (%s)" % ("PASSED" if passed else "FAILED", out / "acceptance.json"))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
