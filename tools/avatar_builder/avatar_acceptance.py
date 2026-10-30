#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""The Avatar v3 acceptance command: one run that says whether Avatar v3 is whole in a build tree.

    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi                  # its default renderer
    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --all-renderers  # every one compiled in
    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --renderer SDLGPU --renderer SOFTWARE
    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --all-renderers --configure  # a fresh checkout
    python3 tools/avatar_builder/avatar_acceptance.py cmake-build-multi --all-renderers --service ../cna-gamer-services-server
    python3 tools/avatar_builder/avatar_acceptance.py --static   # no build: catalog, docs, visual manifest metadata

Stages, each of which must pass:

  source     the checkout: its commit, and that no tracked file, untracked file or the catalog
             submodule differs from it (--allow-dirty runs anyway, and then validates no commit);
             the build tree was configured from this checkout
  build      cmake --build of every target the stages run, so what runs is what this commit builds
             (incremental: a tree already up to date costs a second); --configure first configures
             a tree that does not exist yet, as docs/avatars.md "Acceptance" describes
  catalog    catalog.json, the generator, the codec and CMake agree on the catalog version,
             revision and reader level; every id is unique; outfits, presets and rules name what exists
  docs       what docs/avatars.md, the limitations page, docs/link-size-architecture.md and the
             generator's README state about the catalog (revision, counts, files, size) and the
             visual regression (views, avatars, face presets) is what catalog.json and the
             regression manifest hold
  generator  generate_avatar_catalog.py --check: the checked-in catalog is exactly what it generates
  identities every test acceptance_tests.json names exists in CnaTests under that name and is not
             disabled, and every test a component's patterns match is named there: a missing,
             renamed, disabled or unlisted test fails (--write-expected rewrites the lists from the
             binary, for review in the diff)
  tests      per renderer, exactly those tests (codec, description, catalog, content and geometry,
             animation, renderer, editor, Guide editor, Gamer Services persistence, deterministic
             randomization): each must run and pass, or skip where allowedSkips allows it on that
             renderer; the binary's exit status must be 0 whatever its JSON report says
  ctest      once, with the default renderer: the avatar CTest entries outside CnaTests (the EasyGL
             avatar render, the C API avatar smokes, the link-footprint gates); each must be
             registered and pass
  measure    once: the built catalog library's size is what the documentation states; the game that
             does not use avatars links none of it and stays within its baseline; the first, next
             and a reused avatar load within the documented times, in a process of their own, and
             the caches let go of an avatar and its textures when no renderer draws it any more
  visual manifest
             once: avatar_visual_regression.py manifest --check -- regenerating the job list from its
             sources reproduces the checked-in manifest byte for byte, the manifest names the current
             catalog (version, revision, catalog.json SHA-256), and the goldens are exactly its jobs'
  visual     per renderer that draws, avatar_visual_regression.py check against the goldens

  service    with --service, the Gamer Services service checkout: its commit (clean, like this one),
             its avatar targets built in its build tree (default build-agent/), and its avatar
             validation, avatar persistence and end-to-end CTest entries run against this checkout's
             catalogs and avatar client harness; each must be registered and pass, none may skip

  source end HEAD and the working tree are what they were when the build ran; a commit that only
             touches documentation the run did not read is reported, anything else fails the run

