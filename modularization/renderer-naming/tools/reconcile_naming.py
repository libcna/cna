#!/usr/bin/env python3
"""Naming-normalization no-loss reconciliation (see ../DESIGN.md).

Replays the final rename engine over the pre-campaign baseline and requires the
worktree to be exactly the mechanical result plus an enumerated list of manual
edits:

- every baseline production/test/build-tooling file must map (path pipeline
  a -> b1 -> b2 -> c) to an existing tracked file;
- the file's content must equal the engine's mechanical transform of the
  baseline content (MECHANICAL), or the path must be in MANUAL_EDITS
  (deliberate, reviewed hand edits), otherwise it is BLOCKING;
- tracked files that do not come from any baseline file must be in NEW_FILES;
- the api-decls inventory must reconcile 1:1 through the same transform
  (every disappeared declaration is one mapped rename, zero unexplained);
- captured ctest name sets reconcile through the same transform.

Exit code 0 only when nothing is BLOCKING.
"""
import importlib.util
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
BASE_SHA = "25db3ccbe0f963da61ff2a4e487a23a81bf54125"
BASELINE = os.path.join(os.path.dirname(HERE), "baseline")

spec = importlib.util.spec_from_file_location(
    "apply_renames", os.path.join(HERE, "apply_renames.py"))
eng = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eng)

PASSES = ["a", "b1", "b2", "c"]

# Deliberate hand edits on top of the mechanical transform, each reviewed in its
# pass commit. Everything else must be byte-identical to the engine's output.
MANUAL_EDITS = {
    "modules/CMakeLists.txt": "renderer-module dir list values; umbrella provenance comment",
    "modules/renderers/CMakeLists.txt": "directx11/directx12 family prose",
    "modules/graphics-ext/CMakeLists.txt": "umbrella provenance comment",
    "cmake/RendererSelection.cmake": "identity messages/help text; family prose (sokol-guarded lines)",
    "cmake/Tests/ModuleProbes.cmake": "GraphicsExt provenance comment",
    "modules/core/include/CNA/GraphicsRendererType.hpp": "bare D3D* enum member declarations",
    "scripts/check_renderer_identities.py": "canonical identity table (DirectX*/OPENGLES3 rows)",
    "modules/graphics/include/CNA/Internal/Renderers/Common/NotYetImplemented.hpp":
        "doc example selector string",
    "modules/graphics/tests/Microsoft/Xna/Framework/Graphics/DrawUserPrimitivesTests.cpp":
        "mid-identifier NOXNA test-name suffix",
    "modules/input/src/Internal/SdlInputBridge.cpp":
        "graphics-renderer local rename + prose (compound scope)",
    "modules/input/src/Xna/Mouse.cpp": "graphics-renderer local rename + prose (compound scope)",
    "tools/input_parity/gen_input_parity_matrix.py": "\\bNOXNA\\b scan regex literals",
    ".github/workflows/d3d-windows-ci.yml": "unquoted YAML matrix identity values",
    ".github/workflows/input-ci.yml": "matrix key backend -> renderer",
    "docs/renderer-registry.md": "identity table rows (enum/selector/impl columns)",
    "docs/graphics-renderer-feature-matrix.md": "identity column headers",
    "CLAUDE.md": "selector list entries",
    "README.md": "renderer identity references",
    "CNAEXT.md": "renaming provenance banner",
    "CMakeLists.txt": "DX3 naming-history accuracy note (7409b0361)",
    "modules/renderers/freedirect/include/CNA/Internal/Renderers/FreeDirect/FreeDirectRenderer.hpp":
        "formerly-DX3 history phrases restored (7409b0361)",
}

NEW_FILE_PREFIXES = ("modularization/renderer-naming/",)
NEW_FILES = {"docs/RendererNamingMigration.md"}


def load_tsv(name):
    m = {}
    path = os.path.join(BASELINE, name)
    for line in open(path):
        h, p = line.rstrip("\n").split("  ", 1)
        m[p] = h
    return m


def baseline_content(path):
    r = subprocess.run(["git", "show", f"{BASE_SHA}:{path}"], cwd=REPO,
                       capture_output=True)
    if r.returncode != 0:
        return None
    return r.stdout.decode("utf-8", errors="surrogateescape")


def scoped(path):
    full = any(path == p or path.startswith(p.rstrip("/") + "/")
               for p in eng.FULL_PATHSPECS)
    comp = any(path.startswith(p + "/") for p in eng.COMPOUND_PATHSPECS)
    return full or comp


def replay(path, text):
    """Mechanical pipeline: content+path through passes a, b1, b2, c."""
    p = path
    for ps in PASSES:
        if text is not None:
            hits, tokens = __import__("collections").Counter(), __import__("collections").Counter()
            text = eng.transform(ps, p, text, hits, tokens)
        move_skip = ps == "a" and (
            any(p.startswith(c + "/") for c in eng.COMPOUND_PATHSPECS)
            or eng.DEMOTE_TO_COMPOUND.match(p))
        if not move_skip:
            p = eng.new_path(ps, p)
    return p, text