Every test binary and the visual check run through tools/platform/run_gpu_tests_private.sh (a private
display, never the desktop). `--skip STAGE` leaves a stage out on purpose; the summary says so.
Writes acceptance.json (with the commit, the build's toolchain and the SHA-256 of every binary run)
and every stage's log to --out. Exit status 0 only when every stage passed; the result names the
commit it validated only when the checkout was clean.
"""
import argparse
import fnmatch
import hashlib
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
# What --configure gives a tree that does not exist yet (docs/avatars.md, "Acceptance").
CONFIGURE = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", "-DCNA_BUILD_TESTS=ON", "-DCNA_BUILD_EXAMPLES=ON", "-DCNA_BUILD_C_API=ON",
             "-DCNA_GRAPHICS_RENDERER=OPENGLES3",
             "-DCNA_GRAPHICS_RENDERERS=OPENGLES3;OPENGL33;WEBGPU;SDLGPU;FNA3D;SOFTWARE;HEADLESS;STUB",
             "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache", "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
             "-DFETCHCONTENT_UPDATES_DISCONNECTED=ON"]
# The binaries the stages run themselves (hashed in the report); the build stage builds them and the
# targets of the CTest entries (acceptance_tests.json), so none is older than the checkout.
TARGETS = ["CnaTests", "cna_avatar_review"]
# Documentation stage_docs reads: a commit touching one of these during the run invalidates it.
DOCUMENTS = ["docs/avatars.md", "docs/gamer-services-known-limitations.md", "docs/link-size-architecture.md",
             "tools/avatar_builder/README.md"]
WORDS = "zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen".split()

# The tests the acceptance requires, by name, and the skips it allows (with their reasons).
EXPECTED = HERE / "acceptance_tests.json"


def expected():
    return json.loads(EXPECTED.read_text())


def ctest_entries(build):
    """The CTest entries the acceptance runs in this tree (some register only for a default renderer)."""
    default = cache_value(build, "CNA_GRAPHICS_RENDERER")
    return [t for t in expected()["ctest"]["tests"] if default in t.get("defaultRenderers", [default])]


def targets(build, service=False):
    return TARGETS + [t["target"] for t in ctest_entries(build)] + ([expected()["service"]["harness"]] if service else [])


def matches(name, patterns):
    return any(fnmatch.fnmatchcase(name, p) for p in patterns)


def cache_value(build, key):
    found = re.search(r"^%s:[A-Z]+=(.*)$" % re.escape(key), (build / "CMakeCache.txt").read_text(), re.M)
    return found.group(1).strip() if found else ""


def git(*args, cwd=ROOT):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True).stdout.rstrip("\n")


def changes(cwd):
    """What differs from HEAD in a checkout; a git status that fails is itself a difference, never clean."""
    result = subprocess.run(["git", "status", "--porcelain=v1", "--untracked-files=normal"], cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        return ["git status failed (exit %d): %s" % (result.returncode, (result.stderr.strip().splitlines() or ["?"])[0])]
    return [l for l in result.stdout.splitlines() if l]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 22), b""):
            digest.update(block)
    return digest.hexdigest()


# ----- source and build provenance ---------------------------------------------------------------------

def source_state():
    """The commit, what differs from it (tracked or untracked, the catalog submodule included)."""
    return {"commit": git("rev-parse", "HEAD"), "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
            "changes": changes(ROOT), "catalogSubmodule": git("submodule", "status", "--", str(CATALOG.parent.relative_to(ROOT)))}


def stage_source(state, build, allow_dirty):
    problems = []
    if not state["commit"]:
        problems.append("%s is not a Git checkout" % ROOT)
    if state["changes"] and not allow_dirty:
        problems.append("the checkout differs from %s (%d paths: %s); commit, or --allow-dirty to run without "
                        "validating a commit" % (state["commit"][:12], len(state["changes"]), "; ".join(state["changes"][:6])))
    if not state["catalogSubmodule"].startswith(" "):
        problems.append("the catalog submodule is not at the commit CNA records: %s" % state["catalogSubmodule"])
    if (build / "CMakeCache.txt").is_file():
        home = Path(cache_value(build, "CMAKE_HOME_DIRECTORY")).resolve()
        if home != ROOT:
            problems.append("%s was configured from %s, not from this checkout %s" % (build.name, home, ROOT))
    summary = "%s (%s)%s, catalog submodule %s" % (
        state["commit"][:12], state["branch"], ", %d uncommitted paths" % len(state["changes"]) if state["changes"] else ", clean",
        state["catalogSubmodule"][1:13])
    return problems, summary


def builders(build):
    """Other processes building in this tree (ninja, make or cmake --build)."""
    busy = []
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            args = [a.decode(errors="replace") for a in (proc / "cmdline").read_bytes().split(b"\0") if a]
            cwd = os.readlink(proc / "cwd")
        except OSError:
            continue
        if not args or int(proc.name) == os.getpid():
            continue
        tool = os.path.basename(args[0])
        if tool in ("ninja", "make", "gmake") or (tool == "cmake" and "--build" in args):
            if any(str(build) in a for a in args) or cwd == str(build) or cwd.startswith(str(build) + os.sep):
                busy.append(int(proc.name))
    return busy


def compiler(build):
    for found in sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake")):
        text = found.read_text()
        ident = re.search(r'CMAKE_CXX_COMPILER_ID "([^"]*)"', text)
        version = re.search(r'CMAKE_CXX_COMPILER_VERSION "([^"]*)"', text)
        return "%s %s" % (ident.group(1) if ident else "?", version.group(1) if version else "?")
    return "unknown"


def stage_build(build, out, configure, provenance, service=False):
    log = out / "build.log"
    env = dict(os.environ)
    env.setdefault("CCACHE_DIR", "/rv/cnaccache" if Path("/rv/cnaccache").is_dir() else env.get("CCACHE_DIR", ""))
    env.setdefault("CCACHE_BASEDIR", "/rv" if str(ROOT).startswith("/rv/") else str(ROOT.parent))
    env = {k: v for k, v in env.items() if v != ""}
    commands = []
    if not (build / "CMakeCache.txt").is_file():
        if configure is None:
            return ["%s is not a configured build tree; --configure configures it (%s)" % (build, " ".join(CONFIGURE))], "not configured"
        commands.append(["cmake", "-S", str(ROOT), "-B", str(build), *CONFIGURE, *configure])
    busy = builders(build)
    if busy:
        return ["another build is running in %s (pids %s); run again when it has finished" % (build, busy)], "tree busy"
    text = []
    for command in commands + [None]:
        if command is None:
            command = ["cmake", "--build", str(build), "--target", *targets(build, service), "--parallel", "8"]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        text.append("$ %s\n%s%s" % (" ".join(command), result.stdout, result.stderr))
        log.write_text("\n".join(text))
        if result.returncode != 0:
            tail = " | ".join(l for l in (result.stdout + result.stderr).splitlines() if "error" in l.lower())[:600]
            if "unknown target" in tail:
                tail += " -- the acceptance needs a tree configured with %s" % " ".join(o for o in CONFIGURE if o.startswith("-DCNA_BUILD"))
            return ["%s exited %d (%s): %s" % (command[1], result.returncode, log, tail)], "build failed"
    steps = sum(1 for l in text[-1].splitlines() if re.match(r"\[\d+/\d+\] (Building (C|CXX) object|Linking)", l))
    binaries = {}
    for target in TARGETS:
        path = build / target
        if not path.is_file():
            return ["%s built no %s" % (build.name, path)], "missing binary"
        binaries[target] = {"path": str(path), "bytes": path.stat().st_size, "sha256": sha256(path)}
    provenance.update({"tree": str(build), "generator": cache_value(build, "CMAKE_GENERATOR"),
                       "buildType": cache_value(build, "CMAKE_BUILD_TYPE"), "compiler": compiler(build),
                       "launcher": cache_value(build, "CMAKE_CXX_COMPILER_LAUNCHER"), "configured": bool(commands),
                       "stepsRun": steps, "targets": targets(build, service), "binaries": binaries, "log": str(log)})
    return [], "%s %s, %s: %s (%s)" % (provenance["compiler"], provenance["buildType"], provenance["generator"],
                                       "up to date" if not steps else "%d compile/link steps run" % steps, ", ".join(targets(build, service)))


def stage_source_end(start):
    now = source_state()
    if now == start:
        return [], "HEAD and the working tree are unchanged since the build"
    moved = []
    if now["commit"] != start["commit"]:
        moved = git("diff", "--name-only", start["commit"], now["commit"]).splitlines()
    moved += sorted(set(now["changes"]) ^ set(start["changes"]))
    harmless = all(re.match(r"(.. )?(docs/|plans/|[^/]+\.md$)", p) and p.split(" ")[-1] not in DOCUMENTS for p in moved)
    if moved and harmless:
        return [], "HEAD moved to %s during the run, touching documentation the run did not read only (%d paths); results are %s's" % (
            now["commit"][:12], len(moved), start["commit"][:12])
    return ["the checkout changed during the run (HEAD %s -> %s; %s); the binaries are %s's, run again" % (
        start["commit"][:12], now["commit"][:12], "; ".join(moved[:6]), start["commit"][:12])], "changed during the run"


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
    import avatar_visual_regression as visual
    manifest = json.loads((CATALOG / "catalog.json").read_text())
    regression = json.loads(visual.MANIFEST.read_text())
    count = lambda key, slot: sum(1 for e in manifest[key] if e["slot"] == slot)
    files = list(CATALOG.iterdir())
    size_mb = round(sum(p.stat().st_size for p in files) / 1e6, 1)
    text = {d: (ROOT / d).read_text() for d in DOCUMENTS}
    revision = manifest["revision"]
    faces = {t for j in regression["jobs"] for t in j["covers"] if t.startswith("face:")}
    # (document, pattern, expected values of its groups, every occurrence rather than the first)
    claims = [
        ("docs/avatars.md", r'\*\*revision (\d+)\*\* \(manifest `"revision": (\d+)`\)', (revision, revision)),
        ("docs/avatars.md", r"(\d+) wardrobe items and accessories", (len(manifest["items"]),)),
        ("docs/avatars.md", r"(\d+) hairstyles \(", (count("items", "hair"),)),
        ("docs/avatars.md", r"(\d+) tops, (\d+) bottoms, (\d+) shoes, (\d+) glasses,\s+(\d+) headwear, (\d+) earrings, "
                            r"(\d+) wristwear, (\d+) gloves and (\d+) rings",
         tuple(count("items", s) for s in ("top", "bottom", "shoes", "glasses", "hat", "earrings", "wristwear", "gloves", "rings"))),
        ("docs/avatars.md", r"(\d+) facial-hair styles \(stubble to full beards\) and (\d+) ear shapes",
         (count("featureItems", "facialHair"), count("featureItems", "ears"))),
        ("docs/avatars.md", r"(\d+) eyebrow styles", (count("faceStyles", "eyebrows"),)),
        ("docs/avatars.md", r"(\d+) eye shadow, (\d+)\s+eyeliner", (count("faceStyles", "eyeShadow"), count("faceStyles", "eyeliner"))),
        ("docs/avatars.md", r"(\d+) blush and (\d+) lip styles", (count("faceStyles", "blush"), count("faceStyles", "lips"))),
        ("docs/avatars.md", r"the editor's (\d+) named shapes", (len(manifest["facePresets"]),)),
        ("docs/avatars.md", r"\((\d+) catalog presets", (len(manifest["facePresets"]),)),
        ("docs/avatars.md", r"each of the (\d+) face features", (len(manifest["faceControls"]["male"]),)),
        ("docs/avatars.md", r"(\d+) complete looks", (len(manifest["outfits"]),)),
        ("docs/avatars.md", r"listing the other (\d+) files", (len(manifest["assets"]),)),
        ("docs/avatars.md", r"\((\d+) files, ([\d.]+) MB in\s+all\)", (len(files), size_mb)),
        # The visual regression paragraph, against the checked-in manifest and the script that makes it.
        ("docs/avatars.md", r"keeps (\d+) views of (\w+) described\s+avatars and of (\w+) `CreateRandom` avatars from a fixed seed "
                            r"\(`cna_avatar_review --random (\d+) --seed\s+(\d+)`\)",
         (len(regression["jobs"]), WORDS[len(visual.AVATARS)], WORDS[visual.RANDOM_COUNT], visual.RANDOM_COUNT, visual.RANDOM_SEED)),
        ("docs/avatars.md", r"skin, (\w+) face presets", (WORDS[len(faces)],)),
        ("docs/gamer-services-known-limitations.md", r"shipped, at revision (\d+)", (revision,)),
        ("docs/gamer-services-known-limitations.md", r"\(revision (\d+): adult", (revision,)),
        ("docs/link-size-architecture.md", r"MB deflated at revision (\d+)", (revision,), True),
        ("tools/avatar_builder/README.md", r"It writes\s+revision (\d+) of catalog v3", (revision,)),
    ]
    problems = []
    for document, pattern, expected, *every in claims:
        found = list(re.finditer(pattern, text[document]))
        if not found:
            problems.append("%s no longer states /%s/ (update the claim or this check)" % (document, pattern))
        for match in found if every else found[:1]:
            stated = tuple(g if not re.fullmatch(r"[\d.]+", g) else float(g) if "." in g else int(g) for g in match.groups())
            if stated != tuple(expected):
                problems.append("%s states %s where the catalog and manifest have %s: /%s/" % (document, stated, tuple(expected), pattern))
    return problems, "%d documented claims checked against catalog.json (revision %d) and the visual manifest" % (len(claims), revision)


def stage_visual_metadata():
    import avatar_visual_regression as visual
    data = json.loads(visual.MANIFEST.read_text())
    return visual.stale(data), "%d jobs, %d goldens, made for catalog v%d revision %d (catalog.json %s)" % (
        len(data["jobs"]), len(list(visual.GOLDEN.glob("*.png"))), data["catalogVersion"], data["catalogRevision"],
        data.get("catalogSha256", "?")[:12])


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


def listed_tests(tests):
    """Every test CnaTests defines, disabled ones included (listing runs no test and opens no window)."""
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
    result = subprocess.run([str(tests), "--gtest_list_tests"], env=env, capture_output=True, text=True, timeout=600)
    names, suite = [], ""
    for line in result.stdout.splitlines():
        entry = line.split("#")[0].strip()
        if not entry:
            continue
        if line.startswith(" "):
            names.append(suite + entry)
        else:
            suite = entry
    return result.returncode, names


def stage_identities(tests, out, required):
    data = expected()
    code, present = listed_tests(tests)
    (out / "test-list.txt").write_text("\n".join(present) + "\n")
    if code != 0 or not present:
        return ["%s --gtest_list_tests exited %d and listed %d tests" % (tests.name, code, len(present))], "no test list"
    defined, problems, counts = set(present), [], []
    for component in data["components"]:
        want = set(component["tests"])
        required.update(want)
        disabled = {}
        for name in want - defined:
            suite, case = name.split(".", 1)
            variants = {"%s.DISABLED_%s" % (suite, case), "DISABLED_%s.%s" % (suite, case)} & defined
            if variants:
                disabled[name] = variants
                problems.append("%s: %s is disabled (%s)" % (component["name"], name, ", ".join(sorted(variants))))
            else:
                problems.append("%s: %s is missing from %s (renamed or removed)" % (component["name"], name, tests.name))
        hidden = set().union(*disabled.values()) if disabled else set()
        for name in sorted(n for n in present if matches(n, component["patterns"]) and n not in want and n not in hidden):
            problems.append("%s: %s is not in %s (a new, renamed or disabled test; --write-expected rewrites the lists)" % (
                component["name"], name, EXPECTED.name))
        counts.append("%s %d" % (component["name"], len(want)))
    for name in data["allowedSkips"]:
        if name not in required:
            problems.append("allowedSkips names %s, which no component requires" % name)
    return problems, "%d required tests by name, all defined and enabled in %s (%d tests): %s" % (
        len(required), tests.name, len(present), "; ".join(counts))


def stage_tests(tests, renderer, out, required):
    data = expected()
    report = out / ("tests-%s.json" % renderer)
    report.unlink(missing_ok=True)
    log = out / ("tests-%s.log" % renderer)
    result = private([str(tests), "--gtest_filter=" + ":".join(sorted(required)), "--gtest_output=json:" + str(report)], renderer, log, 3600)
    problems = []
    if result.returncode != 0:
        problems.append("%s exited %d (%s)" % (tests.name, result.returncode, log))
    if not report.is_file():
        return problems + ["%s wrote no test report (%s)" % (tests.name, log)], "no report"
    if "graphics renderer: %s" % renderer not in result.stdout + result.stderr:
        problems.append("the tests never reported drawing with %s (%s)" % (renderer, log))
    document = json.loads(report.read_text())
    outcome = {}
    for suite in document["testsuites"]:
        for case in suite["testsuite"]:
            name = "%s.%s" % (suite["name"], case["name"])
            outcome[name] = ("failed" if case.get("failures") else "skipped" if case.get("result") == "SKIPPED" else
                             "passed" if case.get("status") == "RUN" and case.get("result") == "COMPLETED" else
                             "not run (%s, %s)" % (case.get("status"), case.get("result")))
    if document.get("failures") or document.get("errors"):
        problems.append("the report counts %s failures and %s errors" % (document.get("failures"), document.get("errors")))
    for name in sorted(required - set(outcome)):
        problems.append("did not run: " + name)
    for name in sorted(set(outcome) - required):
        problems.append("ran, though not required: " + name)
    for name, state in sorted(outcome.items()):
        if state == "failed":
            problems.append("FAILED " + name)
        elif state == "skipped":
            allowed = data["allowedSkips"].get(name)
            if not allowed or (allowed["renderers"] is not None and renderer not in allowed["renderers"]):
                problems.append("skipped, which is not an allowed skip on %s: %s" % (renderer, name))
        elif state != "passed":
            problems.append("%s: %s" % (state, name))
    passed = sum(1 for s in outcome.values() if s == "passed")
    skipped = sorted(n for n, s in outcome.items() if s == "skipped")
    return problems, "%d/%d required tests ran: %d passed, %d skipped%s; exit status %d" % (
        len(set(outcome) & required), len(required), passed, len(skipped),
        (" (" + ", ".join(n.split(".")[1] for n in skipped) + ")") if skipped else "", result.returncode)


def stage_ctest(build, out):
    names = [t["name"] for t in ctest_entries(build)]
    junit, log = out / "ctest-avatar.xml", out / "ctest-avatar.log"
    junit.unlink(missing_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy")
    env.pop("CNA_GRAPHICS_RENDERER", None)
    result = subprocess.run([str(RUNNER), str(build), "-R", "^(%s)$" % "|".join(names), "--no-tests=error", "--output-on-failure",
                             "--output-junit", str(junit)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=1800)
    log.write_text(result.stdout + result.stderr)
    problems = [] if result.returncode == 0 else ["ctest exited %d (%s)" % (result.returncode, log)]
    if not junit.is_file():
        return problems + ["ctest wrote no JUnit report (%s)" % log], "no report"
    import xml.etree.ElementTree as ElementTree
    status = {case.get("name"): case.get("status") for case in ElementTree.parse(junit).getroot().iter("testcase")}
    for name in names:
        if name not in status:
            problems.append("%s is not registered in %s" % (name, build.name))
        elif status[name] != "run":
            problems.append("%s: %s" % (name, status[name]))
    return problems, "%d/%d passed (%s)" % (sum(1 for n in names if status.get(n) == "run"), len(names), ", ".join(names))


def footprint(junit, name):
    """text, data and bss of a link-footprint probe, from its CTest output."""
    import xml.etree.ElementTree as ElementTree
    for case in ElementTree.parse(junit).getroot().iter("testcase"):
        if case.get("name") == name:
            found = re.search(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+[0-9a-f]+\s", case.findtext("system-out") or "", re.M)
            if found:
                return tuple(int(g) for g in found.groups())
    return None


def stage_measure(build, tests, renderer, out, measurements):
    budget = expected()["measurements"]
    problems = []
    avatars, links = (ROOT / "docs/avatars.md").read_text(), (ROOT / "docs/link-size-architecture.md").read_text()
    files = list(CATALOG.iterdir())
    library = next(iter(sorted((build / "modules" / "gamer-services").glob("libcna_gamerservices_avatar_catalog.*"))), None)
    measured = {"catalogFiles": len(files), "catalogBytes": sum(p.stat().st_size for p in files),
                "catalogJsonBytes": (CATALOG / "catalog.json").stat().st_size, "libraryBytes": library.stat().st_size if library else None}
    if library is None:
        problems.append("%s has no built avatar catalog library" % build.name)
    else:
        stated = [("docs/avatars.md", m) for m in re.findall(r"to a ([\d.]+) MB library", avatars)] + \
                 [("docs/link-size-architecture.md", m) for m in re.findall(r"([\d.]+) MB deflated at revision", links)]
        if len(stated) < 3:
            problems.append("the documentation no longer states the library's size where this stage looks (%d of 3 statements)" % len(stated))
        for document, value in stated:
            if float(value) != round(measured["libraryBytes"] / 1e6, 1):
                problems.append("%s states a %s MB catalog library; %s is %.1f MB (%d bytes)" % (
                    document, value, library.name, measured["libraryBytes"] / 1e6, measured["libraryBytes"]))
    junit = out / "ctest-avatar.xml"
    plain = footprint(junit, "LinkFootprint_2DGame") if junit.is_file() else None
    gamer = footprint(junit, "LinkFootprint_GamerServicesGame") if junit.is_file() else None
    toolchain = "%s %s" % (compiler(build), cache_value(build, "CMAKE_BUILD_TYPE"))
    growth = None
    if not plain or not gamer:
        problems.append("no link-footprint sizes in %s (the ctest stage runs the probes)" % junit)
    else:
        measured.update({"game2DTextBytes": plain[0], "gamerServicesGameTextBytes": gamer[0]})
        if library and gamer[0] - plain[0] < measured["libraryBytes"] * 0.95:
            problems.append("the Gamer Services game is only %d bytes larger than the 2D game; the %d-byte catalog is not in it" % (
                gamer[0] - plain[0], measured["libraryBytes"]))
        if toolchain == budget["toolchain"]:
            growth = plain[0] / budget["game2DTextBytes"] - 1.0
            if growth > budget["growth"]:
                problems.append("the 2D game's text is %d bytes, %.1f %% over its %d-byte baseline (%s)" % (
                    plain[0], growth * 100, budget["game2DTextBytes"], toolchain))
    report, log = out / "measure-load.json", out / "measure-load.log"
    report.unlink(missing_ok=True)
    result = private([str(tests), "--gtest_filter=" + budget["loadTest"], "--gtest_output=json:" + str(report)], renderer, log, 600)
    case = None
    if report.is_file():
        case = next((c for s in json.loads(report.read_text())["testsuites"] for c in s["testsuite"]), None)
    if result.returncode != 0 or not case or case.get("result") != "COMPLETED" or case.get("failures"):
        problems.append("%s did not pass in a process of its own (exit %d, %s)" % (budget["loadTest"], result.returncode, log))
    else:
        measured.update({key: int(case[key]) for key in ("firstAvatarMilliseconds", "nextAvatarMilliseconds", "reusedAvatarMicroseconds",
                                                          "liveTexturesWhileDrawn", "parsedModelsKept")})
        if case.get("coldProcess") != "yes":
            problems.append("%s did not start from empty caches" % budget["loadTest"])
        documented = re.search(r"is ready in about (\d+)-(\d+) ms\s+and the next in about (\d+)-(\d+) ms", avatars)
        if not documented:
            problems.append("docs/avatars.md no longer states the first and next avatar's load times")
        else:
            measured["documentedMilliseconds"] = [int(g) for g in documented.groups()]
            for key, ceiling in (("firstAvatarMilliseconds", int(documented.group(2))), ("nextAvatarMilliseconds", int(documented.group(4)))):
                if measured[key] > ceiling * budget["loadTimeFactor"]:
                    problems.append("%s %d ms, over %.1f times the documented %d ms" % (key, measured[key], budget["loadTimeFactor"], ceiling))
        if measured["reusedAvatarMicroseconds"] > budget["reusedMilliseconds"] * 1000:
            problems.append("a reused avatar took %.1f ms" % (measured["reusedAvatarMicroseconds"] / 1000.0))
    measurements.update(measured)
    (out / "measurements.json").write_text(json.dumps(measured, indent=1) + "\n")
    summary = "catalog %d files, %.1f MB; library %s; 2D game text %s%s; Gamer Services game text %s" % (
        measured["catalogFiles"], measured["catalogBytes"] / 1e6,
        "%.1f MB" % (measured["libraryBytes"] / 1e6) if library else "?",
        "{:,}".format(plain[0]) if plain else "?",
        " (%+.2f %% on %s)" % (growth * 100, toolchain) if growth is not None else " (no baseline for %s)" % toolchain,
        "{:,}".format(gamer[0]) if gamer else "?")
    if "firstAvatarMilliseconds" in measured:
        summary += "; first avatar %d ms, next %d ms, reused %.2f ms; %d textures while drawn, released after; %d parsed models kept" % (
            measured["firstAvatarMilliseconds"], measured["nextAvatarMilliseconds"], measured["reusedAvatarMicroseconds"] / 1000.0,
            measured["liveTexturesWhileDrawn"], measured["parsedModelsKept"])
    return problems, summary


def stage_service(checkout, service_build, build, out, allow_dirty, provenance):
    spec = expected()["service"]
    run = lambda *args, **kw: subprocess.run(list(args), capture_output=True, text=True, **kw)
    state = {"checkout": str(checkout), "commit": run("git", "-C", str(checkout), "rev-parse", "HEAD").stdout.strip(),
             "branch": run("git", "-C", str(checkout), "rev-parse", "--abbrev-ref", "HEAD").stdout.strip(),
             "changes": changes(checkout)}
    provenance.update(state)
    if not state["commit"]:
        return ["%s is not a Git checkout of the service" % checkout], "no checkout"
    if state["changes"] and not allow_dirty:
        return ["the service checkout differs from %s (%s)" % (state["commit"][:12], "; ".join(state["changes"][:6]))], "dirty"
    if not (service_build / "CMakeCache.txt").is_file():
        return ["%s is not a configured service build tree (see the service's README, Tests)" % service_build], "not configured"
    if Path(cache_value(service_build, "CMAKE_HOME_DIRECTORY")).resolve() != checkout:
        return ["%s was configured from another checkout" % service_build], "foreign tree"
    if builders(service_build):
        return ["another build is running in %s" % service_build], "tree busy"
    log = out / "service.log"
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", CNA_AVATAR_CATALOGS=str(CATALOG.parent),
               CNA_SERVICE_AVATAR_CLIENT_HARNESS=str(build / spec["harness"]))
    env.setdefault("CCACHE_DIR", "/rv/cnaccache" if Path("/rv/cnaccache").is_dir() else "")
    env.setdefault("CCACHE_BASEDIR", "/rv" if str(checkout).startswith("/rv/") else str(checkout.parent))
    env = {k: v for k, v in env.items() if v != ""}
    built = run("cmake", "--build", str(service_build), "--target", *spec["targets"], "--parallel", "8", env=env)
    text = "$ cmake --build\n" + built.stdout + built.stderr
    log.write_text(text)
    if built.returncode != 0:
        return ["building the service's avatar targets failed (%s)" % log], "build failed"
    steps = sum(1 for l in built.stdout.splitlines() if re.match(r"\[\d+/\d+\] (Building (C|CXX) object|Linking)", l))
    junit = out / "service.xml"
    junit.unlink(missing_ok=True)
    tested = run("ctest", "--test-dir", str(service_build), "-R", "^(%s)$" % "|".join(spec["tests"]), "--no-tests=error",
                 "--output-on-failure", "-V", "--output-junit", str(junit), env=env, timeout=1800)
    log.write_text(text + "\n$ ctest\n" + tested.stdout + tested.stderr)
    problems = [] if tested.returncode == 0 else ["the service's ctest exited %d (%s)" % (tested.returncode, log)]
    status = {}
    if junit.is_file():
        import xml.etree.ElementTree as ElementTree
        status = {c.get("name"): c.get("status") for c in ElementTree.parse(junit).getroot().iter("testcase")}
    else:
        problems.append("the service's ctest wrote no JUnit report (%s)" % log)
    for name in spec["tests"]:
        if name not in status:
            problems.append("%s is not registered in %s" % (name, service_build))
        elif status[name] != "run":
            problems.append("%s: %s" % (name, status[name]))
    counts = dict(re.findall(r"^\d+: (avatar validation|avatars?) (\d+) assertions passed", tested.stdout, re.M))
    provenance.update({"buildTree": str(service_build), "buildType": cache_value(service_build, "CMAKE_BUILD_TYPE"),
                       "compiler": compiler(service_build), "stepsRun": steps, "log": str(log), "assertions": counts,
                       "binaries": {t: sha256(service_build / t) for t in spec["targets"] if (service_build / t).is_file()}})
    return problems, "%s at %s (%s): %s; %s" % (
        checkout.name, state["commit"][:12], "clean" if not state["changes"] else "%d uncommitted paths" % len(state["changes"]),
        "up to date" if not steps else "%d compile/link steps run" % steps,
        ", ".join("%s %s" % (n, "passed" if status.get(n) == "run" else status.get(n, "missing")) for n in spec["tests"]) +
        "".join(" (%s %s assertions)" % (k, v) for k, v in sorted(counts.items())))


def stage_visual_manifest(program, renderer, out):
    log = out / "visual-manifest.log"
    result = private([sys.executable, str(HERE / "avatar_visual_regression.py"), "manifest", "--check", "--program", str(program)],
                     renderer, log, 1800)
    text = result.stdout + result.stderr
    manifest = json.loads((HERE / "regression" / "manifest.json").read_text())
    problems = [] if result.returncode == 0 else ["manifest --check exited %d (%s): %s" % (
        result.returncode, log, " | ".join(l.strip() for l in text.splitlines() if l.strip().startswith("- "))[:800])]
    return problems, "%d jobs for catalog v%d revision %d (catalog.json %s): %s" % (
        len(manifest["jobs"]), manifest["catalogVersion"], manifest["catalogRevision"], manifest.get("catalogSha256", "?")[:12],
        "reproduced byte for byte" if not problems else "not reproduced")


def stage_visual(program, renderer, out):
    log = out / ("visual-%s.log" % renderer)
    result = private([sys.executable, str(HERE / "avatar_visual_regression.py"), "check", "--no-reproduce", "--program", str(program),
                      "--renderer", renderer, "--out", str(out / "visual")], renderer, log, 1800)
    text = result.stdout + result.stderr
    summary = next((l for l in text.splitlines() if l.startswith("avatar visual regression on")), "no summary")
    problems = [] if result.returncode == 0 else ["visual regression failed on %s (%s): %s" % (
        renderer, log, " | ".join(l.strip() for l in text.splitlines() if l.strip().startswith("- "))[:800])]
    return problems, summary.replace("avatar visual regression on %s: " % renderer, "")


def write_expected(tests):
    data = expected()
    code, present = listed_tests(tests)
    if code != 0 or not present:
        sys.exit("%s --gtest_list_tests exited %d" % (tests, code))
    for component in data["components"]:
        old = set(component["tests"])
        component["tests"] = sorted(n for n in present if matches(n, component["patterns"]) and "DISABLED_" not in n)
        new = set(component["tests"])
        for name in sorted(new - old):
            print("  + %s: %s" % (component["name"], name))
        for name in sorted(old - new):
            print("  - %s: %s" % (component["name"], name))
    EXPECTED.write_text(json.dumps(data, indent=1) + "\n")
    print("%s rewritten; review the diff before committing it" % EXPECTED)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build", type=Path, nargs="?", help="a CNA build tree configured from this checkout (or, with --configure, to configure)")
    parser.add_argument("--static", action="store_true",
                        help="only what needs no build: catalog, docs and the visual manifest's metadata (GamerServices_AvatarAcceptanceStatic)")
    parser.add_argument("--renderer", action="append", default=[], help="renderer to accept on (repeatable); default: the tree's default")
    parser.add_argument("--all-renderers", action="store_true", help="every renderer compiled into the tree")
    parser.add_argument("--configure", action="store_true", help="configure the build tree first when it does not exist")
    parser.add_argument("--cmake-arg", action="append", default=[], help="extra configure argument for --configure (repeatable)")
    parser.add_argument("--allow-dirty", action="store_true", help="run on a checkout with uncommitted changes (validates no commit)")
    parser.add_argument("--service", type=Path, help="a cna-gamer-services-server checkout: build and run its avatar tests against this one")
    parser.add_argument("--service-build", type=Path, help="the service's configured build tree (default: <service>/build-agent)")
    parser.add_argument("--out", type=Path, help="report directory (default: <build>/avatar-acceptance)")
    parser.add_argument("--write-expected", action="store_true",
                        help="build, then rewrite acceptance_tests.json's lists from the tests its patterns match, and stop")
    parser.add_argument("--skip", action="append", default=[], choices=["generator", "visual"], help="leave a stage out on purpose")
    args = parser.parse_args()
    if not (CATALOG / "catalog.json").is_file():
        sys.exit("no %s: the avatar catalogs are the cna-avatar-assets Git submodule; run "
                 "git submodule update --init modules/gamer-services/assets/avatars" % (CATALOG / "catalog.json"))
    if args.static:
        failed = False
        for name, stage in (("catalog", stage_catalog), ("docs", stage_docs), ("visual metadata", stage_visual_metadata)):
            problems, summary = stage()
            failed |= bool(problems)
            print("  %-22s %s  %s" % (name, "FAIL" if problems else "PASS", summary))
            for problem in problems:
                print("      - " + problem)
        print("Avatar v3 static acceptance %s" % ("FAILED" if failed else "PASSED"))
        return 1 if failed else 0
    if args.build is None:
        parser.error("a build tree is required (or --static)")
    build = args.build.resolve()
    out = (args.out or build / "avatar-acceptance").resolve()
    tests, program = build / "CnaTests", build / "cna_avatar_review"
    start = source_state()
    provenance = {"source": start}
    results = []

    def record(name, outcome):
        problems, summary = outcome
        results.append({"stage": name, "passed": not problems, "summary": summary, "problems": problems})
        print("  %-22s %s  %s" % (name, "PASS" if not problems else "FAIL", summary), flush=True)
        for problem in problems[:40]:
            print("      - " + problem)

    def finish():
        passed = all(r["passed"] is not False for r in results)
        validated = start["commit"] if passed and not start["changes"] else None
        out.mkdir(parents=True, exist_ok=True)
        (out / "acceptance.json").write_text(json.dumps({"build": str(build), "commit": start["commit"], "validatedCommit": validated,
                                                         "passed": passed, "provenance": provenance, "stages": results}, indent=1) + "\n")
        verdict = ("PASSED, validating %s" % start["commit"][:12] if validated else
                   "PASSED on uncommitted changes (validates no commit)" if passed else "FAILED")
        print("Avatar v3 acceptance %s (%s)" % (verdict, out / "acceptance.json"))
        return 0 if passed else 1

    print("Avatar v3 acceptance: %s at %s" % (build.name, start["commit"][:12]), flush=True)
    record("source", stage_source(start, build, args.allow_dirty))
    if results[-1]["passed"]:
        out.mkdir(parents=True, exist_ok=True)
        record("build", stage_build(build, out, args.cmake_arg if args.configure else None, provenance.setdefault("build", {}),
                                    service=args.service is not None))
    if not all(r["passed"] for r in results):
        return finish()
    if args.write_expected:
        return write_expected(tests)
    default = cache_value(build, "CNA_GRAPHICS_RENDERER")
    compiled = [r for r in cache_value(build, "CNA_GRAPHICS_RENDERERS").split(";") if r] or [default]
    renderers = compiled if args.all_renderers else (args.renderer or [default])
    unknown = [r for r in renderers if r not in compiled]
    if unknown:
        record("renderers", (["%s compiled in %s, not %s" % (build.name, compiled, unknown)], "unknown renderer"))
        return finish()
    provenance["renderers"] = {"default": default, "compiled": compiled, "accepted": renderers}
    record("catalog", stage_catalog())
    record("docs", stage_docs())
    if "generator" in args.skip:
        results.append({"stage": "generator", "passed": None, "summary": "left out (--skip generator)", "problems": []})
        print("  %-22s SKIPPED BY REQUEST" % "generator")
    else:
        record("generator", stage_generator(out))
    if "visual" in args.skip:
        results.append({"stage": "visual manifest", "passed": None, "summary": "left out (--skip visual)", "problems": []})
        print("  %-22s SKIPPED BY REQUEST" % "visual manifest")
    else:
        record("visual manifest", stage_visual_manifest(program, default, out))
    required = set()
    record("identities", stage_identities(tests, out, required))
    for renderer in renderers:
        if required:
            record("tests %s" % renderer, stage_tests(tests, renderer, out, required))
        if renderer in NO_PIXELS:
            results.append({"stage": "visual %s" % renderer, "passed": None, "summary": "not applicable: draws no pixels", "problems": []})
            print("  %-22s n/a   %s draws no pixels" % ("visual " + renderer, renderer))
        elif "visual" in args.skip:
            results.append({"stage": "visual %s" % renderer, "passed": None, "summary": "left out (--skip visual)", "problems": []})
            print("  %-22s SKIPPED BY REQUEST" % ("visual " + renderer))
        else:
            record("visual %s" % renderer, stage_visual(program, renderer, out))
    record("ctest", stage_ctest(build, out))
    record("measure", stage_measure(build, tests, default, out, provenance.setdefault("measurements", {})))
    if args.service:
        checkout = args.service.resolve()
        record("service", stage_service(checkout, (args.service_build or checkout / "build-agent").resolve(), build, out,
                                        args.allow_dirty, provenance.setdefault("service", {})))
    else:
        results.append({"stage": "service", "passed": None, "summary": "left out (no --service)", "problems": []})
        print("  %-22s SKIPPED BY REQUEST (no --service)" % "service")
    record("source end", stage_source_end(start))
    return finish()


if __name__ == "__main__":
    sys.exit(main())