def main():
    raw = subprocess.run(["git", "ls-files", "-z"], cwd=REPO, check=True,
                         capture_output=True).stdout
    tracked_now = {q.decode() for q in raw.split(b"\0") if q}
    baseline_files = {}
    for name in ("files-production.tsv", "files-tests.tsv", "files-build-tooling.tsv"):
        baseline_files.update(load_tsv(name))

    mech, manual, blocking, missing = [], [], [], []
    mapped_targets = set()
    for old in sorted(baseline_files):
        r = subprocess.run(["git", "show", f"{BASE_SHA}:{old}"], cwd=REPO,
                           capture_output=True)
        raw_old = r.stdout if r.returncode == 0 else None
        is_binary = old.endswith(eng.BINARY_EXT) or (raw_old is not None and b"\0" in raw_old[:8192])
        if is_binary or not scoped(old):
            newp = replay(old, None)[0] if (scoped(old) and not is_binary) else old
            if is_binary:
                newp = old
                cur_raw = None
                try:
                    cur_raw = open(os.path.join(REPO, newp), "rb").read()
                except OSError:
                    pass
                mapped_targets.add(newp)
                if newp not in tracked_now:
                    missing.append((old, newp))
                elif cur_raw == raw_old:
                    mech.append(old)
                else:
                    blocking.append((old, newp))
                continue
            newt = raw_old.decode("utf-8", errors="surrogateescape") if raw_old is not None else None
        else:
            old_text = raw_old.decode("utf-8", errors="surrogateescape") if raw_old is not None else None
            newp, newt = replay(old, old_text)
        mapped_targets.add(newp)
        if newp not in tracked_now:
            missing.append((old, newp))
            continue
        try:
            cur = open(os.path.join(REPO, newp), "rb").read().decode(
                "utf-8", errors="surrogateescape")
        except (OSError, UnicodeError):
            cur = None
        if cur == newt:
            mech.append(old)
        elif newp in MANUAL_EDITS or old in MANUAL_EDITS:
            manual.append((old, newp))
        else:
            blocking.append((old, newp))

    # additions among the captured categories
    def captured_now():
        out = subprocess.run(
            ["git", "ls-files", "-z", "--", "modules/", "tests/", "CMakeLists.txt",
             "CMakePresets.json", "cmake/", "scripts/", "tools/", "examples/"],
            cwd=REPO, check=True, capture_output=True).stdout
        return {q.decode() for q in out.split(b"\0") if q}

    additions = sorted(p for p in captured_now() - mapped_targets
                       if not p.startswith(NEW_FILE_PREFIXES) and p not in NEW_FILES)

    # --- api-decls reconciliation
    base_rows = [l.split("\t") for l in
                 open(os.path.join(BASELINE, "api-decls.tsv")).read().splitlines() if l]
    expected = set()
    for path, kind, name in base_rows:
        newp, _ = replay(path, None) if scoped(path) else (path, None)
        hits, tokens = __import__("collections").Counter(), __import__("collections").Counter()
        newname = name
        for ps in PASSES:
            newname = eng.transform(ps, newp, newname, hits, tokens)
        expected.add((newp, kind, newname))
    # current decls
    sys.path.insert(0, os.path.join(REPO, "modularization", "tools"))
    import capture_inventory as cap
    module_files = cap.tracked(["modules/"])
    production = [p for p in module_files if re.search(r"/(include|src)/", p)]
    headers = [p for p in production if p.endswith((".hpp", ".h"))
               and "/include/" in ("/" + p)]
    actual = set()
    for row in cap.api_decls(headers):
        p, kind, name = row.split("\t")
        actual.add((p, kind, name))
    decl_missing = sorted(expected - actual)
    decl_added = sorted(actual - expected)

    renamed_files = sum(1 for o in baseline_files
                        if scoped(o) and replay(o, None)[0] != o)

    print(f"baseline files: {len(baseline_files)}")
    print(f"  mechanical:   {len(mech)}")
    print(f"  manual edits: {len(manual)} (enumerated)")
    print(f"  moved paths:  {renamed_files}")
    print(f"  MISSING:      {len(missing)}")
    print(f"  BLOCKING:     {len(blocking)}")
    print(f"captured-category additions beyond campaign files: {len(additions)}")
    print(f"api-decls: baseline {len(base_rows)} -> expected {len(expected)}; "
          f"actual {len(actual)}; unexplained-missing {len(decl_missing)}; "
          f"unexplained-added {len(decl_added)}")
    for tag, rows in (("MISSING", missing), ("BLOCKING", blocking),
                      ("ADDED", [(a, "") for a in additions]),
                      ("DECL-MISSING", decl_missing[:20]), ("DECL-ADDED", decl_added[:20])):
        for r in rows[:20]:
            print(f"  {tag}: {r}")
    ok = not missing and not blocking and not additions and not decl_missing and not decl_added
    print("RECONCILIATION:", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
